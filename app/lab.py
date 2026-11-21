"""Performance Lab back end: stored benchmark results (read from the raw files) and live engine runs.

Stored results come straight from results/ (the same raw CSVs the reports are generated from), with the date,
power limit and settings of each experiment. Live runs are labelled as single runs on this machine, not benchmarks.

  stored_results()   E1 kernels, E2 thread sets (sustained), E8b thread sets (burst), E4 burst -> sustained,
                     E3 mapping policies, Parasail verification records
  LabTask            several engine runs one after another, with progress relayed like a search (race, schedules)
  simd_trace()       H values of one real batch of 16 sequences (engine `simdtrace`)
"""
import csv
import json
import os
import signal
import statistics
import subprocess
import tempfile
import threading
import time
import uuid
from collections import defaultdict

import engine as eng

ROOT = eng.ROOT
SET_LABEL = {"1": "1 thread (P-core)", "p1": "2 P-cores, 1 thread each", "p": "2 P-cores + hyperthreads (4)",
             "e": "8 E-cores", "physical": "1 per physical core (10)", "all": "all logical CPUs (12)"}


def _rows(path):
    p = os.path.join(ROOT, path)
    return list(csv.DictReader(open(p))) if os.path.exists(p) else None


def _sysinfo(path):
    p = os.path.join(ROOT, path)
    if not os.path.exists(p):
        return {}
    kv = dict(l.split(": ", 1) for l in open(p).read().splitlines() if ": " in l)
    return {"date": kv.get("date"), "power_limit_w": kv.get("power_limit_intel-rapl-mmio:0_long_term_w"),
            "window_s": kv.get("power_limit_intel-rapl-mmio:0_long_term_window_s"), "on_ac": kv.get("on_ac_power"),
            "platform_profile": kv.get("platform_profile")}


def _per_config(rows):
    """config -> median run time (sum of the run's queries per round), IQR and GCUPS (as scripts/make_m6_report.py)."""
    t, cells, meta = defaultdict(lambda: defaultdict(float)), defaultdict(lambda: defaultdict(float)), {}
    for r in rows:
        t[r["config"]][r["round"]] += float(r["seconds"])
        cells[r["config"]][r["round"]] += float(r["seconds"]) * float(r["gcups"]) * 1e9
        meta[r["config"]] = {"kernel": r["kernel"], "cores": r["cores"], "threads": int(r["threads"]), "policy": r["policy"]}
    out = {}
    for c, per_round in t.items():
        v = sorted(per_round.values())
        q = statistics.quantiles(v, n=4, method="inclusive") if len(v) >= 4 else [v[0], statistics.median(v), v[-1]]
        tc = next(iter(cells[c].values()))
        out[c] = {**meta[c], "median_s": q[1], "q1_s": q[0], "q3_s": q[2], "rounds": len(v), "gcups": tc / q[1] / 1e9,
                  "gcups_lo": tc / q[2] / 1e9, "gcups_hi": tc / q[0] / 1e9}
    return out


def stored_results():
    out = {}
    rows = _rows("results/m6/e1_kernels.csv")
    if rows:
        pc = _per_config(rows)
        order = ["scalar:1:dynamic,1", "simd:1:dynamic,1", "simd:physical:dynamic,1"]
        out["e1"] = {"title": "Speedup decomposition", "workload": "Swiss-Prot, query length 400, sustained regime",
                     "sysinfo": _sysinfo("results/m6/e1_kernels.sysinfo.txt"),
                     "rows": [{"label": lbl, **pc[c]} for lbl, c in zip(("Scalar, 1 thread", "SIMD, 1 thread", "SIMD + 10 threads"), order) if c in pc]}
    for key, path, sysp, workload in (
            ("e2", "results/m6/e2_threadsets.csv", "results/m6/e2_threadsets.sysinfo.txt",
             "Swiss-Prot, 3 queries (150/400/990), sustained: 2 s all-core load before every run"),
            ("e8b", "results/calibration/e8b_burst_sets.csv", "results/calibration/e8b_burst_sets.sysinfo.txt",
             "Swiss-Prot, query length 400, burst: 20 s idle before every run")):
        rows = _rows(path)
        if rows:
            pc = _per_config(rows)
            out[key] = {"workload": workload, "sysinfo": _sysinfo(sysp),
                        "rows": sorted(({"label": SET_LABEL.get(v["cores"], v["cores"]), **v} for v in pc.values()),
                                       key=lambda r: -r["gcups"])}
    rows = _rows("results/m6/e4_regime_passes.csv")
    if rows:
        series = defaultdict(list)
        for r in rows:
            series[r["config"]].append([round(float(r["t_start"]), 3), round(float(r["gcups"]), 3)])
        out["e4"] = {"workload": "Swiss-Prot, query length 150, repeated searches after a 60 s idle cool-down",
                     "sysinfo": _sysinfo("results/m6/e4_regime.sysinfo.txt"),
                     "series": [{"label": SET_LABEL.get(c, c), "cores": c, "points": pts} for c, pts in series.items()]}
    rows = _rows("results/m6/e3_policies.csv")
    if rows:
        pc = _per_config(rows)
        best = min(v["median_s"] for v in pc.values())
        out["e3"] = {"workload": "Swiss-Prot, 3 queries, one thread per physical core, sustained",
                     "sysinfo": _sysinfo("results/m6/e3_policies.sysinfo.txt"),
                     "rows": sorted(({"label": v["policy"], "regret": v["median_s"] / best - 1, **v} for v in pc.values()),
                                    key=lambda r: r["median_s"])}
    rows = _rows("results/m6/e6_parasail.csv")
    if rows:
        g = defaultdict(list)
        for r in rows:
            g[(r["config"].split(":")[0], int(r["query_length"]))].append(float(r["seconds"]))
        lengths = sorted({L for _, L in g})
        per = [{"length": L, "hysw_s": statistics.median(g[("hysw", L)]), "parasail_s": statistics.median(g[("parasail", L)])}
               for L in lengths]
        out["e6"] = {"workload": "Swiss-Prot, one thread on P-core cpu0, queries 150/400/990, 3 rounds",
                     "sysinfo": _sysinfo("results/m6/e6_parasail.sysinfo.txt"), "per_length": per,
                     "hysw_total_s": sum(p["hysw_s"] for p in per), "parasail_total_s": sum(p["parasail_s"] for p in per),
                     "note": "Parasail sw_striped_profile_16 (hand-written intrinsics) called once per database sequence "
                             "from Python, so its time includes Python call overhead. With 3 rounds, differences under "
                             "about 10% are ties."}
    ver = []
    for name in ("parasail_oracle.json", "parasail_oracle_swissprot.json"):
        p = os.path.join(ROOT, "results", "verification", name)
        if os.path.exists(p):
            d = json.load(open(p))
            ver.append({k: d.get(k) for k in ("date", "database", "pairs", "identical", "pass", "parasail_version")})
    out["verification"] = ver
    return out


# ---------------------------------------------------------------- live runs
RACE = [("Scalar, 1 thread", ["--mode", "scalar"]), ("SIMD, 1 thread", ["--mode", "simd"]),
        ("SIMD + threads", ["--mode", "auto", "--cores", "physical"])]
SCHEDULES = [("static", ["--mode", "auto", "--cores", "physical", "--policy", "static", "--trace"]),
             ("dynamic,1", ["--mode", "auto", "--cores", "physical", "--policy", "dynamic,1", "--trace"])]


def _on_ac():
    """True on mains power, False on battery, None when the system does not say (sysfs power_supply)."""
    base = "/sys/class/power_supply"
    try:
        for d in os.listdir(base):
            if open(os.path.join(base, d, "type")).read().strip() == "Mains":
                return open(os.path.join(base, d, "online")).read().strip() == "1"
    except OSError:
        pass
    return None


def _merge(intervals, gap=0.002):
    out = []
    for a, b in intervals:
        if out and a - out[-1][1] <= gap:
            out[-1][1] = max(out[-1][1], b)
        else:
            out.append([a, b])
    return [[round(a, 5), round(b, 5)] for a, b in out]


class LabTask:
    """Engine runs one after another. Same interface as engine.Job (state, events, cond, run, cancel, summary)."""

    def __init__(self, kind, steps, db_path, query_header, query_seq, workdir):
        self.id = uuid.uuid4().hex[:12]
        self.kind, self.steps, self.db_path = kind, steps, db_path
        self.header, self.seq = query_header, query_seq
        self.state, self.cancel_requested = "running", False
        self.events, self.results = [], []
        self.error = None
        self.proc = None
        self.cond = threading.Condition()
        self.dir = os.path.join(workdir, self.id)
        os.makedirs(self.dir)
        self.query_path = os.path.join(self.dir, "query.fasta")
        with open(self.query_path, "w") as f:
            f.write(f">{query_header}\n{query_seq}\n")
        self.params = {"kind": kind}
        self.warnings = []

    def _push(self, ev):
        with self.cond:
            self.events.append(ev)
            self.cond.notify_all()

    def run(self, on_finish):
        self.on_ac = _on_ac()
        try:
            for si, (label, extra) in enumerate(self.steps):
                if self.cancel_requested:
                    break
                argv = [eng.ENGINE, "search", "--db", self.db_path, "--query", self.query_path, "--top-k", "1",
                        "--align-k", "0", "--progress", *extra]
                self.proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                             start_new_session=True)
                if self.cancel_requested:
                    os.killpg(self.proc.pid, signal.SIGTERM)
                out = []
                reader = threading.Thread(target=lambda: out.append(self.proc.stdout.read()), daemon=True)
                reader.start()
                self._push({"type": "step", "step": si, "label": label})
                for line in self.proc.stderr:
                    if line.startswith("{"):
                        try:
                            ev = json.loads(line)
                        except json.JSONDecodeError:
                            continue
                        if ev.get("type") == "progress":
                            self._push({"type": "progress", "step": si, "done": ev["done"], "total": ev["total"],
                                        "elapsed": ev["elapsed"], "thread_items": ev["thread_items"]})
                rc = self.proc.wait()
                reader.join()
                if self.cancel_requested:
                    break
                if rc != 0:
                    raise RuntimeError(f"engine failed in step '{label}' (status {rc})")
                r = json.loads("".join(out))
                q = r["queries"][0]
                step = {"label": label, "kernel": r["settings"]["kernel"], "threads": r["settings"]["threads"],
                        "cpus": r["settings"]["cpus"], "policy": r["settings"]["policy"], "timing": q["timing"],
                        "thread_stats": [{"cpu": t["cpu"], "type": t["type"], "items": t["items"], "busy_s": t["busy_s"],
                                          **({"busy": _merge(t["busy"])} if "busy" in t else {})} for t in q["threads"]],
                        "top_hit": {k: q["hits"][0][k] for k in ("accession", "name", "score")} if q["hits"] else None}
                self.results.append(step)
                self._push({"type": "step_done", "step": si, "result": step})
            with self.cond:
                self.state = "cancelled" if self.cancel_requested else "done"
        except Exception as e:
            with self.cond:
                self.state, self.error = "error", str(e)
        finally:
            with self.cond:
                self.cond.notify_all()
            on_finish(self)

    def cancel(self):
        with self.cond:
            if self.state != "running":
                return False
            self.cancel_requested = True
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        return True

    def summary(self):
        return {"id": self.id, "kind": self.kind, "state": self.state, "error": self.error, "on_ac": getattr(self, "on_ac", None),
                "query": {"header": self.header, "length": len(self.seq)},
                "steps": [s[0] for s in self.steps], "results": self.results}


def simd_trace(db_path, accession, header, seq, rows=24, cols=48):
    with tempfile.TemporaryDirectory(prefix="hysw_trace_") as d:
        qp = os.path.join(d, "q.fasta")
        with open(qp, "w") as f:
            f.write(f">{header}\n{seq}\n")
        return eng.run_engine("simdtrace", "--db", db_path, "--query", qp, "--accession", accession,
                              "--rows", str(rows), "--cols", str(cols), timeout=60)
