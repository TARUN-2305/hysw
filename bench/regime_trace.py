"""Burst -> sustained transition: after an idle cool-down, repeat the same search continuously and
record each pass's speed together with CPU frequencies and package temperature (sampled from sysfs).
Shows directly how the 15 W long-term power limit (28 s window) lowers speed after the first seconds.
Usage: .venv/bin/python -I bench/regime_trace.py --db DB.bin --query Q.fasta --out results/m6/regime \
           [--configs 1:30,physical:80,all:80] [--cooldown 60]
Writes <out>_passes.csv (one row per search pass) and <out>_samples.csv (sysfs samples every 0.25 s)."""
import argparse
import csv
import glob
import json
import os
import random
import subprocess
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(ROOT, "bin", "hysw_engine")


def read(path):
    try:
        return open(path).read().strip()
    except OSError:
        return ""


def pkg_temp_path():
    for z in glob.glob("/sys/class/thermal/thermal_zone*"):
        if read(z + "/type") == "x86_pkg_temp":
            return z + "/temp"
    return None


def sampler(stop, rows, label, t0, cpus, temp_path):
    while not stop.is_set():
        t = time.monotonic() - t0
        row = {"config": label, "t": round(t, 3), "pkg_temp_c": int(read(temp_path) or 0) / 1000 if temp_path else ""}
        for c in cpus:
            row[f"mhz_cpu{c}"] = int(read(f"/sys/devices/system/cpu/cpu{c}/cpufreq/scaling_cur_freq") or 0) // 1000
        rows.append(row)
        stop.wait(0.25)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--query", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--configs", default="1:30,physical:80,all:80", help="cores:repeats,...")
    ap.add_argument("--cooldown", type=float, default=60)
    ap.add_argument("--seed", type=int, default=20261121)
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    cpus = sorted(int(p.rsplit("cpu", 1)[1]) for p in glob.glob("/sys/devices/system/cpu/cpu[0-9]*"))
    temp_path = pkg_temp_path()
    configs = [c.split(":") for c in a.configs.split(",")]
    random.Random(a.seed).shuffle(configs)
    passes, samples = [], []
    with open(a.out + ".sysinfo.txt", "w") as f:
        f.write(subprocess.run([os.path.join(ROOT, "scripts", "sysinfo.sh")], capture_output=True, text=True).stdout)
    for cores, reps in configs:
        print(f"cool-down {a.cooldown:.0f} s, then {reps} passes on cores={cores}", flush=True)
        time.sleep(a.cooldown)
        stop = threading.Event()
        t0 = time.monotonic()
        th = threading.Thread(target=sampler, args=(stop, samples, cores, t0, cpus, temp_path))
        th.start()
        p = subprocess.run([ENGINE, "search", "--db", a.db, "--query", a.query, "--cores", cores, "--repeat", reps,
                            "--top-k", "1", "--align-k", "0"], capture_output=True, text=True)
        stop.set()
        th.join()
        res = json.loads(p.stdout)
        q = res["queries"][0]
        start = res["database"]["load_seconds"]          # passes start right after loading (process start-up ignored)
        for k, secs in enumerate(q["timing"]["runs"]):
            passes.append({"config": cores, "threads": res["settings"]["threads"], "pass": k, "t_start": round(start, 4),
                           "seconds": secs, "gcups": q["timing"]["cells"] / secs / 1e9})
            start += secs
        print(f"  first pass {passes[-len(q['timing']['runs'])]['gcups']:.1f} GCUPS, last {passes[-1]['gcups']:.1f} GCUPS", flush=True)
    for name, rows in (("_passes.csv", passes), ("_samples.csv", samples)):
        with open(a.out + name, "w", newline="") as f:
            keys = list(rows[0].keys()) if name == "_passes.csv" else ["config", "t", "pkg_temp_c"] + [f"mhz_cpu{c}" for c in cpus]
            w = csv.DictWriter(f, fieldnames=keys)
            w.writeheader()
            w.writerows(rows)
    print("wrote", a.out + "_passes.csv", a.out + "_samples.csv")


if __name__ == "__main__":
    main()
