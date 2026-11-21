"""Oracle test: HySW scores must equal Parasail's for every (query, database sequence) pair.

Parasail (Daily 2016) is an independent, established SIMD alignment library. Both programs use
BLOSUM62 and gap open 12 / extend 1 (a gap of k residues costs 11 + k).
Usage:
  .venv/bin/python -I scripts/oracle_parasail.py <db.fasta> <queries.fasta> <scores.bin> [--record out.json]
<scores.bin> comes from `hysw_engine search ... --dump-scores <scores.bin>`: int32 [query][sequence],
sequences in original FASTA order. Exit status 0 only if every score is identical.
"""
import array
import json
import os
import subprocess
import sys
import time

import parasail

ALPHABET = "ARNDCQEGHILKMFPSTWYVBZX*"
GAP_OPEN, GAP_EXTEND = 12, 1


def read_fasta(path):
    """Same rules as src/fasta.c: letters only, upper-case, letters outside the alphabet -> X."""
    recs, hdr, seq = [], None, []
    allowed = set(ALPHABET)
    for line in open(path):
        line = line.rstrip("\n\r")
        if line.startswith(">"):
            if hdr is not None:
                recs.append((hdr, "".join(seq)))
            hdr, seq = line[1:], []
        elif hdr is not None:
            seq.append("".join((c if c in allowed else "X") for c in line.upper() if c.isalpha()))
    if hdr is not None:
        recs.append((hdr, "".join(seq)))
    return recs


def check_matrix(path):
    """Our BLOSUM62 file must hold exactly Parasail's built-in blosum62 values in the same order."""
    rows = {}
    cols = None
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split()
        if cols is None:
            cols = parts
            continue
        rows[parts[0]] = [int(v) for v in parts[1:]]
    pm = parasail.blosum62.matrix
    for i, a in enumerate(ALPHABET):
        for j, b in enumerate(ALPHABET):
            if rows[a][cols.index(b)] != int(pm[i][j]):
                return False
    return True


def score(q, s):
    r = parasail.sw_striped_16(q, s, GAP_OPEN, GAP_EXTEND, parasail.blosum62)
    if r.saturated or r.score >= 32000:               # at or near the 16-bit limit: always use Parasail's 32-bit kernel
        r = parasail.sw_striped_32(q, s, GAP_OPEN, GAP_EXTEND, parasail.blosum62)
        return r.score, True
    return r.score, False


def main():
    db_path, q_path, dump = sys.argv[1:4]
    record = sys.argv[sys.argv.index("--record") + 1] if "--record" in sys.argv else None
    here = os.path.dirname(os.path.abspath(__file__))
    matrix_ok = check_matrix(os.path.join(here, "..", "data", "BLOSUM62"))
    print(f"data/BLOSUM62 identical to parasail.blosum62: {matrix_ok}")
    db = read_fasta(db_path)
    queries = read_fasta(q_path)
    mine = array.array("i")
    mine.frombytes(open(dump, "rb").read())
    n = len(db)
    if len(mine) != n * len(queries):
        sys.exit(f"dump has {len(mine)} scores, expected {len(queries)} x {n}")
    total, mism, sat, t0 = 0, 0, 0, time.perf_counter()
    first_bad = []
    for qi, (qh, q) in enumerate(queries):
        bad = 0
        for k, (_, s) in enumerate(db):
            sc, was_sat = score(q, s)
            sat += was_sat
            if sc != mine[qi * n + k]:
                bad += 1
                if len(first_bad) < 5:
                    first_bad.append((qi, k, sc, mine[qi * n + k]))
        total += n
        mism += bad
        print(f"  query {qi + 1} (length {len(q):5d}): {n - bad}/{n} identical   {qh[:50]}")
    secs = time.perf_counter() - t0
    ok = mism == 0 and matrix_ok
    print(f"ORACLE {'PASS' if ok else 'FAIL'}: {total - mism}/{total} scores identical to Parasail "
          f"({sat} pairs checked with Parasail's 32-bit kernel), {secs:.1f} s")
    for qi, k, p, h in first_bad:
        print(f"  mismatch: query {qi + 1}, sequence {k + 1}: parasail {p}, hysw {h}")
    if record:
        try:
            ver = subprocess.run(["gcc", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
        except OSError:
            ver = "unknown"
        os.makedirs(os.path.dirname(record) or ".", exist_ok=True)
        json.dump({"date": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "database": db_path, "queries": q_path,
                   "pairs": total, "identical": total - mism, "matrix_identical": matrix_ok,
                   "parasail_version": getattr(parasail, "__version__", "?"), "compiler": ver,
                   "pass": ok}, open(record, "w"), indent=2)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
