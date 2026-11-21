"""E8 report: thread-set calibration repeatability (E8a) and ground truth (E8b), from results/calibration/.

    .venv/bin/python -I scripts/make_e8_report.py

Writes results/calibration/SUMMARY.md and fig_e8_calibration.png. Uses the M6 report's helpers and
style; the sustained-regime ranking of the same thread sets is read from results/m6/e2_threadsets.csv.
"""
import csv
import os
import statistics
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import make_m6_report as m6  # noqa: E402  (sets the shared plot style)

import matplotlib.pyplot as plt  # noqa: E402

m6.D = os.path.join(ROOT, "results", "calibration")
md = m6.md
SETS = ["physical", "all", "e", "p"]
LABEL = {"physical": "1 per physical core (10)", "all": "all logical CPUs (12)", "e": "8 E-cores", "p": "2 P-cores + hyperthreads (4)"}


def e8a():
    rows = m6.read_csv("e8a_repeatability.csv")
    if not rows:
        return None
    md("## E8a — Repeatability: 10 calibrations per budget (Swiss-Prot, 20 s idle before each)")
    md(m6.sysinfo_line("e8a_repeatability.sysinfo.txt"))
    md()
    runs = defaultdict(dict)                       # (budget, round) -> one calibration
    for r in rows:
        k = (r["budget_s"], r["round"])
        runs[k].setdefault("chosen", r["chosen"])
        runs[k].setdefault("fastest", r["fastest"])
        runs[k]["seconds"] = float(r["seconds"])
        runs[k][r["set"]] = float(r["median_gcups"])
    budgets = sorted({b for b, _ in runs}, key=float)
    md("| Budget (s) | Runs | Chosen | Fastest median | Wall time, median (min–max) s | physical / all, median ratio (min–max) |")
    md("|---|---|---|---|---|---|")
    per_budget = {}
    for b in budgets:
        rs = [v for (bb, _), v in runs.items() if bb == b]
        ch = Counter(v["chosen"] for v in rs)
        fa = Counter(v["fastest"] for v in rs)
        secs = [v["seconds"] for v in rs]
        ratio = [v["physical"] / v["all"] for v in rs if "all" in v]
        per_budget[b] = rs
        md(f"| {b} | {len(rs)} | " + ", ".join(f"{k} {n}" for k, n in ch.most_common()) + " | "
           + ", ".join(f"{k} {n}" for k, n in fa.most_common())
           + f" | {statistics.median(secs):.2f} ({min(secs):.2f}–{max(secs):.2f}) | "
           + f"{statistics.median(ratio):.3f} ({min(ratio):.3f}–{max(ratio):.3f}) |")
    md()
    md("Each calibration: 5 rounds, sets in rotated order, query = the Swiss-Prot sequence closest to length 400, "
       "decision margin 5% (keep `physical` unless another set's median is more than 5% faster).")
    md()
    md("Per-set calibration speed, median over the 10 runs of each run's median GCUPS:")
    md()
    md("| Budget (s) | " + " | ".join(LABEL[s] for s in SETS) + " |")
    md("|---|" + "---|" * len(SETS))
    for b in budgets:
        md(f"| {b} | " + " | ".join(f"{statistics.median([v[s] for v in per_budget[b] if s in v]):.1f}" for s in SETS) + " |")
    md()
    return per_budget


def e8b():
    rows = m6.read_csv("e8b_burst_sets.csv")
    if not rows:
        return None
    md("## E8b — Ground truth: one full Swiss-Prot search per run, query length 400, burst regime (20 s idle before each)")
    md(m6.sysinfo_line("e8b_burst_sets.sysinfo.txt"))
    md()
    pc = m6.per_config(rows)
    byset = {c.split(":")[1]: v for c, v in pc.items()}
    best = min(v["median"] for v in byset.values())
    md("| Thread set | Median time (s) | IQR % | GCUPS | Regret vs best |")
    md("|---|---|---|---|---|")
    for s in sorted(byset, key=lambda s: byset[s]["median"]):
        v = byset[s]
        md(f"| {LABEL.get(s, s)} | {v['median']:.2f} | {v['iqr_pct']:.1f} | {v['gcups']:.2f} | +{100 * (v['median'] / best - 1):.1f}% |")
    md()
    md(f"Rounds: {next(iter(byset.values()))['n']}, random order per round.")
    md()
    return byset


def e2_sustained():
    path = os.path.join(ROOT, "results", "m6", "e2_threadsets.csv")
    if not os.path.exists(path):
        return None
    pc = m6.per_config(list(csv.DictReader(open(path))))
    return {c.split(":")[1]: v for c, v in pc.items() if c.split(":")[1] in SETS}


def figure(per_budget, burst, sust):
    if not per_budget or "1" not in per_budget:
        return
    groups = [("calibration trials, 1 s budget (E8a)", m6.S1,
               {s: [v[s] for v in per_budget["1"] if s in v] for s in SETS})]
    if burst:
        groups.append(("full search, burst (E8b)", m6.S2, {s: burst[s] for s in SETS if s in burst}))
    if sust:
        groups.append(("full search, sustained, 3 queries (M6 E2)", m6.S3, {s: sust[s] for s in SETS if s in sust}))
    fig, ax = plt.subplots(figsize=(7.5, 3.4))
    w = 0.8 / len(groups)
    for gi, (name, color, data) in enumerate(groups):
        xs, ys, lo, hi = [], [], [], []
        for si, s in enumerate(SETS):
            if s not in data:
                continue
            d = data[s]
            if isinstance(d, list):
                q1, med, q3 = m6.quart(d)
            else:
                q1, med, q3 = d["gq1"], d["gcups"], d["gq3"]
            xs.append(si - 0.4 + w * (gi + 0.5)); ys.append(med); lo.append(med - q1); hi.append(q3 - med)
        ax.bar(xs, ys, width=w, color=color, edgecolor=m6.SURFACE, linewidth=2, label=name)
        ax.errorbar(xs, ys, yerr=[lo, hi], fmt="none", ecolor=m6.INK2, elinewidth=1, capsize=3)
    ax.set_xticks(range(len(SETS)), [LABEL[s] for s in SETS])
    ax.set_ylabel("GCUPS (median; whiskers = IQR)")
    ax.set_title("Thread-set ranking: calibration vs full searches")
    ax.legend(fontsize=8, loc="upper right")
    ax.set_ylim(0, max(ax.get_ylim()[1], 1) * 1.12)
    m6.save(fig, "fig_e8_calibration.png")


def main():
    md("# E8 — Thread-set calibration (generated by `scripts/make_e8_report.py`)")
    md()
    md("Question, expectations and design: `PLAN.md` (written before the run) and `DECISIONS.md` D5. "
       "All numbers measured on the student's laptop (i3-1220P), AC power. GCUPS counts real cells only.")
    md()
    pb = e8a()
    burst = e8b()
    sust = e2_sustained()
    if sust:
        md("Sustained-regime reference (M6 E2, 3 queries 150/400/990, 2 s conditioning before each run): "
           + ", ".join(f"{LABEL[s]} {sust[s]['gcups']:.1f}" for s in SETS if s in sust) + " GCUPS.")
        md()
    figure(pb, burst, sust)
    with open(os.path.join(m6.D, "SUMMARY.md"), "w") as f:
        f.write("\n".join(m6.out_md) + "\n")
    print("wrote", os.path.join(m6.D, "SUMMARY.md"))


if __name__ == "__main__":
    main()
