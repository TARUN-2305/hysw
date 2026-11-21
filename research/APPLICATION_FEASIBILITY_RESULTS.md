# Application Feasibility Results — Phase 1B

Session 2026-11-11, student's laptop (i3-1220P, 2 P-cores with hyperthreading + 8 E-cores, GCC 15.2, `-O2 -march=native -fopenmp`, AC power, platform profile "balanced").
Everything here was **executed** unless marked *derived*, *estimate* or *hypothesis*. All results are **exploratory**: one machine, one session, 3–7 rounds per configuration.

- Code: `research/spikes_1b/` (`hsched.h` shared harness, `flood.c`, `swsearch.c`, `swsimd.c`, `pagerank.c`, `parasail_check.py`)
- Raw output: `research/spikes_1b/results/`
- Data: `research/data/` (sources S25–S28 in `SOURCE_REGISTER.md`)

## 1. Method

### 1.1 What was compared

One harness function runs any loop under nine policies, so every application sees the same set:

| Policy | Meaning |
|---|---|
| `static`, `dynamic,1`, `dynamic,n/8T`, `guided` | Stock GCC OpenMP schedules |
| `steal` | Equal contiguous blocks + work stealing |
| `share+steal` | The Phase 1 prototype: blocks sized by each thread's share of iterations last time + stealing |
| `cost+steal` | Blocks of equal *application cost* (cost hint per iteration) + stealing |
| `cost*cap+steal` | Blocks proportional to cost **and** to each thread's measured speed + stealing |
| `cost*cap (no steal)` | The same model without stealing |

Correctness was checked on every run by hashing the full output and comparing with the sequential run: **every policy in every application produced bit-identical output**.

Sequential reference: same code on one thread pinned to P-core cpu0. "Regret" = time ÷ best time among the policies in that run − 1.

### 1.2 A second measurement flaw found and fixed (important)

Phase 1 rotated the *starting* configuration each round. The first 1B batch ("run 1") did the same and produced a suspicious pattern: whichever policy ran immediately after `static` or `guided` looked best.

- **Cause (hypothesis, consistent with the data):** `static` and `guided` leave cores idle, so the CPU regains power budget; the next configuration starts faster. In a fixed cyclic order each policy always follows the same neighbour, so rotation does not cancel this.
- **Fix:** (a) a fresh random order every round; (b) the same 0.5 s all-core load before every measured run.
- **Effect (measured):** in the protein search, policies that had looked 23% slower than plain stealing in run 1 were within 1–2% of it after the fix.

**Consequences.** Only results marked **fair** below should be used for conclusions. Results marked **run 1** are kept for the record. The Phase 1 ranking (`FEASIBILITY_LOG.md` §5b) used the flawed ordering and must be treated as unconfirmed; see the addendum there.

Remaining weakness: the sequential reference was timed without the conditioning load, so reported thread speedups are, if anything, pessimistic.

### 1.3 The machine's power limit (measured configuration)

`/sys/class/powercap/intel-rapl-mmio:0`: long-term limit **15 W** over a 28 s window, short-term 25 W. Platform profile: `balanced` (choices: quiet / balanced / performance).

This replaces Phase 1's inference about throttling with a read value, and explains the gap between short and long runs: one thread at full speed already uses a large share of 15 W, so twelve threads cannot go twelve times faster.

## 2. Protein database search (Smith–Waterman)

Database: 4,403 reviewed *E. coli* K-12 proteins, 1,354,442 residues (longest 2,339). Queries: three proteins from the same set, lengths 150, 400, 990. BLOSUM62, gap cost 11 + 1 per residue. One search = all three queries against the whole database.

### 2.1 Correctness against an independent library

| Check | Result |
|---|---|
| Scalar C vs Parasail (`sw_striped_profile_16`, open 12, extend 1) | **13,209 / 13,209 scores identical** |
| SIMD C vs scalar C | identical (hash) |
| Every parallel policy vs sequential | identical (hash) |

### 2.2 Data-level parallelism (one thread)

| Implementation | Time for one search | Billion cell updates/s | vs scalar |
|---|---|---|---|
| Scalar C | 2.73 s | 0.77 | 1× |
| **SIMD C** — 16 sequences per batch, 16-bit lanes, plain C + `#pragma omp simd` (compiler report: "loop vectorized using 32 byte vectors") | 0.222 s | 9.4–9.6 | **12.3–12.7×** |
| Parasail striped, hand-written vector code (timed through Python, overhead included) | 0.213 s | 9.8 | 12.8× |

The compiler-vectorised spike is within about 4% of an established hand-vectorised library on this workload. (Single run each; Parasail uses a different vectorisation strategy, so this is a yardstick, not a like-for-like contest.)

### 2.3 Thread-level parallelism on top of SIMD — **fair** ordering

Database replicated ×20 (88,060 sequences, 27 M residues) so runs last seconds; sorted by length (needed for SIMD batching). Times are for one search; speedup is relative to **one-thread SIMD** (4.34–4.47 s).

| Policy | 12 logical CPUs | 10 threads (one per physical core) | P-cores only (4 hyperthreads) |
|---|---|---|---|
| static | 3.543 s (+26%) | 3.281 s (+23%) | 4.591 s (+28%) |
| dynamic,1 | 2.905 s (+3.3%) | 2.710 s (+1.8%) | **3.596 s** |
| dynamic,n/8T | 2.880 s (+2.4%) | 2.730 s (+2.6%) | — |
| guided | 3.564 s (+27%) | — | — |
| steal | 2.858 s (+1.6%) | 2.703 s (+1.6%) | 3.628 s (+0.9%) |
| share+steal | 2.940 s (+4.5%) | — | — |
| cost+steal | 2.881 s (+2.4%) | 2.717 s (+2.1%) | — |
| cost*cap+steal | **2.813 s** | **2.661 s** | — |
| cost*cap (no steal) | 3.008 s (+6.9%) | — | — |
| Best speedup over one-thread SIMD | 1.54× | **1.68×** | 1.22× |

Scalar version, length-sorted, 12 CPUs, fair: static +47%, guided +65%, **all other seven policies within 1.3% of each other** (≈2.47× over scalar sequential); no-steal +7.7%.

Measured per-thread speeds (SIMD): with 12 threads a P hyperthread ≈ 0.062–0.074 and an E-core ≈ 0.090 of total; with 10 threads a P-core ≈ 0.13 and an E-core ≈ 0.092 (P/E ≈ 1.4).

### 2.4 What this shows

- SIMD is where the speedup is: ~12.5× on one core.
- Under the 15 W limit, threads add only 1.5–1.7× on top in sustained runs. A single short run on the small database (not conditioned) reached 3.06× — the burst regime. *Derived:* about 19–21× over scalar sequential sustained, about 38× in a short burst.
- The default schedule is a real trap: length-sorting for SIMD makes `static` 23–47% slower.
- **Any dynamic-style schedule fixes it.** Differences among `dynamic`, `steal` and the cost/capacity-aware variants are 0–4%, inside the noise of 4–5 rounds. The custom scheduler brings no demonstrable benefit here.
- **Which cores are used matters more than how work is scheduled:** 10 threads beat 12 by ~5%, and beat "P-cores only" by 35%. The vendor's simple advice (P-cores only) is the worst option measured.
- *Estimate, not measured:* a 400-residue query against Swiss-Prot (roughly 2×10⁸ residues — figure from memory, to be confirmed) would take on the order of 100 s scalar, 9 s with one-thread SIMD and 5–6 s sustained on all cores.

## 3. Flood inundation on Bengaluru terrain

Model: inertial shallow-water scheme (S17), 768×768 cells of 30 m cropped from SRTM tile N12E077 (elevation 736–993 m), Manning 0.05, 1 mm depth threshold, closed boundaries, face flow limiter. Two loops per step (face flows; depth update). Dry rows are skipped; dry cells inside wet rows take a cheap path.

Numerical sanity: stored volume ÷ inflow = **1.000000**, no negative depths, repeat runs hash-identical. (A first version without the flow limiter was unstable and created mass; that version's numbers were discarded.) **This is a workload study, not a validated flood forecast.**

### 3.1 Fair ordering

| Policy | Localised inflow (9 sources × 1000 m³/s, 60 min; 2–4% of cells wet), 12 CPUs | Rain everywhere (60 mm/h; 50–73% wet), 10 threads |
|---|---|---|
| sequential | 3.420 s | 1.700 s |
| static | 2.111 s (+32%) | 0.608 s (+9.2%) |
| dynamic,1 | 2.045 s (+28%) | 0.585 s (+5.1%) |
| dynamic,n/8T | 1.724 s (+7.5%) | 0.570 s (+2.3%) |
| guided | 2.316 s (+44%) | 0.566 s (+1.7%) |
| steal | 1.636 s (+2.0%) | 0.564 s (+1.3%) |
| share+steal | **1.604 s** | 0.559 s (+0.5%) |
| cost+steal | 1.608 s (+0.3%) | 0.558 s (+0.2%) |
| cost*cap+steal | 1.615 s (+0.7%) | **0.557 s** |
| cost*cap (no steal) | 1.615 s (+0.7%) | 0.600 s (+7.8%) |
| Best speedup over sequential | 2.13× | 3.05× |

### 3.2 Run 1 (cyclic order — biased, kept for the record)

- Inflow, 12 CPUs: static +34%, guided +42%, dynamic,1 +24%, dynamic,n/8T +8.5%, steal best. Inflow, 10 threads: static +73%, dynamic,n/8T +9.5%, steal family best. The ~8% advantage of contiguous blocks over the best stock schedule appears in all three inflow runs, including the fair one.
- Rain for 60 simulated minutes, 12 CPUs: all nine policies within 14%; speedup 2.1–2.4×.
- Thread sets, short rain run: 2 P-cores 1.78×, 4 P hyperthreads 2.34×, 8 E-cores 3.7×, 10 physical cores 4.29×, all 12 4.23×. Short runs reach ~4.2×; long ones ~2.3× (the 15 W limit again). Hyperthreading added nothing for this kernel.
- 2048×2048 grid, rain: 2.2–2.3×, policies within 8%.

### 3.3 What this shows

- Scheduling matters when the flood is localised, and not when water is everywhere — different stages have different needs.
- Contiguous blocks (with stealing, or sized by wet-cell counts) beat the best stock schedule by about 7–9% and the default by about 30%. One-row-at-a-time `dynamic` is poor (+28%), consistent with a locality and dispatch cost.
- The wet-cell cost model alone (no stealing) matched the best policy in the inflow case.
- Sustained speedup is about 2×; the model's hydrological credibility is low without much more domain work.

## 4. PageRank on `web-Google` (875,713 nodes, 5,105,039 edges) — run 1 ordering only

| Thread set | Best time for 50 iterations | Speedup over sequential (0.73–0.85 s) | Spread among dynamic-style policies |
|---|---|---|---|
| 12 CPUs | 0.449 s | 1.63× | ≤5% |
| 10 threads | 0.354 s | 2.13× | larger, but ordering was biased |
| P-cores only | 0.399 s | 2.12× | ≤1% |

Not re-run under fair ordering: the low speedup alone rules it out. Four P hyperthreads did as well as ten cores — the loop is limited by memory latency.

## 5. Does capacity (P-core vs E-core) weighting help?

| Application (fair runs) | `cost+steal` (cores assumed equal) | `cost*cap+steal` (cores weighted by measured speed) |
|---|---|---|
| Protein search SIMD, 12 CPUs | 2.881 s | 2.813 s (−2.4%) |
| Protein search SIMD, 10 threads | 2.717 s | 2.661 s (−2.1%) |
| Protein search scalar, 12 CPUs | 1.099 s | 1.094 s (−0.5%) |
| Flood inflow, 12 CPUs | 1.608 s | 1.615 s (+0.4%) |
| Flood rain, 10 threads | 0.558 s | 0.557 s (−0.2%) |

A consistent but tiny edge (~2%) only in the SIMD search, where the P/E gap is largest; indistinguishable from noise at this number of rounds. **Not demonstrated.** Reason (measured): with hyperthreading on, each P hyperthread is about as fast as an E-core, so there is little asymmetry left to exploit; and stealing corrects what remains.

## 6. Tested / not tested

| Question | Status |
|---|---|
| Real data loads and runs for all three applications | **Pass** |
| Parallel output identical to sequential | **Pass**, all policies, all applications |
| Protein scores agree with an independent library | **Pass**, 13,209/13,209 |
| Compiler can vectorise the search kernel without intrinsics | **Pass**, 12.5×, within ~4% of Parasail |
| Custom scheduler beats stock `dynamic` in protein search | **Fail** (tie) |
| Custom scheduler beats stock schedules in flood model | **Partly** (≈7–9% in the localised case, tie otherwise) |
| Capacity weighting is worth having | **Not demonstrated** |
| Fixed cyclic ordering gives trustworthy comparisons | **Fail** → randomised + conditioned ordering adopted |
| Search against full Swiss-Prot | Not run (94 MB download deferred to Phase 2) |
| 8-bit lanes, score-overflow handling, alignment traceback | Not tested |
| Energy per search; hardware counters | Not possible without the student enabling access |
| "Performance" power profile | Not tested (system setting; the student's choice) |
| Parasail multi-threaded, SWIPE, or any GPU tool on this machine | Not tested |
