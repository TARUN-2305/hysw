"""E8a: repeatability of the thread-set calibration (blueprint §4.5, DECISIONS.md D5).

Runs `hysw_engine calibrate` N times at each of several budgets, in a fresh random order per round,
each after an idle cool-down (the app's situation: the user starts a calibration on a quiet machine).
Writes one CSV row per calibration and per thread set, plus the random orders and machine state.

Usage
  .venv/bin/python -I bench/run_calibration.py --db data/swissprot_2026_04/swissprot.bin \
      --budgets 0.25,1,3 --rounds 10 --cooldown 20 --out results/calibration/e8a_repeatability.csv
"""
import argparse
import csv
import datetime
import json
import os
import random
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(ROOT, "bin", "hysw_engine")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--budgets", default="0.25,1,3")
    ap.add_argument("--rounds", type=int, default=10)
    ap.add_argument("--cooldown", type=float, default=20.0)
    ap.add_argument("--seed", type=int, default=20261121)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    budgets = [b.strip() for b in a.budgets.split(",")]
    base = os.path.splitext(a.out)[0]
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(base + ".sysinfo.txt", "w") as f:
        f.write(subprocess.run([os.path.join(ROOT, "scripts", "sysinfo.sh")], capture_output=True, text=True).stdout)
    rng = random.Random(a.seed)
    meta = {"db": a.db, "budgets": budgets, "rounds": a.rounds, "cooldown_s": a.cooldown, "seed": a.seed,
            "started": datetime.datetime.now().astimezone().isoformat(timespec="seconds"), "orders": []}
    fields = ["round", "position", "budget_s", "seconds", "chosen", "fastest", "set", "threads", "median_gcups",
              "gcups_runs", "passes_per_trial", "stride", "query_length", "timestamp"]
    total, n = a.rounds * len(budgets), 0
    with open(a.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in range(a.rounds):
            order = budgets[:]
            rng.shuffle(order)
            meta["orders"].append(order)
            for pos, b in enumerate(order):
                n += 1
                time.sleep(a.cooldown)
                p = subprocess.run([ENGINE, "calibrate", "--db", a.db, "--budget", b], capture_output=True, text=True)
                if p.returncode != 0:
                    raise SystemExit(f"calibrate failed: {p.stdout} {p.stderr}")
                c = json.loads(p.stdout)["calibration"]
                ts = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
                for s in c["sets"]:
                    w.writerow({"round": r, "position": pos, "budget_s": b, "seconds": c["seconds"], "chosen": c["chosen"],
                                "fastest": c["fastest"], "set": s["name"], "threads": s["threads"],
                                "median_gcups": s["median_gcups"], "gcups_runs": " ".join(f"{x:.3f}" for x in s["gcups"]),
                                "passes_per_trial": c["passes_per_trial"], "stride": c["slice"]["stride"],
                                "query_length": c["query_length"], "timestamp": ts})
                f.flush()
                print(f"[{n}/{total}] round {r} budget {b:>5} s  took {c['seconds']:.2f} s  chose {c['chosen']:<8} "
                      f"(fastest {c['fastest']})", flush=True)
    meta["finished"] = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    with open(base + ".meta.json", "w") as f:
        json.dump(meta, f, indent=1)
    print("raw results:", a.out)


if __name__ == "__main__":
    main()
