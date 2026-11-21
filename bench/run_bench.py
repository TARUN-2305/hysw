"""Benchmark harness with randomised, conditioned ordering (blueprint §7).

Two artefacts found in Phase 1/1B are removed by construction:
  * order effects: configurations run in a fresh random order every round (seed recorded);
  * power-budget carry-over: in the *sustained* regime the engine runs the same all-core conditioning
    load (--condition S) immediately before every measured pass, so each run starts from the same
    thermal/power state. The *burst* regime instead sleeps --cooldown seconds and runs unconditioned.
Each measured run is one engine process; the time recorded is the engine's own score-pass time
(database loading, process start-up and traceback are excluded; --align-k 0).

Usage
  .venv/bin/python -I bench/run_bench.py --db data/X.bin --queries tests/queries/bench3.fasta \
      --set threadsets --rounds 9 --regime sustained --out results/raw/NAME.csv
Configuration sets (--set): kernels, threadsets, policies, or an explicit list
  "kernel:cores:policy[:profile];..." (kernel simd | simd_untiled | scalar; profile query (default) | full; e.g. "simd:1:dynamic,1;simd:physical:static;simd:1:dynamic,1:query").
"""
import argparse
import csv
import datetime
import json
import os
import random
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(ROOT, "bin", "hysw_engine")

SETS = {
    "kernels": ["scalar:1:dynamic,1", "simd:1:dynamic,1", "simd:physical:dynamic,1"],
    "threadsets": ["simd:1:dynamic,1", "simd:p1:dynamic,1", "simd:p:dynamic,1", "simd:e:dynamic,1",
                   "simd:physical:dynamic,1", "simd:all:dynamic,1"],
    "policies": [f"simd:physical:{p}" for p in
                 ("static", "dynamic,1", "dynamic,chunk", "guided", "steal", "cost+steal", "costcap+steal")],
}


def pkg_temp():
    for z in sorted(os.listdir("/sys/class/thermal")):
        try:
            if open(f"/sys/class/thermal/{z}/type").read().strip() == "x86_pkg_temp":
                return int(open(f"/sys/class/thermal/{z}/temp").read()) / 1000
        except OSError:
            pass
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--queries", required=True)
    ap.add_argument("--set", required=True)
    ap.add_argument("--rounds", type=int, default=9)
    ap.add_argument("--regime", choices=("sustained", "burst"), default="sustained")
    ap.add_argument("--condition", type=float, default=2.0, help="seconds of all-core load before each pass (sustained)")
    ap.add_argument("--cooldown", type=float, default=20.0, help="idle seconds before each run (burst)")
    ap.add_argument("--seed", type=int, default=20261120)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    configs = SETS.get(a.set, None) or [c for c in a.set.split(";") if c]
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    base = os.path.splitext(a.out)[0]
    with open(base + ".sysinfo.txt", "w") as f:
        f.write(subprocess.run([os.path.join(ROOT, "scripts", "sysinfo.sh")], capture_output=True, text=True).stdout)
    meta = {"command": sys.argv, "started": datetime.datetime.now().isoformat(timespec="seconds"),
            "configs": configs, "rounds": a.rounds, "regime": a.regime, "seed": a.seed,
            "condition_s": a.condition if a.regime == "sustained" else 0,
            "cooldown_s": a.cooldown if a.regime == "burst" else 0, "orders": []}
    fields = ["round", "position", "config", "kernel", "cores", "threads", "policy", "profile", "query", "query_length",
              "seconds", "gcups", "rescored", "regime", "pkg_temp_before_c", "timestamp"]
    n_runs = a.rounds * len(configs)
    done = 0
    with open(a.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for rd in range(a.rounds):
            order = configs[:]
            random.Random(a.seed * 1000 + rd).shuffle(order)          # fresh random order every round
            meta["orders"].append(order)
            for pos, cfg in enumerate(order):
                parts = cfg.split(":")
                kernel, cores, policy = parts[:3]
                profile = parts[3] if len(parts) > 3 else "query"   # engine default (D3)
                cmd = [ENGINE, "search", "--db", a.db, "--query", a.queries, "--kernel", kernel, "--cores", cores,
                       "--policy", policy, "--profile", profile, "--top-k", "1", "--align-k", "0"]
                if a.regime == "sustained":
                    cmd += ["--condition", str(a.condition)]
                else:
                    time.sleep(a.cooldown)
                temp = pkg_temp()
                p = subprocess.run(cmd, capture_output=True, text=True)
                if p.returncode != 0:
                    sys.exit(f"engine failed for {cfg}: {p.stdout[:300]} {p.stderr[:300]}")
                res = json.loads(p.stdout)
                for q in res["queries"]:
                    w.writerow({"round": rd, "position": pos, "config": cfg, "kernel": kernel, "cores": cores,
                                "threads": res["settings"]["threads"], "policy": policy, "profile": profile,
                                "query": q["query"]["header"].split()[0], "query_length": q["query"]["length"],
                                "seconds": q["timing"]["seconds"], "gcups": q["timing"]["gcups"],
                                "rescored": q["timing"]["rescored"], "regime": a.regime, "pkg_temp_before_c": temp,
                                "timestamp": datetime.datetime.now().isoformat(timespec="seconds")})
                f.flush()
                done += 1
                total = sum(q["timing"]["seconds"] for q in res["queries"])
                print(f"[{done}/{n_runs}] round {rd} {cfg:28s} {total:8.3f} s", flush=True)
    meta["finished"] = datetime.datetime.now().isoformat(timespec="seconds")
    json.dump(meta, open(base + ".meta.json", "w"), indent=1)
    print("raw results:", a.out)


if __name__ == "__main__":
    main()
