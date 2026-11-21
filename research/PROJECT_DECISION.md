# Project Decision — Phase 1

> **Superseded by Phase 1B (2026-11-11).** The selection below is kept unchanged as the Phase 1 record. Application experiments and a missed prior-art paper changed the recommendation: see `FINAL_PROJECT_PROPOSAL.md`, `PROJECT_OPTIONS_COMPARISON.md` and `NOVELTY_AND_PRIOR_ART_COMPARISON.md`.

## Selected project

**HybridFor — a capacity-aware, locality-preserving loop scheduler for hybrid (P-core/E-core) CPUs on stock GCC OpenMP, with a controlled comparative study.**

(Working name; rename freely.)

## Problem statement

On CPUs that mix fast and slow cores, OpenMP's default `static` schedule leaves fast cores idle, while `dynamic` repairs the balance at the cost of cache locality and dispatch overhead. With stock GCC there is no schedule that is safe for both situations, and the operating system does not tell a program how much faster one core is than another for a given loop. On the student's 2P+8E laptop each stock schedule was 31–116% slower than the best alternative on at least one of four test loops (exploratory measurement, `FEASIBILITY_LOG.md` §5b).

**Intended user:** a programmer or student running OpenMP loops on a hybrid laptop/desktop with the system compiler, who should not have to diagnose each loop to choose a schedule.

## Bounded contribution

1. **A small C library** (one header, one source file) providing a parallel-for that
   - gives each thread a contiguous block sized by its *measured* speed on that loop,
   - remembers those speeds per loop and refines them every time the loop runs again,
   - lets a thread that runs out steal from the fullest remaining block,
   - works with unmodified GCC and libgomp (it uses OpenMP for threads and locks only).
2. **A comparative study** on a real Intel hybrid CPU against `static`, `dynamic` (two chunk settings) and `guided`, across uniform, irregular, cache-sensitive and memory-bound loops, reported as speedup over a sequential P-core baseline and as *regret* against the best schedule per loop.
3. **An ablation** answering the open question from Phase 1: how much comes from weights, how much from stealing, how much from feedback.
4. **A benchmarking method that survives laptop power throttling** (interleaved rotation, regime labelling), with the evidence that naive ordering reverses conclusions.

### What is and is not claimed

- Not a new algorithm. Speed-proportional splitting (AID, S1), static-plus-steal (LLVM, S7) and timing feedback (Bull, S5) all exist.
- The differentiation: those ideas combined at user level for stock GCC, and measured on a real P+E CPU with hyperthreading, cache-sensitive repeated loops and power throttling in view. AID required a patched GCC 8 runtime and was evaluated on ARM and an emulated asymmetric Xeon (S1, partly from an automated summary — to be re-verified).
- Expected benefit is **robustness** (never far from the best schedule), not a large speedup over a well-tuned stock schedule.

## Scope

**In scope:** the library; four to six benchmark kernels (uniform compute, Mandelbrot, small and medium Jacobi, a memory-bound case kept as a negative result, and one application-style iterative kernel such as k-means); correctness tests; benchmark harness; plots; report, case study, slides, demo script.

**Out of scope:** modifying GCC or libgomp; nested parallelism, tasks, `collapse`, `ordered`; non-Linux systems; GPU, MPI or OpenACC code; automatic thread-count selection (may be reported as an observation only); energy measurement unless the student enables RAPL access.

## Why this candidate

- It is the only candidate whose central claim was **tested on the available hardware** in Phase 1.
- The gap relative to prior work is specific and checkable.
- It needs no installs for the core work and no cloud service.
- It maps to named syllabus topics (Unit IV mapping and load balancing; Unit II shared-memory and synchronisation; Unit I performance reporting).
- It is explainable: the core is roughly 150 lines of scheduling logic the student can walk through line by line.
- The demonstration is concrete: a per-core timeline showing P-cores idle under `static`, cache thrash under `dynamic`, and neither under HybridFor, live on the laptop.

## Why the alternatives were rejected

- **GPU wildfire simulation on Colab:** the laptop has no NVIDIA GPU, so the core of the project could not be executed or verified here; free cloud GPUs are explicitly not guaranteed (S9); and the CPU fallback is a memory-bound stencil of the kind measured not to scale on this laptop.
- **SIMD Smith–Waterman search:** feasible, but mature tools have covered it for over a decade (S10, S11), leaving a re-implementation with no defensible contribution, and intrinsics code is harder to defend in a viva.

## Risks and mitigations

| # | Risk | Likelihood | Mitigation / fallback |
|---|---|---|---|
| R1 | Rigorous benchmarks show HybridFor is not better than simply using `dynamic` with a good chunk everywhere except small stencils | Medium | The study remains valid as a comparative case study with an honest negative result; the practical guideline ("which schedule when, on hybrid CPUs") is still a deliverable |
| R2 | Weights add nothing beyond stealing | Medium | The ablation reports it. The library then simplifies to static+steal for GCC, which GCC still lacks |
| R3 | Timing noise and throttling blur differences of a few percent | High | Interleaved rotation, many rounds, medians with spread, both power regimes reported, differences under the noise floor called ties |
| R4 | Faculty expect GPU/CUDA content | Medium | Confirm with faculty early. Lab Part B allows any parallel programming concept. If required, a small optional Colab appendix can be added without changing the core — only on the student's instruction |
| R5 | Concurrency bug in range splitting (lost or repeated iteration) | Medium | Exactly-once stress tests over random sizes, thread counts and weights; simple lock-based design before any lock-free optimisation |
| R6 | Results specific to one CPU | Certain | Stated as a limitation; optional second data point if the student has access to another hybrid machine |
| R7 | No cache-miss or energy evidence to explain results | Certain without sudo | Explanations labelled as hypotheses; optional one-time `sudo` step by the student to enable `perf`/RAPL |

## Unverified assumptions (visible, not hidden)

- That AID has no stealing or cross-execution feedback (from an automated summary of S1).
- That GCC 15's libgomp still has no stealing schedule (S6 is trunk source comments via search extract).
- That no published hybrid-x86 loop-schedule study exists (two searches only).
- That the throttling step is the PL2→PL1 power limit (inferred from S8, not measured).
- That the Phase 1 ranking holds with more kernels and rounds.

## Questions for the student (none block Phase 2)

1. Submission deadline, team size and any required report/slide format?
2. Does the faculty require GPU content, and what does "program-specific requirements (10 marks)" mean?
3. Are you able to run two `sudo` commands once (to allow `perf` counters and RAPL energy readings)? Optional.
4. Is a second hybrid machine (a friend's laptop or lab PC) available for a confirmation run? Optional.
