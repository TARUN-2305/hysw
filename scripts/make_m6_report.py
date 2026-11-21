"""Regenerate every M6 table and figure from the raw data in results/m6/ (one command).

    .venv/bin/python -I scripts/make_m6_report.py [results/m6]

Writes results/m6/SUMMARY.md (tables with medians, IQRs and definitions) and results/m6/fig_*.png.
Experiments whose raw files are missing are skipped. Definitions (blueprint §7):
  run time      = sum over the run's queries of the engine's score-pass time
  GCUPS         = query residues x database residues / run time / 1e9 (real cells, padding excluded)
  speedup       = baseline median time / configuration median time
  regret        = configuration median / best median - 1
Differences smaller than the inter-quartile ranges are reported as ties.
"""
import csv
import json
import os
import statistics
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

D = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "results", "m6")

# Reference palette (dataviz skill, light mode): categorical slots in fixed order + chart ink.
S1, S2, S3, S4 = "#2a78d6", "#eb6834", "#1baf7a", "#eda100"
INK, INK2, MUTED, GRID, AXIS, SURFACE = "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7", "#fcfcfb"
P_COLOR, E_COLOR = S1, S2
plt.rcParams.update({
    "font.family": "sans-serif", "font.size": 9, "axes.titlesize": 10, "axes.titleweight": "bold",
    "axes.titlelocation": "left", "axes.edgecolor": AXIS, "axes.labelcolor": INK2, "axes.facecolor": SURFACE,
    "figure.facecolor": SURFACE, "xtick.color": MUTED, "ytick.color": MUTED, "text.color": INK,
    "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True, "grid.color": GRID,
    "grid.linewidth": 0.6, "axes.axisbelow": True, "legend.frameon": False, "lines.linewidth": 2,
})

out_md = []


def md(line=""):
    out_md.append(line)


def read_csv(name):
    path = os.path.join(D, name)
    return list(csv.DictReader(open(path))) if os.path.exists(path) else None


def quart(v):
    v = sorted(v)
    if len(v) < 4:
        return v[0], statistics.median(v), v[-1]
    q = statistics.quantiles(v, n=4, method="inclusive")
    return q[0], q[1], q[2]


def per_config(rows):
    """config -> dict(median, q1, q3, n, gcups, threads) using per-round sums over queries."""
    t = defaultdict(lambda: defaultdict(float))
    cells = defaultdict(lambda: defaultdict(float))
    threads = {}
    for r in rows:
        t[r["config"]][r["round"]] += float(r["seconds"])
        cells[r["config"]][r["round"]] += float(r["seconds"]) * float(r["gcups"]) * 1e9
        threads[r["config"]] = r.get("threads", "1")
    out = {}
    for c in t:
        q1, med, q3 = quart(list(t[c].values()))
        tc = next(iter(cells[c].values()))
        out[c] = {"median": med, "q1": q1, "q3": q3, "n": len(t[c]), "gcups": tc / med / 1e9,
                  "gq1": tc / q3 / 1e9, "gq3": tc / q1 / 1e9, "threads": threads[c], "iqr_pct": 100 * (q3 - q1) / med}
    return out


def sysinfo_line(name):
    path = os.path.join(D, name)
    if not os.path.exists(path):
        return ""
    kv = dict(l.split(": ", 1) for l in open(path).read().splitlines() if ": " in l)
    return (f"Recorded {kv.get('date', '?')}: platform profile `{kv.get('platform_profile')}`, governor `{kv.get('governor')}`, "
            f"long-term power limit {kv.get('power_limit_intel-rapl-mmio:0_long_term_w')} W "
            f"(MMIO zone; window {kv.get('power_limit_intel-rapl-mmio:0_long_term_window_s')} s), "
            f"on AC: {kv.get('on_ac_power')}, package temperature at start {kv.get('temp_x86_pkg_temp_c')} °C.")


def save(fig, name, rect=(0, 0, 1, 1)):
    fig.tight_layout(rect=rect)
    fig.savefig(os.path.join(D, name), dpi=150)
    plt.close(fig)
    md(f"![{name}]({name})")
    md()


def bar_h(ax, labels, values, lo, hi, color, fmt):
    y = range(len(labels))
    ax.barh(y, values, height=0.6, color=color, edgecolor=SURFACE, linewidth=2)
    ax.errorbar(values, y, xerr=[[v - l for v, l in zip(values, lo)], [h - v for v, h in zip(values, hi)]],
                fmt="none", ecolor=INK2, elinewidth=1, capsize=3)
    for yi, v, h in zip(y, values, hi):
        ax.text(h + max(values) * 0.015, yi, fmt(v), va="center", color=INK, fontsize=8)
    ax.set_yticks(list(y))
    ax.set_yticklabels(labels)
    ax.invert_yaxis()
    ax.grid(axis="y", visible=False)
    ax.set_xlim(0, max(hi) * 1.18)


CORE_NAMES = {"1": "1 thread, P-core", "p1": "2 P-cores, 1 thread each", "p": "2 P-cores + hyperthreads (4)",
              "e": "8 E-cores", "physical": "1 per physical core (10)", "all": "all logical CPUs (12)", "4": "1 thread, E-core"}


def e1():
    rows = read_csv("e1_kernels.csv")
    if not rows:
        return
    st = per_config(rows)
    order = ["scalar:1:dynamic,1", "simd:1:dynamic,1", "simd:physical:dynamic,1"]
    names = ["Scalar, 1 P-core thread", "SIMD (16 lanes), 1 P-core thread", "SIMD + threads (1 per physical core)"]
    base = st[order[0]]["median"]
    md("## E1 — Speedup decomposition (Swiss-Prot, query length 400, sustained)")
    md(sysinfo_line("e1_kernels.sysinfo.txt"))
    md()
    md("| Configuration | Threads | Median time (s) | IQR (% of median) | GCUPS | Speedup vs scalar | Step speedup |")
    md("|---|---|---|---|---|---|---|")
    prev = None
    for c, n in zip(order, names):
        s = st[c]
        step = f"{prev / s['median']:.2f}×" if prev else "—"
        md(f"| {n} | {s['threads']} | {s['median']:.2f} | {s['iqr_pct']:.1f} | {s['gcups']:.2f} | {base / s['median']:.1f}× | {step} |")
        prev = s["median"]
    md(f"\nRounds per configuration: {st[order[0]]['n']}. Total speedup = SIMD step × thread step.\n")
    fig, ax = plt.subplots(figsize=(7, 2.4))
    g = [st[c]["gcups"] for c in order]
    speed = {round(st[c]["gcups"], 6): base / st[c]["median"] for c in order}
    bar_h(ax, names, g, [st[c]["gq1"] for c in order], [st[c]["gq3"] for c in order], S1,
          lambda v: f"{v:.1f} GCUPS  ({speed[round(v, 6)]:.1f}× scalar)")
    ax.set_xlim(0, max(g) * 1.75)
    ax.set_xlabel("GCUPS (median; whiskers = inter-quartile range)")
    ax.set_title("Speed of each level of parallelism\nSwiss-Prot, query length 400", fontsize=10)
    save(fig, "fig_e1_speedup.png")


def e2():
    rows = read_csv("e2_threadsets.csv")
    if not rows:
        return
    st = per_config(rows)
    best = min(s["median"] for s in st.values())
    base = st["simd:1:dynamic,1"]["median"]
    md("## E2 — Which cores? Thread sets (Swiss-Prot, 3 queries 150/400/990, SIMD, `dynamic,1`, sustained)")
    md(sysinfo_line("e2_threadsets.sysinfo.txt"))
    md()
    md("| Thread set | Threads | Median time (s) | IQR % | GCUPS | Speedup vs 1 thread | Regret vs best |")
    md("|---|---|---|---|---|---|---|")
    order = sorted(st, key=lambda c: st[c]["median"])
    for c in order:
        s = st[c]
        md(f"| {CORE_NAMES[c.split(':')[1]]} | {s['threads']} | {s['median']:.2f} | {s['iqr_pct']:.1f} | {s['gcups']:.2f} | "
           f"{base / s['median']:.2f}× | {100 * (s['median'] / best - 1):+.1f}% |")
    md(f"\nRounds: {st[order[0]]['n']}.\n")
    fig, ax = plt.subplots(figsize=(7, 3))
    bar_h(ax, [CORE_NAMES[c.split(':')[1]] for c in order], [st[c]["gcups"] for c in order],
          [st[c]["gq1"] for c in order], [st[c]["gq3"] for c in order], S1, lambda v: f"{v:.1f}")
    ax.set_xlabel("GCUPS (median; whiskers = inter-quartile range)")
    ax.set_title("Which cores to use? Same search, different thread sets")
    save(fig, "fig_e2_threadsets.png")


def e3():
    rows = read_csv("e3_policies.csv")
    if not rows:
        return
    st = per_config(rows)
    best = min(s["median"] for s in st.values())
    order = sorted(st, key=lambda c: st[c]["median"])
    md("## E3 — Batch-to-thread mapping policies (Swiss-Prot, 3 queries, 1 thread per physical core, sustained)")
    md(sysinfo_line("e3_policies.sysinfo.txt"))
    md()
    md("| Policy | Median time (s) | IQR % | GCUPS | Regret vs best |")
    md("|---|---|---|---|---|")
    for c in order:
        s = st[c]
        md(f"| `{c.split(':', 2)[2]}` | {s['median']:.2f} | {s['iqr_pct']:.1f} | {s['gcups']:.2f} | {100 * (s['median'] / best - 1):+.1f}% |")
    md(f"\nRounds: {st[order[0]]['n']}. Regret within the IQRs = tie.\n")
    fig, ax = plt.subplots(figsize=(7, 2.9))
    reg = [100 * (st[c]["median"] / best - 1) for c in order]
    lo = [100 * (st[c]["q1"] / best - 1) for c in order]
    hi = [100 * (st[c]["q3"] / best - 1) for c in order]
    y = range(len(order))
    ax.barh(y, reg, height=0.6, color=S1, edgecolor=SURFACE, linewidth=2)
    ax.errorbar(reg, y, xerr=[[max(0, r - l) for r, l in zip(reg, lo)], [h - r for r, h in zip(reg, hi)]], fmt="none",
                ecolor=INK2, elinewidth=1, capsize=3)
    for yi, r, h in zip(y, reg, hi):
        ax.text(max(h, r) + 1, yi, f"+{r:.1f}%", va="center", fontsize=8)
    ax.set_yticks(list(y))
    ax.set_yticklabels([c.split(":", 2)[2] for c in order])
    ax.invert_yaxis()
    ax.grid(axis="y", visible=False)
    ax.set_xlim(min(0, min(lo)) - 2, max(hi) * 1.2 + 3)
    ax.set_xlabel("extra time vs the best policy, % (median; whiskers = inter-quartile range)")
    ax.set_title("Mapping length-sorted batches to threads")
    save(fig, "fig_e3_policies.png")
    traces = [os.path.join(D, f"e3_trace_{p}.json") for p in ("static", "dynamic")]
    if all(os.path.exists(t) for t in traces):
        fig, axes = plt.subplots(2, 1, figsize=(7.5, 6), sharex=True)
        for ax, path in zip(axes, traces):
            d = json.load(open(path))
            q = d["queries"][0]
            for row, t in enumerate(q["threads"]):
                ax.broken_barh([(a, b - a) for a, b in t["busy"]], (row - 0.38, 0.76),
                               facecolors=P_COLOR if t["type"] == "P" else E_COLOR, linewidth=0)
            ax.axvline(q["timing"]["seconds"], color=INK2, lw=1, ls="--")
            ax.set_yticks(range(len(q["threads"])))
            ax.set_yticklabels([f"cpu{t['cpu']} {t['type']}" for t in q["threads"]], fontsize=7.5)
            ax.invert_yaxis()
            ax.grid(axis="y", visible=False)
            ax.set_title(f"{d['settings']['policy']}: {q['timing']['seconds']:.2f} s", fontsize=9)
        from matplotlib.patches import Patch
        axes[0].legend(handles=[Patch(color=P_COLOR, label="P-core thread"), Patch(color=E_COLOR, label="E-core thread")],
                       loc="lower right", fontsize=8)
        axes[-1].set_xlabel("seconds since the start of the score pass (bars = thread busy computing batches)")
        fig.suptitle("Per-thread timeline, Swiss-Prot, query length 400", x=0.02, ha="left", fontweight="bold", fontsize=10)
        save(fig, "fig_e3_timeline.png")


def e4():
    passes, samples = read_csv("e4_regime_passes.csv"), read_csv("e4_regime_samples.csv")
    if not passes:
        return
    md("## E4 — Burst → sustained (Swiss-Prot, query length 150, continuous repeated searches after a 60 s idle cool-down)")
    md(sysinfo_line("e4_regime.sysinfo.txt"))
    md()
    md("| Thread set | Passes | First pass GCUPS | Median of first 5 s | Median after 40 s | Drop |")
    md("|---|---|---|---|---|---|")
    configs = [c for c in ("1", "physical", "all") if any(p["config"] == c for p in passes)]
    colors = dict(zip(configs, (S1, S2, S3)))
    fig, axes = plt.subplots(3, 1, figsize=(7.5, 7.2), sharex=True)
    for c in configs:
        ps = [p for p in passes if p["config"] == c]
        t = [float(p["t_start"]) + float(p["seconds"]) / 2 for p in ps]
        g = [float(p["gcups"]) for p in ps]
        early = [gi for ti, gi in zip(t, g) if ti <= 5] or g[:1]
        late = [gi for ti, gi in zip(t, g) if ti >= 40]
        late_m = statistics.median(late) if late else float("nan")
        md(f"| {CORE_NAMES[c]} | {len(ps)} | {g[0]:.1f} | {statistics.median(early):.1f} | {late_m:.1f} | "
           f"{100 * (late_m / statistics.median(early) - 1):+.0f}% |")
        axes[0].plot(t, g, color=colors[c], marker="o", markersize=3, label=CORE_NAMES[c])
        if samples:
            sm = [s for s in samples if s["config"] == c]
            ts = [float(s["t"]) for s in sm]
            axes[1].plot(ts, [float(s["mhz_cpu0"]) for s in sm], color=colors[c], lw=1.5, label=CORE_NAMES[c])
            axes[2].plot(ts, [float(s["pkg_temp_c"]) for s in sm], color=colors[c], lw=1.5, label=CORE_NAMES[c])
    axes[0].set_ylabel("GCUPS per pass")
    axes[0].set_ylim(bottom=0)
    axes[0].legend(fontsize=8, loc="upper right")
    axes[0].set_title("Speed of each search pass")
    axes[1].set_ylabel("cpu0 clock (MHz)")
    axes[1].set_title("Clock of P-core cpu0 (busy in every configuration)")
    axes[2].set_ylabel("package °C")
    axes[2].set_title("Package temperature")
    axes[2].set_xlabel("seconds since the first pass started")
    md("\nDrop = median GCUPS after 40 s vs the first 5 s. The long-term power limit uses a 28 s averaging window.\n")
    save(fig, "fig_e4_regime.png")


E5_VARIANTS = (("simd", "query", S1, "tiled kernel, query profile (default)"),
               ("simd", "full", S2, "tiled kernel, full profile"),
               ("simd_untiled", "query", S3, "untiled kernel, query profile"),
               ("simd_untiled", "full", S4, "untiled kernel, full profile"))


def e5():
    rows = read_csv("e5_querylen_profile.csv")
    if not rows:
        return
    g = defaultdict(list)
    for r in rows:
        g[(r["cores"], r["kernel"], r["profile"], int(r["query_length"]))].append(float(r["gcups"]))
    lengths = sorted({k[3] for k in g})
    variants = [v for v in E5_VARIANTS if any(k[1] == v[0] and k[2] == v[1] for k in g)]
    md("## E5 — Query length × kernel variant × core type (E. coli × 20 = 27.1 M residues, sustained)")
    md(sysinfo_line("e5_querylen_profile.sysinfo.txt"))
    md()
    md("Median GCUPS per query (rounds: %d). *tiled* = column blocks of 16 (default), *untiled* = the original "
       "column-by-column loop; *query* profile = rows only for the query's residues (default), *full* = all 24 codes.\n"
       % len(g[next(iter(g))]))
    md("| Cores | Variant | " + " | ".join(f"L={L}" for L in lengths) + " |")
    md("|---|---|" + "---|" * len(lengths))
    cores_list = [c for c in ("1", "4", "physical") if any(k[0] == c for k in g)]
    fig, axes = plt.subplots(1, len(cores_list), figsize=(9.5, 3.6), sharey=False)
    for ax, c in zip(axes, cores_list):
        for kern, prof, col, lab in variants:
            vals = [statistics.median(g[(c, kern, prof, L)]) for L in lengths]
            md(f"| {CORE_NAMES[c]} | {lab} | " + " | ".join(f"{v:.2f}" for v in vals) + " |")
            ax.plot(lengths, vals, color=col, marker="o", markersize=4, label=lab)
        ax.set_xscale("log")
        ax.set_title(CORE_NAMES[c], fontsize=9)
        ax.set_xlabel("query length (log scale)")
        ax.set_ylim(bottom=0)
    axes[0].set_ylabel("GCUPS (median)")
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=len(labels), fontsize=8, bbox_to_anchor=(0.5, 0.0))
    md()
    save(fig, "fig_e5_querylen.png", rect=(0, 0.08, 1, 1))


def e7():
    rows = read_csv("e7_tiling.csv")
    if not rows:
        return
    g = defaultdict(list)
    for r in rows:
        g[(r["cores"], r["kernel"], int(r["query_length"]))].append(float(r["gcups"]))
    lengths = sorted({k[2] for k in g})
    cores_list = [c for c in ("physical", "all") if any(k[0] == c for k in g)]
    md("## E7 — Before/after the cache fix: untiled vs tiled SIMD kernel (Swiss-Prot, 3 queries, sustained)")
    md(sysinfo_line("e7_tiling.sysinfo.txt"))
    md()
    md("Median GCUPS per query (rounds: %d); ratio = tiled / untiled." % len(g[next(iter(g))]))
    md()
    md("| Cores | Query length | Untiled GCUPS (IQR) | Tiled GCUPS (IQR) | Tiled / untiled |")
    md("|---|---|---|---|---|")
    fig, axes = plt.subplots(1, len(cores_list), figsize=(8, 2.9), sharey=True)
    if len(cores_list) == 1:
        axes = [axes]
    w = 0.38
    for ax, c in zip(axes, cores_list):
        for L in lengths:
            u, t = quart(g[(c, "simd_untiled", L)]), quart(g[(c, "simd", L)])
            md(f"| {CORE_NAMES[c]} | {L} | {u[1]:.2f} ({u[0]:.2f}–{u[2]:.2f}) | {t[1]:.2f} ({t[0]:.2f}–{t[2]:.2f}) | {t[1] / u[1]:.2f}× |")
        x = range(len(lengths))
        for off, kern, col, lab in ((-w / 2, "simd_untiled", S3, "untiled (original)"), (w / 2, "simd", S1, "tiled (default)")):
            q = [quart(g[(c, kern, L)]) for L in lengths]
            ax.bar([i + off for i in x], [v[1] for v in q], width=w, color=col, edgecolor=SURFACE, linewidth=2, label=lab)
            ax.errorbar([i + off for i in x], [v[1] for v in q], yerr=[[v[1] - v[0] for v in q], [v[2] - v[1] for v in q]],
                        fmt="none", ecolor=INK2, elinewidth=1, capsize=3)
        ax.set_xticks(list(x))
        ax.set_xticklabels([f"L={L}" for L in lengths])
        ax.grid(axis="x", visible=False)
        ax.set_title(CORE_NAMES[c], fontsize=9)
    axes[0].set_ylabel("GCUPS (median; whiskers = IQR)")
    axes[0].legend(fontsize=8, loc="upper left")
    md()
    save(fig, "fig_e7_tiling.png")


def e6():
    rows = read_csv("e6_parasail.csv")
    if not rows:
        return
    g = defaultdict(list)
    for r in rows:
        g[(r["config"].split(":")[0], int(r["query_length"]))].append(float(r["seconds"]))
    tot = per_config(rows)
    lengths = sorted({k[1] for k in g})
    md("## E6 — Same-machine yardstick: Parasail vs HySW, one thread on P-core cpu0 (Swiss-Prot)")
    md(sysinfo_line("e6_parasail.sysinfo.txt"))
    md()
    md("| Query length | HySW median (s) | Parasail median (s) | Parasail / HySW |")
    md("|---|---|---|---|")
    for L in lengths:
        h, p = statistics.median(g[("hysw", L)]), statistics.median(g[("parasail", L)])
        md(f"| {L} | {h:.2f} | {p:.2f} | {p / h:.2f} |")
    th = [v for k, v in tot.items() if k.startswith("hysw")][0]
    tp = [v for k, v in tot.items() if k.startswith("parasail")][0]
    md(f"| all 3 | {th['median']:.2f} ({th['gcups']:.2f} GCUPS) | {tp['median']:.2f} ({tp['gcups']:.2f} GCUPS) | "
       f"{tp['median'] / th['median']:.2f} |")
    md("\nParasail: `sw_striped_profile_16` (hand-written intrinsics, best instruction set auto-selected) called once per "
       "database sequence from Python, so its time includes Python call overhead (~576k calls per query). "
       f"Rounds: {th['n']}. Conditioning differs slightly: Parasail gets the 2 s load before each query, HySW once "
       "before its 3-query process. With 3 rounds, treat differences under ~10% as ties.\n")
    fig, ax = plt.subplots(figsize=(6.5, 2.8))
    w = 0.38
    x = range(len(lengths))
    for off, tool, col, lab in ((-w / 2, "hysw", S1, "HySW (plain C + omp simd)"), (w / 2, "parasail", S2, "Parasail (intrinsics, via Python)")):
        vals = [statistics.median(g[(tool, L)]) for L in lengths]
        ax.bar([i + off for i in x], vals, width=w, color=col, edgecolor=SURFACE, linewidth=2, label=lab)
    ax.set_xticks(list(x))
    ax.set_xticklabels([f"query length {L}" for L in lengths])
    ax.grid(axis="x", visible=False)
    ax.set_ylabel("seconds (median, lower is better)")
    ax.legend(fontsize=8)
    ax.set_title("One thread, same P-core, all of Swiss-Prot")
    save(fig, "fig_e6_parasail.png")


def main():
    md("# M6 results — generated by `scripts/make_m6_report.py` from the raw files in this folder")
    md()
    md("All numbers are measured on the student's laptop (Intel i3-1220P, 2 P-cores + 8 E-cores). "
       "Medians over rounds; each round runs the configurations in a fresh random order, each measured pass preceded by "
       "the same 2 s all-core conditioning load (sustained regime) unless stated otherwise. Raw CSV, random orders "
       "(`*.meta.json`) and machine state (`*.sysinfo.txt`) are in this folder.")
    md()
    md("Engine: tiled SIMD kernel and query-only profile (the defaults, `DECISIONS.md` D3–D4); E5 and E7 also run the "
       "original untiled kernel for comparison. The first study, run with the untiled kernel, is kept in "
       "`results/m6_v1_untiled/`, together with the cache diagnosis that motivated the change.")
    md()
    for f in (e1, e2, e3, e4, e5, e6, e7):
        f()
    open(os.path.join(D, "SUMMARY.md"), "w").write("\n".join(out_md) + "\n")
    print("wrote", os.path.join(D, "SUMMARY.md"))


if __name__ == "__main__":
    main()
