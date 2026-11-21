"""Pick the fixed test queries from the E. coli test proteome (deterministic, re-runnable).

Queries are real proteins chosen by a simple rule so the selection is reproducible:
the protein whose length is closest to each target (ties: first in file), plus the first protein
containing selenocysteine (U, mapped to X by the engine) and the first containing X.
Usage: .venv/bin/python -I scripts/make_test_queries.py data/ecoli_k12.fasta tests/queries
"""
import os
import sys


def read_fasta(path):
    recs, hdr, seq = [], None, []
    for line in open(path):
        line = line.rstrip("\n\r")
        if line.startswith(">"):
            if hdr is not None:
                recs.append((hdr, "".join(seq)))
            hdr, seq = line[1:], []
        elif hdr is not None:
            seq.append(line.strip())
    if hdr is not None:
        recs.append((hdr, "".join(seq)))
    return recs


def write(path, recs):
    with open(path, "w") as f:
        for h, s in recs:
            f.write(">" + h + "\n")
            for k in range(0, len(s), 60):
                f.write(s[k:k + 60] + "\n")


def main():
    src, out = sys.argv[1], sys.argv[2]
    recs = read_fasta(src)
    lens = [len(s) for _, s in recs]
    chosen = {}
    for name, target in (("q_tiny", 10), ("q_50", 50), ("q_short", 150), ("q_medium", 400), ("q_long", 1000)):
        k = min(range(len(recs)), key=lambda i: (abs(lens[i] - target), i))
        chosen[name] = recs[k]
    chosen["q_longest"] = recs[max(range(len(recs)), key=lambda i: (lens[i], -i))]
    chosen["q_selenocys"] = next(r for r in recs if "U" in r[1].upper())
    chosen["q_unknownX"] = next(r for r in recs if "X" in r[1].upper())
    os.makedirs(out, exist_ok=True)
    for name, rec in chosen.items():
        write(os.path.join(out, name + ".fasta"), [rec])
        print(f"{name:12s} length {len(rec[1]):5d}  {rec[0][:70]}")
    write(os.path.join(out, "oracle_set.fasta"), list(chosen.values()))


if __name__ == "__main__":
    main()
