# Feasibility Log — Phase 1

Session date: 2026-11-10 / 11. Everything below was **actually executed** on the student's laptop unless marked otherwise.
All spike results are **exploratory**: one machine, one session, few repetitions. They justify the project choice; they are not final benchmark results.

Spike sources: `research/spikes/*.c`. Raw outputs: `research/spikes/results/*.txt`.

---

## 1. Environment inspection (measured)

| Item | Finding | Command |
|---|---|---|
| CPU | Intel Core i3-1220P, 12 logical CPUs, 1 socket, 1 NUMA node | `lscpu` |
| Core types | **Hybrid.** P-cores: logical CPUs 0–3 (2 physical cores × 2 hyperthreads, max 4.4 GHz). E-cores: CPUs 4–11 (8 cores, no SMT, max 3.3 GHz) | `/sys/devices/cpu_core/cpus`, `/sys/devices/cpu_atom/cpus`, `lscpu -e` |
| Caches | P-core: L1d 48K, L2 1.25 MB per core. E-core: L1d 32K, L2 2 MB **shared by 4 E-cores**. L3 12 MB shared by all | `/sys/devices/system/cpu/cpu*/cache` |
| SIMD | AVX2, FMA, SSE4.2. No AVX-512 | `/proc/cpuinfo` |
| Kernel-reported capacity | `cpu_capacity` = 1024 for **both** a P-core and an E-core (the OS does not expose the asymmetry through this file on this machine) | `/sys/devices/system/cpu/cpu{0,4}/cpu_capacity` |
| RAM | 7.0 GiB total, ~3 GiB available, 4 GiB swap | `free -h` |
| GPU | Intel UHD (Alder Lake GT1) integrated only. **No NVIDIA GPU, no `nvidia-smi`, no CUDA toolkit** | `lspci`, `nvidia-smi` |
| OpenCL | `libOpenCL.so.1` loader present but no ICD (`/etc/OpenCL/vendors` missing) → no usable OpenCL device | `ls` |
| OS | Ubuntu 26.04.1 LTS, Linux 7.0.0-43 | `/etc/os-release` |
| Compilers | GCC/G++/gfortran 15.2.0, Clang 21.1.8. GCC OpenMP works (`_OPENMP=201511`, libgomp present). Clang has **no** `libomp` installed | `gcc --version`, `gcc -fopenmp -dM -E -` |
| Missing tools | `make`, `cmake`, `pip`, MPI (`mpicc`/`mpirun`), `nvcc`, `nvc++` (OpenACC), conda, hwloc | `command -v` |
| Python | 3.14.4; `venv` and `ensurepip` available; no numpy/matplotlib/numba installed | `python3 -m venv --help` |
| PyPI wheels (metadata only, nothing installed) | numpy 2.5.4, matplotlib 3.11.2, numba 0.68.1 have cp314 manylinux x86_64 wheels; `mpich` 5.0.2 and `mpi4py` 4.1.2 wheels exist | `curl https://pypi.org/pypi/<pkg>/json` |
| Privileges | No passwordless sudo. `perf_event_paranoid=4` → **no hardware counters**. RAPL `energy_uj` → **permission denied** (no energy measurement) | `sudo -n true`, `cat` |
| Thread pinning | `sched_setaffinity` / `taskset` work without root | spikes |
| Workspace filesystem | Project is on an **NTFS** partition (`ntfs3`, no `noexec`). Compiling and running an OpenMP binary from the project directory works | `findmnt -T .`, hello-OpenMP test |
| Power state | AC power, governor `powersave` (intel_pstate/HWP), turbo enabled | sysfs |

**Consequences.** CUDA and OpenACC-on-GPU cannot be run locally. MPI is absent but installable in a venv. OpenMP with GCC works today with zero installs. The laptop has exactly the asymmetric-multicore hardware that Candidate 1 studies.

---

## 2. Spike 1 — single-core P vs E throughput by workload type

- **Question:** how much faster is a P-core than an E-core, and does it depend on the workload?
- **Code:** `spikes/spike1_core_ratio.c`. Build `gcc -O2 -march=native spike1_core_ratio.c -o spike1 -lm`. Run `./spike1 0` (P-core), `./spike1 4` (E-core). Process pinned to one CPU; 5 repetitions, minimum reported.
- **Result (measured; two runs agreed to <1%):**

| Kernel | P-core (cpu0) | E-core (cpu4) | P/E speed ratio (derived) |
|---|---|---|---|
| Mandelbrot, scalar, branchy | 0.0644 s | 0.1010 s | **1.57×** |
| AVX2-vectorisable loop, data in L1 | 0.0404 s | 0.1126 s | **2.79×** |
| STREAM-like triad, 384 MB (memory-bound) | 0.2685 s | 0.2995 s | **1.12×** |
| Random gather (latency-bound) | 0.2769 s | 0.6133 s | **2.21×** |

- **Interpretation:** the asymmetry is real and **workload-dependent (1.1× to 2.8×)**. A single fixed "capacity" number per core type would be wrong for most loops; a scheduler has to measure the ratio on the actual loop. Why the SIMD ratio is largest is *hypothesised* (narrower vector datapath on E-cores), not verified.

---

## 3. Spike 2 — stock OpenMP schedules on all 12 CPUs, single-shot loops

- **Question:** does `schedule(static)` waste the fast cores? Do `dynamic`/`guided` fix it? Does a capacity-weighted static split (weights from a 2% online sample) help?
- **Code:** `spikes/spike2_sched.c`. `gcc -O2 -march=native -fopenmp spike2_sched.c -o spike2 -lm`; `./spike2 <kernel> 0,1,...,11`. Threads pinned 1:1 to CPUs. 7 repetitions, median. Sequential reference on cpu0 (P-core). Raw: `results/spike2_run1.txt`.
- **Caveat discovered later (§5):** configurations were run one after another, not interleaved, so later rows may be mildly penalised by power throttling. Treat differences under ~5% as noise.

| Kernel | static | dynamic,1 | dynamic,chunk | guided | weighted (sampled) | Note |
|---|---|---|---|---|---|---|
| uniform compute (0.71 s seq) | 0.1297 s | 0.1104 s | **0.1079 s** | 0.1302 s | 0.1128 s | under static the fastest thread was idle **49%** of the loop |
| fine-grained (ns/iteration) | 0.0749 s | skipped | 0.0715 s | 0.0751 s | 0.0748 s | all within noise |
| triad, memory-bound (0.0526 s seq) | 0.0691 s | 0.0709 s | 0.0709 s | 0.0710 s | 0.0678 s | **12 threads slower than 1 P-core (0.74–0.78×)** |
| Mandelbrot rows, irregular (3.91 s seq) | 1.599 s | 0.914 s | **0.845 s** | 1.078 s | 1.585 s | weights alone cannot fix irregular work |

- Correctness: checksums matched the sequential run (relative error ≤ 4e-12 where summation order differs; exact otherwise).
- **Interpretation:**
  1. Static loses ~17–20% on a uniform compute-bound loop purely because cores are unequal.
  2. `dynamic` with a sensible chunk already recovers that. A weighted-static split gets within ~5% of dynamic but does **not** beat it. *Negative result for the naive idea "weighted static beats dynamic".*
  3. Memory-bound loops do not scale at all on this laptop (bandwidth saturated by one P-core). **Benchmarks must be chosen and interpreted with this in mind.**
  4. Capacity weights do nothing for irregular iteration costs.

---

## 4. Spike 3 — loops executed repeatedly over the same data (Jacobi stencil)

- **Question:** when cache locality matters (same rows revisited every sweep), is `dynamic` still the answer?
- **Code:** `spikes/spike3_feedback_jacobi.c` (`-DHEAVY` adds an `exp()` source term per cell). 5 repetitions, median. Raw: `results/spike3_run1.txt`, `spike3h_run1.txt`. Same non-interleaving caveat.

| Grid (plain Jacobi) | seq (P) | static | dynamic,1 row | dynamic,chunk | guided | feedback-weighted static |
|---|---|---|---|---|---|---|
| 512² (4 MB, in cache), 2000 sweeps | 0.1625 s | **0.0950 s** | 0.2228 s | 0.1619 s | 0.1375 s | 0.1027 s |
| 1024² (17 MB), 500 sweeps | 0.6252 s | 0.6336 s | 1.0309 s | 0.7302 s | 0.6744 s | **0.5668 s** |
| 3000² (144 MB), 60 sweeps | 0.9020 s | 1.1735 s | 1.1873 s | 1.2080 s | 1.1884 s | 1.1749 s |

- All final grids matched the sequential result **exactly**.
- `-DHEAVY` variant: all five schedules within ~10% of each other at every size (speedup ≈ 3×, 1.8× at 3000²). No schedule stands out.
- **Interpretation:** `dynamic` is **1.2–2.3× slower than static** on cache-resident repeated sweeps (threads keep getting different rows, and dispatch overhead is comparable to the 50–100 µs sweep). So neither stock schedule is safe everywhere: *static loses on unequal cores, dynamic loses on locality-sensitive loops.* The largest grid is memory-bound and does not scale with any schedule.

---

## 5. Spike 4 — the decisive test: one policy for all loop types

- **Policy "WS" (weighted + steal):** every thread owns a contiguous block of iterations sized by its measured speed (weights fed back from the previous execution of the same loop, exponential smoothing 0.5). A thread that finishes steals the back half of the fullest remaining block. Weights address unequal cores, contiguous blocks keep locality, stealing absorbs irregular work and wrong weights.
- **Code:** `spikes/spike4_weighted_steal.c`. `gcc -O2 -march=native -fopenmp -Wall spike4_weighted_steal.c -o spike4 -lm`; `./spike4 <kernel> <cpu list> <rounds>`.

### 5a. Methodological finding — run order changes the answer (measured)

- Run 1 (`results/spike4_run1.txt`): configs run in fixed order, WS last. WS looked **45% slower** than dynamic on the uniform kernel.
- Run 2 (`spike4_run2.txt`): same binary, 60 s idle first, **order reversed** (WS first, dynamic,1 last). WS = 0.0655 s (best); dynamic,1 = 0.0929 s (was 0.0656 s when it ran early).
- Run 3 raw data shows a step: after ~20 s of sustained all-core load every config becomes ~35–40% slower (e.g. static 0.078 s → 0.108 s per sweep) and stays there.
- **Interpretation:** the CPU drops from a short-term to a long-term power limit (consistent with the published 64 W turbo / 28 W base figures for this chip — *the cause is inferred, not measured; RAPL is not readable*). **Any benchmark that runs configurations sequentially on this laptop is biased against whatever runs last.** Run 3 onward therefore interleaves configurations (every round runs each config once; the starting config rotates each round). This requirement is carried into the Phase 2 methodology.

### 5b. Interleaved comparison, all 12 CPUs, 7 rounds (`results/spike4_run3.txt`)

Per-sweep time, median of 7 rounds. "Regret" = how much slower than the best schedule for that kernel (derived).

| Kernel | static | dynamic,1 | dynamic,N/(8T) | guided | **WS** | seq on P-core |
|---|---|---|---|---|---|---|
| Mandelbrot rows (irregular) | 1.5648 s (+94%) | 0.8303 s (+3.0%) | 0.8388 s (+4.1%) | 1.0573 s (+31%) | **0.8059 s (best)** | 3.905 s |
| Jacobi 512² (in-cache, repeated) | **68 µs (best)** | 126 µs (+86%) | 89 µs (+32%) | 82 µs (+20%) | 70 µs (+2.5%) | 143 µs |
| Jacobi 1024² (repeated) | **0.902 ms (best)** | 1.949 ms (+116%) | 1.097 ms (+22%) | 0.985 ms (+9.2%) | 0.951 ms (+5.5%) | 1.209 ms |

Uniform compute-bound kernel — the power step fell in the middle of the run, so medians over all 7 rounds are **not comparable** across configs (the printed "+13.2%" for WS is an artefact). Comparing within each regime instead:

| Uniform kernel | static | dynamic,1 | dynamic,N/(8T) | guided | **WS** |
|---|---|---|---|---|---|
| rounds 0–2 (before throttling), median | 0.0790 s (+21%) | 0.0653 s (+0.3%) | 0.0678 s (+4%) | 0.0773 s (+19%) | **0.0651 s** |
| rounds 3–6 (throttled), median | 0.1082 s (+23%) | 0.0891 s (+1.4%) | 0.0970 s (+10%) | 0.1036 s (+18%, 3 values) | **0.0879 s** |

**Worst-case regret over the four kernels (derived from the tables above):**

| static | dynamic,1 | dynamic,N/(8T) | guided | WS |
|---|---|---|---|---|
| +94% | +116% | +32% | +31% | **+5.5%** |

- Correctness: every configuration's output matched the sequential checksum exactly in every round.
- Best speedup over one P-core observed: 6.5× (uniform, unthrottled), 4.85× (Mandelbrot), ~2.1× (Jacobi 512²).
- **Interpretation:** in this exploratory run, WS was the only policy within ~6% of the best schedule on every kernel; each stock schedule had at least one kernel where it was 31–116% off. This is the evidence the selected project rests on. It needs rigorous confirmation in Phase 2 (more kernels, more rounds, both power regimes).
- **What this spike does *not* show:** which ingredient matters. On the uniform kernel the first (cold, equal-weights) sweep took 0.066 s versus 0.0655 s once weights had converged — i.e. stealing alone did nearly all the work there. Whether the weights add value beyond "static + steal" is **not yet tested** and is a planned ablation.

### 5c. Which cores to use (`results/spike4_run4_subsets.txt`, uniform kernel, 3 rounds)

| Thread set | Best per-sweep time | Speedup vs 1 P-core | Schedules differ? |
|---|---|---|---|
| P-cores only (4 hyperthreads, cpus 0–3) | 0.1246 s | 3.45× | No — all five identical |
| E-cores only (8 threads, cpus 4–11) | 0.1091 s | 3.9× (derived: 0.4297/0.1091) | ≤5% |
| One thread per physical core (10) | 0.0823 s | 5.2× | static +12%, WS +0.2% |
| All 12 logical CPUs | 0.0651 s (run 3, unthrottled) | 6.5× | static +21%, WS best |

(The log line for the E-only run prints "speedup 7.9" because its sequential reference ran on an E-core; the table uses the P-core reference.)

- **Interpretation:** on homogeneous subsets every schedule performs the same, confirming that core asymmetry — not something else — causes the static penalty. Using all cores is ~1.9× faster than "P-cores only" on this 2P+8E chip, so the vendor's simple advice (run on P-cores only) is costly here. 4 threads on 2 physical P-cores reached 3.45× on this latency-bound kernel; the reason (SMT filling idle execution slots) is *hypothesised*.

---

## 6. Assumptions tested vs not tested

| Assumption | Status |
|---|---|
| OpenMP builds and runs from the NTFS workspace with stock GCC | **Tested — pass** |
| Threads can be pinned to chosen cores without root | **Tested — pass** |
| P/E asymmetry is large enough to matter and is workload-dependent | **Tested — pass** (1.1–2.8×) |
| `schedule(static)` leaves measurable performance on the table | **Tested — pass** (≈20% uniform, ≈94% irregular) |
| A stock schedule already solves it everywhere | **Tested — false** (dynamic loses 22–116% on repeated in-cache sweeps) |
| One weighted+steal policy can stay near-best across loop types | **Tested — supported, exploratory** (≤5.5% regret on 4 kernels) |
| Results are trustworthy without controlling run order | **Tested — false** (power throttling; interleaving required) |
| Output correctness is preserved under the custom scheduler | **Tested — pass** (exact checksums; not yet stress-tested for exactly-once execution) |
| Capacity weights add value beyond stealing alone | **Not tested** (Phase 2 ablation) |
| Results hold on another hybrid CPU / with LLVM's `static_steal` | **Not tested** (no second machine; `libomp` not installed) |
| Energy or hardware-counter measurements | **Not possible** without sudo |
| Python venv + matplotlib install for plots | **Not tested** (wheels confirmed to exist on PyPI only) |
| CUDA / OpenACC / MPI toolchains (rejected candidates) | **Not tested** — not available locally |

---

## 7. Phase 1B addendum (2026-11-11) — read before relying on §5

Nothing above has been edited. Later work changes how it should be read:

1. **§5b used a flawed ordering.** Rotating the starting configuration does not remove a neighbour effect: a configuration that leaves cores idle (static, guided) lets the next one run faster. In §5b the prototype ("WS") always ran right after `guided`, and `dynamic,1` right after `static` — the two that looked best. With randomised order and a conditioning load (Phase 1B), differences among dynamic-style policies shrank to a few percent. **The "+5.5% worst-case regret" result is unconfirmed.** The large effects (static and guided losing 20–90% on unequal or irregular work; `dynamic,1` losing heavily on small in-cache sweeps) are consistent with later fair runs.
2. **The uniform synthetic kernel overstated core asymmetry.** It is a single dependent chain of floating-point operations, which hyperthreading accelerates unusually well, so each P hyperthread appeared ~1.8× an E-core. In three real applications a P hyperthread was about as fast as an E-core.
3. **The throttling cause in §5a is now a read value, not an inference:** sustained package limit 15 W over a 28 s window (`intel-rapl-mmio:0`), platform profile `balanced`.
4. Application-level experiments, the corrected method and the revised project choice are in `APPLICATION_FEASIBILITY_RESULTS.md`, `PROJECT_OPTIONS_COMPARISON.md` and `FINAL_PROJECT_PROPOSAL.md`.
