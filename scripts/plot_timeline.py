"""Per-thread timeline (busy intervals) from engine runs made with --trace, one panel per run.
Shows how a mapping policy spreads length-sorted batches over P- and E-cores and where cores idle.
Usage: .venv/bin/python -I scripts/plot_timeline.py out.png run1.json [run2.json ...]"""
import json
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

out, files = sys.argv[1], sys.argv[2:]
fig, axes = plt.subplots(len(files), 1, figsize=(9, 1.2 + 0.32 * 12 * len(files)), squeeze=False, sharex=True)
for ax, path in zip(axes[:, 0], files):
    d = json.load(open(path))
    q = d["queries"][0]
    th = q["threads"]
    for row, t in enumerate(th):
        color = "#3b6fb6" if t["type"] == "P" else "#d08a2e"
        ax.broken_barh([(a, b - a) for a, b in t["busy"]], (row - 0.4, 0.8), facecolors=color, linewidth=0)
    end = q["timing"]["seconds"]
    ax.axvline(end, color="black", lw=0.8, ls="--")
    ax.set_yticks(range(len(th)))
    ax.set_yticklabels([f"cpu{t['cpu']} ({t['type']})" for t in th], fontsize=7)
    ax.invert_yaxis()
    ax.set_title(f"policy {d['settings']['policy']}, {d['settings']['threads']} threads, query length {q['query']['length']}: "
                 f"{end:.3f} s", fontsize=9, loc="left")
axes[-1, 0].set_xlabel("seconds since the start of the score pass (blue = P-core thread, orange = E-core)")
fig.tight_layout()
fig.savefig(out, dpi=130)
print("wrote", out)
