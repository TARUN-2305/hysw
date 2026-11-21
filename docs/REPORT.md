# HySW: exact protein database search with two levels of parallelism on a hybrid laptop CPU

CS372IA Parallel Architecture and GPU Programming, Experiential Learning project report. All numbers below are measured on the student's laptop unless labelled otherwise; every figure regenerates from raw data in `results/` (see the README). Source IDs (S…) refer to `research/SOURCE_REGISTER.md`.

## 1. Problem
A biologist with one protein sequence asks which known proteins it resembles, how closely and where. The exact answer is the Smith–Waterman local alignment score against every protein in a database [S10, S11; Smith & Waterman 1981, Gotoh 1982]. For one 147-residue query against UniProtKB/Swiss-Prot (576,073 proteins, 209.2 M residues) that means 30.8 billion dynamic-programming cells. On one core in plain C (0.71 GCUPS, measured) this takes about 43 s; heuristic tools (BLAST) are faster but may miss distant matches. The project asks how far a cheap hybrid laptop CPU (Intel i3-1220P: 2 performance cores with hyper-threading + 8 efficiency cores, AVX2, 15 W long-term power limit [S8, S32]) can take *exact* search, and where the speed comes from.

## 2. Contribution (bounded, not novel in method)
Inter-sequence SIMD is established (Rognes' SWIPE [S10]); hybrid-core scheduling is studied in HPC [S1–S5, S14]. The contribution is an **engineered and measured** combination on a consumer hybrid CPU:
1. A portable SIMD kernel in plain C (`#pragma omp simd`, no intrinsics) scoring 16 length-sorted database proteins per vector, with a 16-bit overflow path re-scored exactly in 32 bits.
2. A cache fix found by measurement: column tiling of the kernel (D4), 1.54× faster for long queries on 10 threads.
3. A controlled study of thread sets, seven batch-to-thread mapping policies, burst vs sustained power regimes, and a calibration selector, with randomised run order and conditioning (a Phase 1B finding: run order alone moved results).
4. A local web app that makes the parallelism visible with real engine output (live per-core progress, a replay of real SIMD cell values, traced schedules).

## 3. Design
- **Data layout.** The database is sorted by length and stored in batches of 16, residue *j* of all 16 proteins adjacent, so one 16-byte load feeds one vector step and padding is 0.19% of Swiss-Prot.
- **Kernel.** Gotoh affine gaps (open 12, extend 1), BLOSUM62 [S28]. 16 lanes × int16 per AVX2 register; a per-column profile row for each residue (only the query's residues, D3). The build fails if GCC stops vectorising the lane loop.
- **Tiling (D4).** The original loop swept all query rows per column; for queries above ~500 residues the per-thread H/E arrays left L1 and the four E-cores sharing one 2 MB L2 saturated it. Processing columns in blocks of 16 keeps the working set in L1 and cuts L2 traffic 16×.
- **Threads.** OpenMP over batches; each thread is pinned and owns its buffers (no shared writes except its own score slots). Policies: static, dynamic(1), dynamic(chunk), guided, steal, cost+steal, costcap+steal.
- **App.** Flask on 127.0.0.1 runs the engine as a separate process; the engine reports progress as JSON lines. Structures come from AlphaFold DB (CC BY 4.0), viewed with 3Dmol.js.

## 4. Correctness (before any timing)
- Every score compared with Parasail 1.3.4 [S11, S31]: **35,224/35,224** identical (E. coli, 8 queries) in four kernel modes, and **1,152,146/1,152,146** on all of Swiss-Prot with the final tiled kernel (2026-11-21). Overflow path: 96/96 identical, including scores above 32,767.
- Kernel tests: 2,351 checks (SIMD = scalar on random databases, all policies × thread sets identical to sequential, traceback scores, calibration, and 4,011,861 traced SIMD cells identical to a textbook Gotoh matrix), clean under AddressSanitizer and UBSan. Engine interface: 481 checks. Web app: 86 checks (app results identical to the engine's own output).

## 5. Results (Swiss-Prot unless stated; medians; randomised order; 2 s all-core conditioning = "sustained")
| Experiment | Result |
|---|---|
| E1 speedup decomposition (query 400, 5 rounds) | scalar 0.71 → SIMD 9.00 GCUPS (**12.7×**) → SIMD + 10 threads 22.6 GCUPS (**2.52×** more), **31.9× total** |
| E2 thread sets (3 queries, 9 rounds) | one per physical core 23.7, all 12 CPUs 23.3 (tie), 8 E-cores 22.6, 2 P-cores 14.3, P-cores + hyperthreads 14.1, 1 thread 9.2 GCUPS |
| E3 mapping policies (9 rounds) | dynamic(1), stealing variants within 0–0.8% (ties); **static +30.9%**, **guided +48.3%** |
| E4 burst → sustained (from idle) | 10 cores: 32.0 → 19.0 GCUPS (**−41%**) about 17 s in; 1 thread −17% |
| E5/E7 tiling (5 rounds) | long queries (990) on 10 threads **1.54×** faster; ties at 150/400; one P-core thread on short queries slower (L=50 −27%) |
| E6 vs Parasail, one core (3 rounds) | HySW 1.50× faster at L=150, 1.13× at 400, 1.03× at 990 (tie), 1.12× overall |
| E8 calibration (30 runs) | chose one-per-physical-core 30/30; matches full-search burst ranking (39.0 / 37.8 / 30.7 / 19.3 GCUPS) |
Figures: `results/m6/fig_*.png`, `results/calibration/fig_e8_calibration.png`.

## 6. Analysis
- **SIMD** gives about 13×, not 16×: building the profile, loading columns and the overflow check cost time; a short query cannot amortise them.
- **Threads** give only 2.5× from 10 cores under sustained load: E-cores are slower than P-cores, hyperthreads share a core's vector unit (no gain), and the 15 W limit lowers every core's clock when all are busy (E4). In the burst regime the same change gives about 4× (E8b: 39 vs 9 GCUPS).
- **Mapping** matters more than core choice: equal static shares leave P-cores idle while E-cores finish; any dynamic or stealing policy balances them. Cost-aware and capacity-aware variants did not beat simple dynamic(1).
- **Memory:** the kernel is compute-bound for the database stream (~400 cells per byte) but its per-thread state was cache-bound for long queries, which tiling fixed.

## 7. Negative results and limitations
Tiling costs up to 27% for short queries on one thread; hyperthreads add nothing; calibration adds nothing measurable on this laptop; the Parasail comparison includes Python call overhead and is a yardstick, not a race; results are from one machine; no GPU or MPI was used (the laptop has no discrete GPU; OpenACC/CUDA in the syllabus are not covered); no energy measurement (would need root access to RAPL counters). Live numbers in the app are single runs and vary with power state (labelled on screen).

## 8. Reproduce
`./build.sh && tests/run_tests.sh` (correctness), `bench/run_m6.sh` then `scripts/make_m6_report.py` (≈2 h on AC power), `app/launch.sh` (app). Data: `scripts/get_swissprot.sh` (UniProt, CC BY 4.0).

## References (beyond the source register)
Smith & Waterman, J. Mol. Biol. 147:195–197 (1981); Gotoh, J. Mol. Biol. 162:705–708 (1982); Farrar, Bioinformatics 23(2):156–161 (2007) for Parasail's striped method. These three are standard citations given from memory: check the details before submission.
