# Phase 2 Blueprint — HybridFor

> **Superseded by `PHASE_2_BLUEPRINT_V2.md` (Phase 1B, 2026-11-11).** Kept unchanged as the Phase 1 record.

Baseline for implementation. Do not start until the student explicitly initiates Phase 2. Record any deviation in `DECISIONS.md`.

## 1. Programming model and toolchain

- **Primary model: OpenMP (shared-memory threads) in C11, GCC 15, `-O2 -march=native -fopenmp`.** OpenMP supplies the thread team, barriers and locks; HybridFor replaces only the iteration-distribution step. No second parallel model.
- **Build:** a plain `build.sh` (no `make`/`cmake` on the machine). Binaries go to `build/`.
- **Analysis:** Python 3.14 in a project-local venv with numpy + matplotlib (wheels confirmed to exist; install not yet tested). If a venv on NTFS misbehaves, try `python3 -m venv --copies .venv`; if that fails, place the venv under `~/.venvs/pagp` and document it.
- **Platform:** Linux, x86-64. Topology read from sysfs.

## 2. Architecture

```
application loop body  ──►  hf_for(site, n, body, ctx)
                               │
        ┌──────────────────────┼─────────────────────────┐
        ▼                      ▼                         ▼
   topology.c             partition.c                steal.c
 core types, pin      weights → contiguous       per-thread range,
 threads to CPUs      blocks; feedback update    take-front / steal-back
        └──────────────────────┬─────────────────────────┘
                               ▼
                            trace.c
                 per-thread busy/idle intervals, steals
```

### Module plan

| File | Responsibility |
|---|---|
| `include/hybridfor.h` | Public API (below) |
| `src/topology.c` | Read `/sys/devices/cpu_core/cpus` and `cpu_atom/cpus` (fallback: per-CPU `cpuinfo_max_freq`; final fallback: treat all cores as equal). Build the CPU list for a named thread set (`all`, `physical`, `p_only`, `e_only`). Pin each OpenMP thread once |
| `src/partition.c` | Turn a weight vector into contiguous `[lo,hi)` blocks; update weights from the last execution (exponential smoothing); cold-start rule |
| `src/steal.c` | Owner takes chunks from the front of its range; an idle thread locks the fullest victim and takes the back half |
| `src/hybridfor.c` | `hf_for`: setup → work/steal loop → feedback. Callable inside an existing parallel region (orphaned) or standalone |
| `src/trace.c` | Optional per-thread interval log to CSV for the timeline plot; compiled out when disabled |
| `bench/kernels/*.c` | One file per kernel, each exposing `init`, `body(lo,hi,ctx)`, `checksum` |
| `bench/harness.c` | Runs {kernel × schedule × thread set} with interleaved rotation; writes raw CSV |
| `tests/*.c` | Correctness and stress tests |
| `scripts/analyse.py`, `scripts/plot.py` | Aggregate CSV → tables and figures |
| `scripts/sysinfo.sh` | Capture CPU, kernel, compiler, governor, temperatures alongside every result set |

### Public API (sketch)

```c
typedef void (*hf_body)(long lo, long hi, void *ctx);   // run iterations [lo, hi)
typedef struct hf_site hf_site;                         // per-loop memory: weights, stats

int      hf_init(const char *thread_set);               // "all" | "physical" | "p_only" | "e_only" | "0,2,4-11"
hf_site *hf_site_create(const char *name);
void     hf_for(hf_site *site, long n, hf_body body, void *ctx);
void     hf_site_stats(const hf_site *site, FILE *out); // weights, steals, per-thread idle
void     hf_set_mode(int mode);                         // ablation switch, see §4
```

The body receives a **range**, not a single index, so the inner loop stays inlinable and vectorisable. Baselines call the same body through `#pragma omp for schedule(runtime)` over fixed-size sub-ranges so that the comparison differs only in scheduling.

## 3. Algorithm

Per execution of a loop site with `n` iterations and `T` threads:

1. **Partition.** Block size of thread `t` = `n × w[t]`, contiguous, in thread order. First execution: `w[t] = 1/T`.
2. **Work.** Each thread repeatedly takes a chunk from the *front* of its own range under its own lock. Chunk = a fraction of what remains (start with 1/8, minimum grain configurable) so the lock is taken O(log n) times.
3. **Steal.** A thread whose range is empty scans the others (unlocked peek), locks the fullest, removes the *back half*, installs it as its own range, and continues. No victim → done.
4. **Feedback.** After the barrier, `w[t] ← (1−α)·w[t] + α·share[t]`, renormalised, where `share[t]` is the speed measured for thread `t` in this execution. α = 0.5 initially.

### Design options to settle by measurement (not by assumption)

| Option | Choices | How it is decided |
|---|---|---|
| What `share[t]` measures | (a) fraction of iterations the thread executed — used in the Phase 1 spike; mixes core speed with iteration cost on irregular loops. (b) iterations per unit busy time on the thread's *own* block only | Compare on Mandelbrot and Jacobi; prefer (b) if it converges to stable capacity-like weights |
| Owner chunk rule | fixed fraction 1/8; fixed grain; guided-style | Overhead on small-sweep Jacobi |
| Range protection | per-thread OpenMP lock (start here); packed 64-bit compare-and-swap | Only if lock cost shows up in measurements |
| Victim choice | fullest; nearest neighbour (same core cluster first) | Steal counts and locality on Jacobi |
| Cold start | equal weights; seed weights by core type from a previous site | First-execution time |

## 4. Schedules compared

| Label | What it is |
|---|---|
| `seq` | Same body, one thread pinned to a P-core. Reference for correctness and speedup |
| `static` | `schedule(static)` — the GCC default |
| `dynamic,1`, `dynamic,c` | `schedule(dynamic)` with grain 1 and with a moderate chunk; `c` fixed by rule (`n/(8T)`), stated, not tuned per kernel |
| `guided` | `schedule(guided)` |
| `hf-steal` | HybridFor, equal blocks + stealing (ablation: no weights) |
| `hf-weights` | HybridFor, weighted blocks, no stealing (ablation) |
| `hf-full` | Weighted + stealing + feedback |
| *optional* `llvm-static_steal` | Only if an LLVM/Intel OpenMP runtime can be obtained without system changes (e.g. the `intel-openmp` PyPI wheel — **untested idea**) or the student installs `libomp-dev` |

## 5. Benchmark kernels

| Kernel | Character | Why it is included |
|---|---|---|
| Uniform compute | equal-cost, compute-bound | Isolates core asymmetry |
| Mandelbrot rows | irregular cost, compute-bound | Isolates load imbalance from the work itself; visual for the demo |
| Jacobi 512² and 1024², many sweeps | repeated, cache-sensitive | Where `dynamic` fails |
| Jacobi 3000² or triad | memory-bound | Expected negative result; shows the bandwidth limit |
| k-means assignment step (iterated) | application-style, repeated, vectorisable | Realistic iterative workload for the case study; data can be synthetic or a small public dataset (availability **not yet checked**; synthetic is the fallback) |
| *optional* SpMV on one public sparse matrix | irregular + memory-heavy | Only if time permits and the matrix licence/size check passes |

## 6. Correctness plan (done before any timing)

1. **Exactly-once test:** body increments `count[i]`; assert every entry is 1. Sweep n ∈ {0, 1, T−1, T, T+1, primes, 10⁶}, every thread set, random weight vectors (including zeros and extreme skew), 1000+ repetitions.
2. **Output equivalence:** every schedule's output equals `seq` exactly for kernels with independent per-iteration outputs (Mandelbrot, Jacobi, k-means labels).
3. **Reductions:** per-thread partial results combined in a fixed order; floating-point sums compared with a stated tolerance and the reason documented.
4. **Edge cases:** n < T, one thread, repeated calls with changing n at the same site, a thread set that excludes a core type.
5. **Error handling:** invalid thread set string, affinity failure, non-hybrid machine (must degrade to equal weights and still be correct).
6. **Tooling:** `-Wall -Wextra`, `-fsanitize=address,undefined` builds of the tests. ThreadSanitizer only if it gives usable output with libgomp; otherwise rely on the stress tests and say so.

## 7. Benchmark methodology

- **Pinning:** thread *i* → fixed CPU for the whole run. Thread sets: all 12, one-per-physical-core (10), P-only (4), E-only (8).
- **Interleaving (mandatory, from Phase 1 finding):** each round runs every schedule once; starting schedule rotates per round; ≥ 9 rounds.
- **Two power regimes, reported separately:**
  - *burst* — short measurements separated by cool-down until the package temperature returns to a set threshold;
  - *sustained* — after a warm-up long enough to reach the throttled plateau.
  Each sample is tagged with its regime, the temperature and `scaling_cur_freq` read just before and after (both readable without root).
- **Statistics:** median and inter-quartile range per (kernel, schedule, thread set, regime); raw samples kept. Differences smaller than the measured spread are reported as ties.
- **Definitions:**
  - speedup = `seq` time on a P-core ÷ parallel time, same kernel, same regime;
  - regret = time ÷ best time among all schedules for that kernel and regime − 1;
  - idle fraction = 1 − (thread busy time ÷ loop wall time), from the trace;
  - efficiency is reported only as *capacity-normalised* efficiency, with the per-kernel capacity of each thread measured, and its definition stated next to every use.
- **Costs included:** first-execution (cold weights) time is reported separately from steady state; library setup is inside the timed region.
- **Environment hygiene:** AC power, no other heavy applications, system state captured by `sysinfo.sh` with each result set, exact command lines stored in the CSV header.
- **Negative results** (memory-bound kernels, any kernel where HybridFor loses) appear in the main results table, not an appendix.

## 8. Milestones (each ends with something runnable)

| # | Milestone | Done when |
|---|---|---|
| M1 | Repository skeleton, `build.sh`, `sysinfo.sh`, venv, `README.md` stub, `DECISIONS.md` | `./build.sh` builds a hello kernel; venv imports matplotlib |
| M2 | Kernels + `seq` reference + stock-schedule wrappers + checksum | All stock schedules match `seq` on every kernel |
| M3 | HybridFor core (`hf-steal` first, then weights, then feedback) | Exactly-once and equivalence tests pass in all three modes |
| M4 | Harness with interleaving, regimes, CSV output; trace | One full pilot run produces raw CSV and a timeline |
| M5 | Full experiments + ablation + thread-set study; analysis and plots | Tables and figures regenerate from raw CSV with one command |
| M6 | Evidence-driven tuning only (options in §3); rerun affected experiments | Each change justified by a before/after measurement in `DECISIONS.md` |
| M7 | Report, case study, slides, demo/video script; clean-clone setup check | A fresh copy builds, tests and reproduces a small benchmark from the README |

## 9. Final submission contents

- Source: library, kernels, harness, tests, scripts.
- `README.md`: prerequisites, build, test, benchmark, plot instructions.
- Raw and aggregated results with system information; figures generated from them:
  per-thread timeline (static vs dynamic vs HybridFor), speedup bars per kernel, regret heat-map, ablation chart, P/E ratio by workload, throttling/run-order demonstration.
- Project report with citations (problem, background, design, method, results including negatives, limitations).
- Case study: "Why your OpenMP loop under-performs on a hybrid laptop" — the measured story from asymmetry to fix, mapped to syllabus units.
- Slide content (and a PPTX if tooling permits).
- Video storyboard, screen-recording sequence and narration script. The recording itself is made by the student.

## 10. Fallbacks

- HybridFor shows no advantage → deliver the comparative study, the measurement method and a schedule-selection guideline; report the result as negative.
- Weights useless → ship static+steal for GCC; state so.
- Noise too high for small effects → restrict claims to effects larger than the measured spread; lengthen runs.
- Non-hybrid machine at demo time → library falls back to equal weights; demo uses recorded traces from the hybrid laptop.
