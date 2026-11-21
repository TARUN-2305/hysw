"""Same-machine yardstick: HySW one-thread SIMD vs Parasail (Daily 2016) one thread, same queries,
same database, same scoring (BLOSUM62, gap open 12 / extend 1), both pinned to the same P-core.
Parasail: striped 16-bit kernel with a query profile built once per query (sw_striped_profile_16),
called from Python once per database sequence, so its time includes Python call overhead (stated in
the report). Every measured run is preceded by the engine's conditioning load; order is random per round.
Usage: .venv/bin/python -I bench/parasail_yardstick.py --db-fasta F --db-bin B --queries Q --rounds 3 --out X.csv"""
import argparse
import csv
import datetime
import json
import os
import random
import subprocess
import sys
import time

import parasail

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from oracle_parasail import read_fasta  # noqa: E402  (same residue rules as the engine)

ENGINE = os.path.join(ROOT, "bin", "hysw_engine")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db-fasta", required=True)
    ap.add_argument("--db-bin", required=True)
    ap.add_argument("--queries", required=True)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--condition", type=float, default=2.0)
    ap.add_argument("--cpu", type=int, default=0)
    ap.add_argument("--seed", type=int, default=20261122)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.sched_setaffinity(0, {a.cpu})
    db = [s for _, s in read_fasta(a.db_fasta)]
    residues = sum(map(len, db))
    queries = read_fasta(a.queries)
    base = os.path.splitext(a.out)[0]
    with open(base + ".sysinfo.txt", "w") as f:
        f.write(subprocess.run([os.path.join(ROOT, "scripts", "sysinfo.sh")], capture_output=True, text=True).stdout)
    rows = []
    for rd in range(a.rounds):
        order = ["parasail", "hysw"]
        random.Random(a.seed * 100 + rd).shuffle(order)
        for pos, tool in enumerate(order):
            if tool == "hysw":
                p = subprocess.run([ENGINE, "search", "--db", a.db_bin, "--query", a.queries, "--cores", str(a.cpu),
                                    "--top-k", "1", "--align-k", "0", "--condition", str(a.condition)],
                                   capture_output=True, text=True)
                res = json.loads(p.stdout)
                times = [(q["query"]["header"].split()[0], q["query"]["length"], q["timing"]["seconds"]) for q in res["queries"]]
            else:
                times = []
                for h, q in queries:
                    subprocess.run([ENGINE, "condition", "--seconds", str(a.condition)], capture_output=True)
                    prof = parasail.profile_create_16(q, parasail.blosum62)
                    f = parasail.sw_striped_profile_16
                    t0 = time.perf_counter()
                    for s in db:
                        f(prof, s, 12, 1)
                    times.append((h.split()[0], len(q), time.perf_counter() - t0))
            for qh, ql, secs in times:
                rows.append({"round": rd, "position": pos, "config": f"{tool}:cpu{a.cpu}", "query": qh, "query_length": ql,
                             "seconds": secs, "gcups": ql * residues / secs / 1e9, "threads": 1,
                             "timestamp": datetime.datetime.now().isoformat(timespec="seconds")})
            print(f"round {rd} {tool:9s} {sum(t[2] for t in times):8.2f} s", flush=True)
    with open(a.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
