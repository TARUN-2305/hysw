"""Aggregate raw benchmark CSV files from bench/run_bench.py.

A run's time is the sum over its queries of the engine's score-pass time. Per configuration:
median and inter-quartile range over rounds, speedup = baseline median / config median, and
regret = config median / best median - 1. Differences smaller than the IQRs are treated as ties.
Usage: .venv/bin/python -I scripts/analyse.py results/raw/X.csv [--baseline CONFIG] [--out agg.csv]
"""
import argparse
import csv
import statistics
from collections import defaultdict


def quartiles(v):
    v = sorted(v)
    if len(v) < 4:
        return v[0], statistics.median(v), v[-1]
    q = statistics.quantiles(v, n=4, method="inclusive")
    return q[0], q[1], q[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--baseline")
    ap.add_argument("--out")
    a = ap.parse_args()
    runs = defaultdict(lambda: defaultdict(float))       # config -> (file, round) -> seconds
    cells = {}
    threads = {}
    for path in a.csv:
        for r in csv.DictReader(open(path)):
            runs[r["config"]][(path, r["round"])] += float(r["seconds"])
            threads[r["config"]] = r["threads"]
            cells.setdefault(r["config"], defaultdict(float))[(path, r["round"])] += float(r["seconds"]) * float(r["gcups"]) * 1e9
    stats = {}
    for c, by_round in runs.items():
        t = list(by_round.values())
        q1, med, q3 = quartiles(t)
        total_cells = next(iter(cells[c].values()))
        stats[c] = {"config": c, "threads": threads[c], "n": len(t), "median_s": med, "q1_s": q1, "q3_s": q3,
                    "iqr_pct": 100 * (q3 - q1) / med, "gcups": total_cells / med / 1e9}
    best = min(s["median_s"] for s in stats.values())
    base = stats.get(a.baseline, {}).get("median_s") if a.baseline else None
    print(f"{'config':30s} {'thr':>3s} {'n':>3s} {'median_s':>9s} {'IQR%':>6s} {'GCUPS':>7s} {'speedup':>8s} {'regret':>8s}")
    for s in sorted(stats.values(), key=lambda s: s["median_s"]):
        s["regret_pct"] = 100 * (s["median_s"] / best - 1)
        s["speedup"] = base / s["median_s"] if base else ""
        sp = f"{s['speedup']:8.2f}" if base else f"{'':8s}"
        print(f"{s['config']:30s} {s['threads']:>3s} {s['n']:3d} {s['median_s']:9.3f} {s['iqr_pct']:6.1f} {s['gcups']:7.2f} {sp} {s['regret_pct']:+7.1f}%")
    if a.out:
        with open(a.out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(next(iter(stats.values())).keys()))
            w.writeheader()
            w.writerows(stats.values())


if __name__ == "__main__":
    main()
