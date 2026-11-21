# Phase 2 Handoff — start here

For a fresh session beginning Phase 2. Research is finished; do not reopen project selection unless implementation reveals a material flaw (then record it in `DECISIONS.md`).

## What we are building
**HySW — exact protein database search with two-level parallelism (SIMD + OpenMP threads), delivered as a local web app**, on the student's hybrid laptop (Intel i3-1220P: 2 P-cores with hyperthreading + 8 E-cores, 7 GB RAM, no NVIDIA GPU, Ubuntu 26.04, GCC 15.2).

## Read in this order
1. `CLAUDE.md` — standing instructions. **Exception:** §6's "no web frontend" rule is overridden for this project (`DECISIONS.md` D1, student-approved).
2. `DECISIONS.md` — D0 project choice, D1 web app.
3. `research/FINAL_PROJECT_PROPOSAL.md` — problem, objectives, contribution, claims not to make, risks.
4. `research/APP_DESIGN.md` — users, screens, inputs/outputs, flow, engine JSON interface, tech choices.
5. `research/PHASE_2_BLUEPRINT_V2.md` — modules, algorithms, tests, benchmark method, milestones M1–M7.
6. Only if needed: `research/APPLICATION_FEASIBILITY_RESULTS.md` (measured evidence), `research/SOURCE_REGISTER.md` (citations).

## Already verified (do not re-research)
- GCC OpenMP builds and runs from the NTFS project folder; thread pinning works without root.
- No `make`, `cmake`, system `pip`, MPI or CUDA. Use `build.sh` and a project venv (`python3 -m venv`; `ensurepip` works). `parasail` 1.3.4, numpy, matplotlib and numba have Python 3.14 wheels; parasail was installed and used successfully.
- Prototype kernels: `research/spikes_1b/swsearch.c` (scalar) and `swsimd.c` (16 lanes × 16-bit, plain C + `#pragma omp simd`). GCC reports the lane loop "vectorized using 32 byte vectors". SIMD = 12.3–12.7× scalar on one core; scores identical to scalar and to Parasail (13,209/13,209).
- Gap convention: cost 11 + k for a gap of k residues = Parasail open 12 / extend 1. Matrix: `research/data/ncbi_blosum62/BLOSUM62`.
- Test database: `research/data/uniprot_ecoli_k12/ecoli_k12_reviewed.fasta` (4,403 proteins). Swiss-Prot FASTA is ~94 MB compressed (size checked, licence not yet read, not downloaded).
- Mapping: default `static` is 23–47% slower on length-sorted batches; all dynamic-style schedules tie. Best thread set measured: one per physical core (10), ~35% faster than P-cores only.
- Machine limits: sustained package power 15 W (28 s window), platform profile `balanced`. Expect ~1.5–1.7× from threads on top of SIMD in long runs, ~3× in short bursts.

## Pitfalls already hit — avoid repeating
- **Benchmark ordering:** never run configurations in a fixed or rotated cyclic order. Use a fresh random order each round plus the same short all-core conditioning load before every measured run (helpers `hs_pick` and `hs_condition` in `research/spikes_1b/hsched.h`). Label burst vs sustained.
- Synthetic floating-point kernels exaggerate P/E asymmetry; judge on the real kernel.
- RAPL energy and perf counters are not readable unless the student runs (each reset on reboot): `sudo chmod a+r /sys/class/powercap/intel-rapl:0/energy_uj` and `sudo sysctl kernel.perf_event_paranoid=2`. The student offered to enter the sudo password in the terminal when asked; ask first, never run sudo yourself.

## Order of work
Engine first (M1–M5, verified against Parasail), then the full study (M6), then the app (M6a, M6b), then submission materials (M7). Keep the engine independently testable; the UI only displays engine output, and every animation must come from real engine data.

Optional, only if the student asks: keep the benchmark harness portable (one command, logs power profile and energy) so it can be run on classmates' hybrid laptops, which is the route to a possible paper (see the publishability discussion: needs multiple machines, energy data and an automatic thread-set selector).
