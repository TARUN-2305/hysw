# Slide content (12 slides). Figures are in results/m6/ and results/calibration/.

1. **HySW: exact protein search on a laptop.** Title; student name, CS372IA; one line: "Smith–Waterman over 576,073 proteins, SIMD + threads, measured."
2. **The problem.** One protein, find similar known proteins exactly. 30.8 billion score cells per query; scalar C on one core: about 43 s (147-residue query). BLAST is faster but heuristic.
3. **The machine.** i3-1220P: 2 P-cores (+HT), 8 E-cores, AVX2, 15 W long-term limit. Why hybrid cores make scheduling interesting.
4. **Level 1: SIMD.** 16 length-sorted proteins per 256-bit register; plain C + `#pragma omp simd`; overflow lanes re-scored in 32-bit. Figure: Inside SIMD screenshot from the app.
5. **Correctness first.** 1,152,146/1,152,146 scores identical to Parasail; 2,351 kernel checks under ASan/UBSan.
6. **Speedup decomposition.** `fig_e1_speedup.png`: 0.71 → 9.00 → 22.6 GCUPS, 12.7× × 2.52× = 31.9×.
7. **Which cores?** `fig_e2_threadsets.png`: physical cores = all 12 (tie); hyperthreads add nothing.
8. **Scheduling.** `fig_e3_timeline.png` + `fig_e3_policies.png`: static +31%, dynamic balances P and E cores.
9. **The power wall.** `fig_e4_regime.png`: −41% after about 17 s; why 10 cores give 2.5×, not 10×.
10. **A cache fix found by measuring.** `fig_e7_tiling.png`: tiling, 1.54× on long queries; cost on short ones.
11. **The app.** Screenshot: search, alignment, 3D structure, Performance Lab "Before and after".
12. **Lessons and limits.** Exact search in about a second; dynamic scheduling wins on hybrid CPUs; measure under controlled power. Limits: one machine, no GPU/MPI, Parasail comparison via Python.
