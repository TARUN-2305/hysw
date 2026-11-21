# Phase 2 progress (resume here)

Session 1 — 2026-11-20. Milestones M1–M5 of `research/PHASE_2_BLUEPRINT_V2.md` built; deviations in `DECISIONS.md` D2.

## Built
| Milestone | What exists |
|---|---|
| M1 | `build.sh` (release + `san`; fails if the SIMD lane loop is not vectorised), `.venv` + pinned `requirements.txt` (parasail 1.3.4, numpy, matplotlib, Flask), `scripts/sysinfo.sh`, README, `.gitignore` |
| M2 | `src/fasta.c`, `src/matrix.c`, `src/sw_scalar.c` (32-bit Gotoh), `scripts/oracle_parasail.py`, 8 fixed test queries in `tests/queries/` (`scripts/make_test_queries.py`) |
| M3 | `src/db.c` (length-sorted, 16-lane interleaved `.bin`, mmap), `src/sw_simd.c` (16 × 16-bit, `omp simd`), overflow flag + 32-bit re-score |
| M4 | `src/topology.c` (P/E/hyperthread sets), `src/mapping.c` (7 policies), `src/search.c` (threads, progress JSON lines, trace, top-K), `src/align.c` (3-pass traceback), `src/main.c` (`info` / `prep` / `search`, JSON) |
| M5 | `bench/run_bench.py` (random order per round, conditioning load, burst/sustained), `scripts/analyse.py`, `scripts/plot_timeline.py`, `scripts/replicate_fasta.py` |

## Verified (all measured in this session; `tests/run_tests.sh`, ~35 s)
- Parasail oracle: **35,224 / 35,224** scores identical (8 queries × 4,403 E. coli proteins) for the scalar kernel, and again for the SIMD kernel on all 12 CPUs; matrix file identical to `parasail.blosum62`. Record: `results/verification/parasail_oracle.json`.
- Overflow: synthetic database with scores up to 37,400 — 96/96 identical to Parasail's 32-bit kernel; engine re-scored 6 + 2 sequences.
- Kernel tests: 2,187 checks (SIMD = scalar on random databases incl. partial batches and 0/1-residue sequences; 402 tracebacks re-score exactly; 72 policy × thread-set × kernel runs identical to sequential). Also clean under ASan + UBSan (`tests/run_tests.sh --san`, 4.5 min).
- Engine interface tests: 460 checks (JSON, ordering, alignments re-scored from JSON, bad input → JSON error, `.bin` = `.fasta`).

## Pilot measurements (exploratory: E. coli × 20 = 27.1 M residues, 3 queries 150/400/990, 5 rounds, sustained, 2 s conditioning)
`results/pilot/m5_policies_ecolix20.*` — median time, one thread per physical core:
costcap+steal 1.997 s, dynamic,chunk 2.005, steal 2.038, cost+steal 2.074, dynamic,1 2.139 (IQR 9–21% → all ties), **static 2.804 (+40%)**, **guided 3.484 (+74%)**.
Timeline figure: `results/pilot/timeline_static_vs_dynamic.png` (static leaves E-cores idle; dynamic finishes all threads together).
One-thread SIMD on the test queries: 8.0–11.8 GCUPS (single runs); scalar 0.66–0.80 GCUPS.

## Not done / next
1. **Swiss-Prot ready** (downloaded with the student's OK): `data/swissprot_2026_04/swissprot.bin` — release 2026_04, 576,073 sequences, 209.2 M residues, longest 35,213, padding 0.19%; prep 1.7 s, 700 MB peak RAM. Licence CC BY 4.0 (`LICENSE` kept with the data). Re-create with `scripts/get_swissprot.sh`. **Parasail oracle at full scale: 1,152,146 / 1,152,146 identical** (q_short + q_medium × all of Swiss-Prot; `results/verification/parasail_oracle_swissprot.json`). Single cold runs, 10 threads: 28.8 GCUPS (length 150), 35.5 GCUPS (length 400) — burst, not yet a controlled measurement.
2. **M6 study done, twice.**
   - **First run** (2026-11-20 23:28 → 11-21 01:10, untiled kernel, full profile): archived in `results/m6_v1_untiled/` with its cache diagnosis. It found that on 10 threads, queries of L≥990 ran at about half the speed of L=400 (E-core clusters saturating their shared L2). The student chose to fix this first (D3, D4).
   - **Fix: tiled SIMD kernel** (D4). Verified before the re-run: 2,278 kernel checks (SIMD = scalar for both loop orders × both profiles), Parasail oracle 35,224/35,224 in four modes (scalar, default, `--profile full`, `--kernel simd_untiled`), overflow path 96/96, engine tests 460, and `tests/run_tests.sh --san`: ALL TESTS PASSED under ASan + UBSan.
   - **Re-run** (2026-11-21 16:16 → 17:53, AC power in every experiment, randomised + conditioned; the battery was charging from 6% during it, package 46–67 °C at experiment starts): `results/m6/SUMMARY.md` + 8 figures, regenerate with `.venv/bin/python -I scripts/make_m6_report.py`. Measured headlines (default engine: tiled kernel, query profile, `dynamic,1`):
     - E1 (Swiss-Prot, L=400): scalar 0.71 → SIMD 9.00 GCUPS (**12.7×**) → SIMD + 10 threads 22.6 GCUPS (2.52× more), **31.9× total**.
     - E2: 1/physical core 23.7, all 12 CPUs 23.3 (tie), 8 E-cores 22.6, 2 P-cores 14.3, 2 P-cores + HT 14.1, 1 thread 9.2 GCUPS.
     - E3: dynamic-family and stealing policies tie (0–0.8%); `static` +31%, `guided` +48% (IQR 25%).
     - E4: from idle, 10 physical cores ~32 GCUPS for ~17 s, then ~19 after 40 s (−41%); 1 thread −17%. The 15 W limit remains the ceiling.
     - E5: the query-only profile is 2.1× faster at L=10 on one P-core. Tiled vs untiled is in D4.
     - E7 (the controlled before/after): at L=990 tiled = **1.54×** untiled on 10 threads, 1.50× on 12; L=150/400 tie to +5%. Cost: one P-core thread on short queries is slower (L=50 −27%, L=150 −8%; E5).
     - E6 vs Parasail, 1 thread, same P-core (3 rounds): Parasail/HySW = 1.50 at L=150, 1.13 at L=400, 1.03 at L=990, 1.12 overall. With 3 rounds, differences under ~10% count as ties, so HySW is faster on short queries and level on long ones. Parasail's time includes Python call overhead.
     - The first and second runs differ in kernel, profile and machine state, so compare across them only through E7/E5, which ran both kernels in the same randomised rounds.
3. **Calibration selector built and measured** (D5, `results/calibration/SUMMARY.md`): `hysw_engine calibrate` / `--cores calibrate`. 30/30 calibrations (0.25–3 s budgets) chose `physical`, the set that full burst searches also rank first (39.0 GCUPS vs all 37.8, e 30.7, p 19.3). `--cores auto` therefore stays `physical`; calibration is opt-in. Tests: 2,309 kernel checks, 471 engine checks, ASan + UBSan pass.
4. **M6a done (web app part 1, D6):** `.venv/bin/python -I app/server.py --open`. Search screen (input, examples, live per-CPU progress, hit table, alignment, run summary with labelled scalar estimate, CSV/text/JSON exports, cancel) and Setup screen (databases, hardware, calibration). `tests/test_app.py`: 66 checks, including app results identical to the engine's direct output; run 20 times.
5. Optional, needs the student to run two `sudo` commands: energy (RAPL) and perf counters.
6. **M6b built (D7):**
   - The 3D structure view (AlphaFold DB models, cached; aligned region highlighted only when the model's sequence matches; confidence colouring; 2D protein map without WebGL).
   - The Performance Lab "Before and after", with five pairs that are live, stored or replayed, every number labelled.
   - The launcher (`app/launch.sh`).
   - Engine: `simdtrace` (4,011,861 traced cells identical to a textbook matrix; search kernel speed a measured tie).
   - Swiss-Prot Parasail oracle re-run on the tiled kernel: 1,152,146/1,152,146 identical.
   - Design followed the Impeccable skill (PRODUCT.md, a structure chosen by the student, a direction contract, an independent finish review).
   - Not done: Swiss-Prot download from the Setup screen (the data is already present; `scripts/get_swissprot.sh`), and installing the desktop entry (`app/install_launcher.sh`, the student's choice).
7. **M7 drafted (2026-11-21):**
   - `docs/REPORT.md` (report with citations, negative results, limitations).
   - `docs/CASE_STUDY.md` (mapped to syllabus units I–IV; V not covered).
   - `docs/SLIDES.md` (12-slide content; the figures are in results/).
   - `docs/DEMO_SCRIPT.md` (storyboard, recording sequence, narration).
   - **Clean-copy check:** the project tarred without binaries or prepared data, `./build.sh` OK, 2,351 kernel and 481 engine checks pass, and the small benchmark reproduced from the README: E. coli, q_medium, 2 rounds, on battery, so a smoke test rather than a measurement: scalar 0.41 / SIMD 4.41 / SIMD + threads 30.0 GCUPS.
   - **Student to do:**
     - Check the three references given from memory (Smith–Waterman 1981, Gotoh 1982, Farrar 2007).
     - Make the PPTX if wanted.
     - Record the video on AC power.
     - Optionally decide the DESIGN.md token-scale drift (D7).
