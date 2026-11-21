# Project Options — Phase 1

> **Outcome superseded by `PROJECT_OPTIONS_COMPARISON.md` (Phase 1B, 2026-11-11).** Kept unchanged as the Phase 1 record. Note that the objections to Candidate 3 below (needs hand-written intrinsics; nothing tested) no longer hold.

Three candidates were considered after inspecting the laptop (hybrid 2P+8E Intel i3-1220P, 7 GB RAM, **no NVIDIA GPU**, GCC 15 + OpenMP working, no MPI/CUDA/OpenACC). Evidence references point to `FEASIBILITY_LOG.md` (FL) and `SOURCE_REGISTER.md` (S#).

## Candidate 1 — Capacity-aware, locality-preserving loop scheduling for hybrid (P-core/E-core) CPUs  ← **selected**

- **Problem:** OpenMP's default `static` loop schedule assumes equal cores. Most laptops sold since 2021 mix fast P-cores and slow E-cores, so fast cores sit idle while slow ones finish. `dynamic` fixes the balance but destroys cache locality on repeated loops. The programmer has to guess per loop.
- **User / use case:** anyone running OpenMP code on a hybrid laptop or desktop with stock GCC — including students doing this course's OpenMP lab on 12th-gen-or-later Intel machines.
- **Why parallelism matters:** the project *is* about mapping parallel work to unequal cores; the measured quantity is parallel loop time.
- **Course alignment:** Unit IV (decomposition, task characteristics, **mapping techniques for load balancing**, interaction overheads, work-pool model); Unit II (symmetric-multiprocessor assumptions, synchronisation, cache effects); Unit I (measuring and reporting performance, speedup on unequal cores); Unit III (loop-level parallelism, SIMD affects the P/E ratio). Lab: OpenMP.
- **Proposed contribution:** a small user-level scheduler for stock GCC OpenMP — contiguous blocks sized by measured per-thread speed, feedback across repeated executions, work stealing — plus a controlled comparative study against `static`/`dynamic`/`guided` on a hybrid x86 laptop, including an ablation and a throttling-aware benchmarking method.
- **Risks:** gains over a well-chosen stock schedule are modest (the win is robustness, not a big speedup); weights may add little beyond stealing; laptop power throttling makes measurement delicate; no GPU content.
- **Data / inputs:** self-generated kernels; optionally one public sparse matrix or image for an application kernel.
- **Evaluation:** per-loop time vs sequential P-core baseline, "regret" vs best stock schedule per kernel, per-thread idle time, exact output comparison.
- **Evidence so far:** FL §2–§5 — asymmetry 1.1–2.8×; static +21–94% off best; dynamic +22–116% off best on in-cache sweeps; prototype policy ≤5.5% off best on all four kernels.

## Candidate 2 — GPU-accelerated wildfire-spread cellular automaton (Numba-CUDA on Google Colab, CPU fallback)

- **Problem / user:** fast what-if simulation of fire spread over a terrain grid for planners; a stencil update repeated over many time steps.
- **Why parallelism matters:** each cell update is independent within a step → natural data parallelism on a GPU.
- **Course alignment:** Unit III (CUDA threads/blocks/grids, memory hierarchy), Unit V (Numba). Strongest match to the "GPU programming" half of the course title.
- **Contribution:** an end-to-end case study with CPU vs GPU comparison; little that is not already in tutorials.
- **Risks:** **no local NVIDIA GPU** → the core technology cannot be run or verified on available hardware; depends on a free cloud tier whose GPU availability is explicitly not guaranteed (S9) and needs the student's Google account; the CPU fallback is a memory-bound stencil, and memory-bound loops were measured *not to scale* on this laptop (FL §3, §4).
- **Evidence so far:** only that Numba has a Python 3.14 wheel (metadata check). Nothing executed.

## Candidate 3 — SIMD + OpenMP Smith–Waterman protein database search

- **Problem / user:** exact local alignment of a query protein against a sequence database for biologists.
- **Why parallelism matters:** compute-bound dynamic programming; vectorises (AVX2) and parallelises across sequences; irregular sequence lengths make load balancing relevant.
- **Course alignment:** Unit III (SIMD), Unit II/IV (threads, mapping). Would run fully on the laptop.
- **Contribution:** very hard to differentiate — inter-sequence SIMD search (SWIPE, S10) and a tuned SIMD library (Parasail, S11) have existed for a decade; a student version would be a re-implementation.
- **Risks:** hand-written AVX2 intrinsics are error-prone and harder to explain in a viva; dataset download (~90 MB class) and licence check still needed.
- **Evidence so far:** spike 1 shows SIMD code has the largest P/E gap (2.8×), so it would work as a *workload*, but nothing specific to alignment was tested.

## Comparison (same criteria for each; High = favourable)

| Criterion | C1 Hybrid-CPU scheduler | C2 GPU wildfire (Colab) | C3 SIMD Smith–Waterman |
|---|---|---|---|
| Practical relevance | High — affects every OpenMP run on hybrid CPUs | Medium | Medium–High |
| Clarity/honesty of contribution | **High** — specific gap, measurable | Low — tutorial-like | Low — saturated prior art |
| Course alignment | Units I, II, IV (+III partly); OpenMP lab. **No GPU** | **Units III, V; GPU** | Units II, III |
| Feasible on available hardware | **Yes, verified** | **No** (cloud only, untested) | Yes (untested) |
| Implementation effort | Moderate (~500 lines core + harness) | Moderate | High (intrinsics) |
| Measurable performance | Verified in spikes | Unknown | Likely |
| Data / tool availability | Nothing to install for the core | Cloud account, external GPU | Dataset download |
| Demonstration quality | Good — per-core timeline, live comparison on the laptop | Good visuals, but cloud-dependent | Plain numeric output |
| Risk | Medium (modest gains, noisy laptop) | **High** (unverifiable core) | Medium (weak originality) |

## Outcome

**Candidate 1 is selected.** It is the only candidate whose central claim was tested on the real hardware in Phase 1, it has a specific and honestly stated gap relative to prior work, and it turns the laptop's main limitation (no GPU, unequal cores) into the subject of the study. See `PROJECT_DECISION.md`.
