# Phase 2 Blueprint (V2) — Exact Protein Database Search with Two-Level Parallelism

Supersedes `PHASE_2_BLUEPRINT.md` (kept for the record). Do not start until the student explicitly initiates Phase 2. Record deviations in `DECISIONS.md`.

## 1. Toolchain

- C11, GCC 15, `-O2 -march=native -fopenmp`. One primary parallel model: **OpenMP** (threads via `parallel`, vectorisation via `simd`).
- Build with a plain `build.sh` (no `make`/`cmake` installed).
- Application: local web app (Flask server + plain HTML/JS + Chart.js + 3Dmol.js), specified in `APP_DESIGN.md`; decision `DECISIONS.md` D1.
- Python venv for the app server, plots and the Parasail oracle (flask, numpy, matplotlib, parasail — all have wheels; Parasail was installed and used in Phase 1B). If a venv on the NTFS project folder misbehaves, use `--copies` or place it under the home directory and document it.
- Linux x86-64 with AVX2. Scalar fallback always available.

## 2. Data flow

```
query.fasta ─┐
             ├─► load ─► for each query:  build per-query tables
db.fasta ──► preprocess (once) ─► db.bin                    │
             encode residues, sort by length,                ▼
             pack into batches of 16                  parallel loop over batches
                                                      (each thread: SIMD kernel on 16 sequences)
                                                             │
                                                 scores ─► overflow check ─► scalar re-score if needed
                                                             │
                                                 top-K selection ─► scalar traceback ─► report
```

## 3. Modules

| File | Responsibility |
|---|---|
| `src/fasta.c` | Read FASTA; map residues to codes; unknown → X |
| `src/matrix.c` | Load a substitution matrix (BLOSUM62 file) |
| `src/dbprep.c` | One-time conversion: sort by length, form batches of 16, write a binary file with an index back to original order and names |
| `src/sw_scalar.c` | Reference kernel (32-bit scores), plus traceback for reported hits |
| `src/sw_simd.c` | Batch kernel: 16 lanes × 16-bit, plain C + `#pragma omp simd` |
| `src/search.c` | Per-query driver: parallel loop over batches, per-thread buffers, overflow handling, top-K |
| `src/mapping.c` | Batch-to-thread mapping policies behind one interface (stock schedules; contiguous blocks + stealing; cost-aware blocks) — adapted from the Phase 1B harness |
| `src/topology.c` | Core types from sysfs; thread sets `all`, `physical`, `p_only`, `e_only`, explicit list; pinning; short calibration to pick a default set |
| `src/trace.c` | Optional per-thread busy/idle log for the timeline figure |
| `src/main.c` | Engine entry point `hysw_engine prep / search / info`, JSON output and JSON-lines progress (internal interface for the app; see `APP_DESIGN.md` §4) |
| `app/server.py` | Flask server on 127.0.0.1: input validation, job handling, runs the engine, relays progress (Server-Sent Events), caches AlphaFold structures |
| `app/static/` | Plain HTML/CSS/JS front end: Setup, Search, Performance Lab; bundled Chart.js and 3Dmol.js |
| `app/launch.sh`, `app/hysw.desktop` | Double-click launcher: build engine if missing, start server, open browser |
| `tests/` | Correctness tests (§6) |
| `bench/` | Harness with randomised, conditioned ordering (§7); raw CSV |
| `scripts/` | `oracle_parasail.py`, `analyse.py`, `plot.py`, `sysinfo.sh` |

## 4. Algorithms

### 4.1 Scalar reference
Gotoh's affine-gap Smith–Waterman, score only, two rows of state. Gap of *k* residues costs 11 + *k* (in Parasail's convention: open 12, extend 1 — confirmed empirically in Phase 1B).

### 4.2 SIMD batch kernel (as prototyped)
For each database column *j*: build a small table `prof[residue][lane]` giving the score of every residue type against the 16 residues in that column (finished lanes get a large negative score). Then walk the query; the innermost loop over the 16 lanes is straight-line integer code with `max` operations and is vectorised by the compiler.

Design options to settle by measurement:

| Option | Choices | Decided by |
|---|---|---|
| State layout | separate H and E arrays (prototype) vs interleaved | Speed vs query length on P- and E-cores (per-thread state is 64 bytes per query residue: beyond L1 for long queries) |
| Profile build | all 24 residue rows vs only residues present in the query | Overhead for short queries |
| Lane width | 16-bit × 16 (baseline); 8-bit × 32 with saturation fallback (stretch) | Speed vs fallback rate |
| Batch composition | strict length order vs small groups to cut padding | Measured padding waste |
| Vector width | let the compiler choose vs force 128-bit | Speed on E-cores specifically |

### 4.3 Overflow
A lane whose best score comes within one maximum substitution score of 32,767 is flagged; flagged sequences are re-scored with the 32-bit scalar kernel. Final scores are therefore always exact.

### 4.4 Thread mapping
Iterations are batches in length order. Policies compared: `static`, `dynamic` (two chunk sizes), `guided`, contiguous blocks + stealing, cost-aware blocks (cost = batch length) ± capacity weights. Phase 1B says the dynamic-style policies tie; the default will be the simplest one that stays within noise of the best. The comparison is repeated on the full database before fixing it.

### 4.5 Core selection
At start-up (or on request), run a one-second calibration on a slice of the database for a few thread sets and pick the fastest. The study behind it — P-only, E-only, physical, all — is reported in full.

## 5. Datasets

| Use | Dataset | Status |
|---|---|---|
| Unit tests, fast benchmarks | *E. coli* K-12 reviewed proteome (4,403 sequences) | In `research/data/` |
| Main benchmark and demo | UniProtKB/Swiss-Prot FASTA | 94 MB compressed, availability checked; **licence text and exact size to be read before download** |
| Queries | A fixed, documented set of real proteins spanning short to long | To be chosen in Phase 2 |
| Edge cases | Synthetic: empty, length 1, identical, very long, non-standard residues | To be generated |

## 6. Correctness (before any timing)

1. Scalar kernel vs Parasail on every sequence of the test proteome for every test query — must be 100% identical.
2. SIMD kernel vs scalar kernel — 100% identical, including batches with fewer than 16 sequences and very unequal lengths.
3. Overflow path: artificial sequences that exceed 16-bit scores must still match the scalar kernel.
4. Every mapping policy and thread set vs sequential — identical output (hash and full comparison).
5. Traceback: reported alignment's score recomputed from the alignment equals the reported score.
6. Robustness: malformed FASTA, unknown residues, query longer than any database sequence, single-sequence database.
7. Builds with `-Wall -Wextra` and with address/undefined-behaviour sanitisers for the test suite. A build check fails if the kernel loop is reported as not vectorised.

## 7. Benchmark methodology

- **Ordering:** fresh random order of configurations every round; identical all-core conditioning load before every measured run, including the sequential baselines.
- **Regimes, reported separately:** *sustained* (conditioned, long database) and *burst* (after a cool-down, short run). State the power profile and the read power limits with every result set.
- **Rounds:** at least 9; report median and inter-quartile range; keep raw samples. Differences smaller than the spread are ties.
- **Thread sets:** 1 P-core, P-only (2 and 4 threads), E-only, one per physical core, all logical CPUs.
- **Metrics and definitions:**
  - cell updates per second = query length × database residues ÷ time;
  - SIMD speedup = scalar one-thread time ÷ SIMD one-thread time;
  - thread speedup = SIMD one-thread time ÷ SIMD multi-thread time (same regime);
  - total speedup = product, stated as such;
  - regret of a policy = its time ÷ best policy's time − 1;
  - energy per search and cell updates per joule, only if energy readings are enabled.
- **Yardstick:** Parasail on the same machine, one thread, same queries and database; its call overhead through Python stated. Published figures from other papers appear only as context with their hardware named.
- **Environment record:** `sysinfo.sh` output (CPU, kernel, compiler, governor, power profile, power limits, temperatures) stored with each result set; exact commands in the CSV header.
- **Negative results** stay in the main tables.

## 8. Milestones

| # | Milestone | Done when |
|---|---|---|
| M1 | Repository skeleton, `build.sh`, venv, `sysinfo.sh`, README stub, `DECISIONS.md` | Builds a hello target; venv imports parasail and matplotlib |
| M2 | FASTA + matrix loading, scalar kernel, Parasail oracle test | 100% score agreement on the test proteome |
| M3 | Database preprocessing and SIMD batch kernel, overflow path | SIMD = scalar on all tests; vectorisation check passes |
| M4 | Threaded search with selectable mapping and thread set; top-K and traceback | All policies and sets match sequential; a real search prints sensible hits |
| M5 | Benchmark harness (randomised, conditioned), tracing, pilot on the small proteome | Raw CSV and a timeline figure produced |
| M6 | Full study on Swiss-Prot: SIMD options, mapping, thread sets, regimes, Parasail yardstick, optional energy | All tables and figures regenerate from raw data with one command |
| M6a | App, part 1: server + Search screen (input, progress, hit table, alignment, run summary, export) | A real search runs end-to-end from the browser; results equal the engine's direct output |
| M6b | App, part 2: 3D structure view with aligned region highlighted (cached, offline-safe); Performance Lab (race, core timeline, SIMD view, stored benchmark charts); launcher | Demo runs from a double-click with the network off (cached demo proteins) |
| M7 | Report, case study, slides, demo and narration script; clean-copy setup check | A fresh copy builds, passes tests and reproduces a small benchmark from the README |

Order rule: the engine (M1–M5) is finished and verified before app work starts; a UI delay must never block measurements.

## 9. Final submission contents

- Source, tests, scripts, `README.md` (prerequisites, build, test, run, benchmark, plot).
- Dataset acquisition instructions (no large files committed).
- Raw and aggregated results with system records; figures: speedup decomposition (scalar → SIMD → threads), speed vs query length, thread-set comparison, mapping-policy regret, per-core timeline, burst vs sustained, run-order artefact demonstration, comparison with Parasail.
- Report with citations, including limitations and negative results.
- Case study: *from minutes to seconds — and why twelve threads are not twelve times faster on a laptop*, mapped to syllabus units.
- Slide content (PPTX if tooling permits).
- Video storyboard, screen-recording sequence and narration script. The recording itself is made by the student.

## 10. Fallbacks

| If… | Then… |
|---|---|
| The compiler stops vectorising the kernel after refactoring | Keep the prototype's loop shape; as a last resort add a small intrinsics version, with the portable version retained and compared |
| Swiss-Prot cannot be used | Benchmark on a replicated reference proteome (as in Phase 1B) and say so |
| Thread-set auto-selection is unreliable | Ship a documented default (one thread per physical core) and report the study |
| Energy readings are unavailable | Drop energy; nothing else depends on it |
| Results differ on a rerun | Report the spread; do not claim effects below it |
