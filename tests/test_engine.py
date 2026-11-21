"""Engine-level tests through the real command-line interface (blueprint §6, items 4–6).

Checks the JSON interface the web app will use, robustness on bad input, .bin vs .fasta equivalence,
hit ordering, and that every reported alignment re-scores to its reported score.
Usage: .venv/bin/python -I tests/test_engine.py   (run from anywhere; needs bin/hysw_engine)
"""
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(ROOT, "bin", "hysw_engine")
ECOLI = os.path.join(ROOT, "data", "ecoli_k12.fasta")
QDIR = os.path.join(ROOT, "tests", "queries")
ALPHABET = "ARNDCQEGHILKMFPSTWYVBZX*"
checks = failures = 0


def check(cond, msg):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("  FAIL:", msg)


def engine(*args):
    p = subprocess.run([ENGINE, *args], capture_output=True, text=True)
    try:
        out = json.loads(p.stdout) if p.stdout.strip() else None
    except json.JSONDecodeError as e:
        out = {"_bad_json": str(e)}
    return p.returncode, out, p.stderr


def blosum():
    rows, cols = {}, None
    for line in open(os.path.join(ROOT, "data", "BLOSUM62")):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split()
        if cols is None:
            cols = parts
        else:
            rows[parts[0]] = dict(zip(cols, map(int, parts[1:])))
    return rows


def rescore(a, M, go=12, ge=1):
    s = 0
    for k, (x, y) in enumerate(zip(a["q"], a["t"])):
        if x == "-" or y == "-":
            prev_gap = k > 0 and ((x == "-" and a["q"][k - 1] == "-") or (y == "-" and a["t"][k - 1] == "-"))
            s -= ge if prev_gap else go
        else:
            s += M[x][y]
    return s


def write(tmp, name, text):
    path = os.path.join(tmp, name)
    with open(path, "w") as f:
        f.write(text)
    return path


def main():
    M = blosum()
    with tempfile.TemporaryDirectory() as tmp:
        dbbin = os.path.join(tmp, "ecoli.bin")
        rc, out, _ = engine("prep", "--fasta", ECOLI, "--out", dbbin)
        check(rc == 0 and out["sequences"] == 4403, f"prep: rc={rc} out={out}")

        print("[a] search output, ordering and alignments")
        q = os.path.join(QDIR, "oracle_set.fasta")
        rc, res, err = engine("search", "--db", dbbin, "--query", q, "--top-k", "25", "--cores", "physical", "--trace")
        check(rc == 0 and res and res.get("status") == "ok", f"search failed: rc={rc} {err[:200]}")
        n_aln = 0
        for qr in res["queries"]:
            hits = qr["hits"]
            check(len(hits) == 25, "25 hits expected")
            check(all((hits[i]["score"], -hits[i]["db_index"]) >= (hits[i + 1]["score"], -hits[i + 1]["db_index"])
                      for i in range(len(hits) - 1)), "hits not sorted by score")
            check(hits[0]["score"] > 0 and hits[0]["identity"] == 1.0, "a query from the database must find itself first")
            for h in hits:
                if "alignment" in h:
                    n_aln += 1
                    a = h["alignment"]
                    check(rescore(a, M) == h["score"], f"alignment of {h['accession']} rescores to {rescore(a, M)} != {h['score']}")
                    check(len(a["q"]) == len(a["t"]) == len(a["mid"]) == a["length"], "alignment strings have unequal length")
            check(sum(t["items"] for t in qr["threads"]) == 276, "thread item counts must add up to the 276 batches")
            check(all("busy" in t for t in qr["threads"]), "--trace must give busy intervals per thread")
        print(f"  {len(res['queries'])} queries, {n_aln} alignments re-scored from the JSON strings")

        print("[b] .bin and .fasta databases give identical results; modes agree")
        d1, d2, d3 = (os.path.join(tmp, f"s{k}.bin") for k in range(3))
        engine("search", "--db", dbbin, "--query", q, "--mode", "simd", "--dump-scores", d1)
        engine("search", "--db", ECOLI, "--query", q, "--mode", "simd_threads", "--cores", "all", "--policy", "steal", "--dump-scores", d2)
        engine("search", "--db", dbbin, "--query", os.path.join(QDIR, "q_medium.fasta"), "--mode", "scalar", "--dump-scores", d3)
        b1, b2 = open(d1, "rb").read(), open(d2, "rb").read()
        check(b1 == b2 and len(b1) == 4 * 4403 * 8, "simd(.bin, 1 thread) != simd_threads(.fasta, all CPUs, steal)")
        medium_index = 3                                  # q_medium is the 4th record of oracle_set.fasta
        b3 = open(d3, "rb").read()
        check(b1[4 * 4403 * medium_index: 4 * 4403 * (medium_index + 1)] == b3, "scalar mode != SIMD mode for q_medium")

        print("[c] robustness on bad or unusual input")
        cases = {
            "no_header.fasta": ("MKTAYIAKQR\n", False),
            "empty.fasta": ("", False),
            "empty_seq.fasta": (">empty\n\n", False),
            "dna.fasta": (">dna\nACGTACGTACGTAAACCCGGGTTT\n", True),          # valid letters: searched as protein
            "lower_unknown.fasta": (">x\nmktayiakqrUUOJ12 34*-\nqisfvksh\n", True),
            "crlf.fasta": (">crlf\r\nMKTAYIAKQRQISFVKSHFSRQ\r\n", True),
        }
        for name, (text, should_work) in cases.items():
            rc, out, err = engine("search", "--db", dbbin, "--query", write(tmp, name, text), "--top-k", "3")
            if should_work:
                check(rc == 0 and out and out.get("status") == "ok", f"{name}: expected success, got rc={rc} {err[:120]}")
            else:
                check(rc != 0 and out and "error" in out, f"{name}: expected a JSON error, got rc={rc} out={out}")
        rc, out, _ = engine("search", "--db", dbbin, "--query", write(tmp, "lu.fasta", cases["lower_unknown.fasta"][0]))
        check(out["queries"][0]["query"]["length"] == 22 and out["queries"][0]["query"]["unknown_letters"] == 4,
              f"letters only, U/O/J counted as unknown: {out['queries'][0]['query']}")
        for args, what in ((("--db", "/nonexistent.bin"), "missing db"),
                           (("--db", write(tmp, "junk.bin", "not a database at all, just text....." * 4)), "corrupt .bin"),
                           (("--db", dbbin, "--cores", "99"), "offline CPU"),
                           (("--db", dbbin, "--policy", "fastest"), "unknown policy"),
                           (("--db", dbbin, "--top-k", "0"), "top-k 0")):
            rc, out, _ = engine("search", *args, "--query", os.path.join(QDIR, "q_short.fasta"))
            check(rc != 0 and out and "error" in out, f"{what}: expected JSON error, got rc={rc}")

        # query longer than every database sequence; single-sequence database
        long_q = ">long\n" + ("MKTAYIAKQRQISFVKSHFSRQLEERLGLIEVQ" * 200) + "\n"
        rc, out, _ = engine("search", "--db", dbbin, "--query", write(tmp, "long.fasta", long_q), "--top-k", "5")
        check(rc == 0 and out["queries"][0]["query"]["length"] == 6600, "6,600-residue query must work")
        one = write(tmp, "one.fasta", ">sp|P00001|ONE_TEST single\nMKTAYIAKQRQISFVKSHFSRQ\n")
        rc, out, _ = engine("search", "--db", one, "--query", one, "--top-k", "25")
        check(rc == 0 and len(out["queries"][0]["hits"]) == 1 and out["queries"][0]["hits"][0]["identity"] == 1.0,
              "single-sequence database: one hit, identical")
        check(out["queries"][0]["hits"][0]["accession"] == "P00001" and out["queries"][0]["hits"][0]["entry"] == "ONE_TEST",
              "UniProt header parsing")
        print("[d] thread-set calibration")
        rc, cal, err = engine("calibrate", "--db", dbbin, "--budget", "0.3", "--rounds", "3")
        c = (cal or {}).get("calibration", {})
        names = [st["name"] for st in c.get("sets", [])]
        check(rc == 0 and c.get("default") == "physical" and c.get("chosen") in names and c.get("fastest") in names,
              f"calibrate: rc={rc} {err[:160]} {cal}")
        check(all(len(st["gcups"]) == 3 and min(st["gcups"]) > 0 and st["threads"] == len(st["cpus"]) for st in c.get("sets", [])),
              "calibrate: 3 positive measurements per set, threads == len(cpus)")
        meds = {st["name"]: st["median_gcups"] for st in c.get("sets", [])}
        if meds:
            fastest = max(meds, key=meds.get)
            expect = fastest if meds[fastest] > meds["physical"] * 1.05 else "physical"
            check(c["chosen"] == expect, f"calibrate: chose {c['chosen']}, rule says {expect} ({meds})")
        for args, what in ((("--budget", "0"), "budget 0"), (("--margin", "2"), "margin 2"), (("--rounds", "0"), "rounds 0"),
                           (("--rounds", "33"), "rounds 33")):
            rc, out, _ = engine("calibrate", "--db", dbbin, *args)
            check(rc != 0 and out and "error" in out, f"calibrate {what}: expected JSON error, got rc={rc}")
        rc, out, _ = engine("calibrate", "--db", one)
        check(rc != 0 and out and "error" in out, "calibrate on a 1-sequence database: expected JSON error")
        rc, out, _ = engine("calibrate")
        check(rc != 0 and out and "error" in out, "calibrate without --db: expected JSON error")
        d4, d5 = os.path.join(tmp, "s4.bin"), os.path.join(tmp, "s5.bin")
        rc, r4, _ = engine("search", "--db", dbbin, "--query", q, "--cores", "calibrate", "--top-k", "5", "--dump-scores", d4)
        engine("search", "--db", dbbin, "--query", q, "--cores", "physical", "--top-k", "5", "--dump-scores", d5)
        sc = (r4 or {}).get("settings", {})
        chosen = sc.get("calibration", {}).get("chosen")
        chosen_cpus = next((st["cpus"] for st in sc.get("calibration", {}).get("sets", []) if st["name"] == chosen), None)
        check(rc == 0 and r4["status"] == "ok" and sc.get("cores_spec") == "calibrate" and sc.get("cpus") == chosen_cpus,
              f"search --cores calibrate must search with the chosen set's CPUs: rc={rc} {sc.get('cpus')} vs {chosen_cpus}")
        check(open(d4, "rb").read() == open(d5, "rb").read(), "search --cores calibrate must give the same scores as --cores physical")

        print("[e] SIMD batch trace (Inside SIMD view)")
        qm = os.path.join(QDIR, "q_medium.fasta")
        rc, tr, err = engine("simdtrace", "--db", dbbin, "--query", qm, "--accession", "P08506", "--rows", "8", "--cols", "40")
        rc2, sr, _ = engine("search", "--db", dbbin, "--query", qm, "--top-k", "1", "--align-k", "0")
        lane = (tr or {}).get("target_lane", -1)
        check(rc == 0 and tr["sequences"][lane]["accession"] == "P08506" and tr["sequences"][lane]["score"] == sr["queries"][0]["hits"][0]["score"],
              f"simdtrace: the query's own lane must score like the search ({err[:120]})")
        check(rc == 0 and len(tr["H"]) == 8 and all(len(row) == 40 and all(len(c) == 16 for c in row) for row in tr["H"]),
              "simdtrace: H is rows x cols x 16")
        check(rc == 0 and max(tr["H"][7][7]) == tr["H"][7][7][lane], "simdtrace: the self-alignment lane leads on its diagonal")
        rc, tr2, _ = engine("simdtrace", "--db", dbbin, "--query", qm, "--batch", "0", "--row0", "395", "--rows", "20", "--col0", "0", "--cols", "400")
        w2 = (tr2 or {}).get("window", {})            # batch 0 holds the longest proteins: 400 columns fit, 20 rows do not
        check(rc == 0 and w2["rows"] == 5 and w2["cols"] == min(400, w2["batch_length"]), f"simdtrace window clamps: {w2}")
        for args, what in ((("--accession", "NOPE00"), "unknown accession"), (("--batch", "99999"), "batch out of range"),
                           (("--batch", "0", "--rows", "0"), "rows 0"), ((), "no target")):
            rc, out, _ = engine("simdtrace", "--db", dbbin, "--query", qm, *args)
            check(rc != 0 and out and "error" in out, f"simdtrace {what}: expected JSON error, got rc={rc}")

        rc, di, _ = engine("dbinfo", "--db", dbbin)
        check(rc == 0 and di["sequences"] == 4403 and di["residues"] == res["database"]["residues"], f"dbinfo: {di}")
        rc, di, _ = engine("dbinfo", "--db", "/nonexistent.bin")
        check(rc != 0 and di and "error" in di, "dbinfo on a missing file: expected JSON error")
        rc, info, _ = engine("info")
        check(rc == 0 and info["logical_cpus"] >= 1 and len(info["thread_sets"]["all"]) == info["logical_cpus"], "info")

    print(f"ENGINE TESTS {'PASS' if failures == 0 else 'FAIL'}: {checks} checks, {failures} failures")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
