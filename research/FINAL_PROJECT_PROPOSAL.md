# Final Project Proposal — Phase 1B outcome

Supersedes the selection in `PROJECT_DECISION.md` (kept for the record). Evidence: `APPLICATION_FEASIBILITY_RESULTS.md`; prior art: `NOVELTY_AND_PRIOR_ART_COMPARISON.md`.

## Proposed title

**Exact Protein Database Search on a Power-Limited Hybrid Laptop CPU: Two-Level Parallelism (SIMD + Multithreading) for Smith–Waterman**

Short name for the code: `hysw` (working name).

## Problem statement

A biologist with a new protein wants to know which known proteins it resembles. The exact method, Smith–Waterman local alignment, compares the query against every database sequence by filling a table of *query length × database residues* cells. On one core in straightforward C this ran at 0.77 billion cells per second on the student's laptop — on the order of minutes for one query against a curated database of a few hundred thousand proteins — which is why exact search is often replaced by faster approximate tools.

Commodity laptops now have vector units and a mix of fast and slow cores, but using them well for this workload is not automatic:

- vectorising across sequences requires sorting them by length, and that sorting makes the default thread schedule 23–47% slower (measured);
- the fast cores, the slow cores and hyperthreads contribute very differently, and the vendor's simple advice of "fast cores only" was the slowest choice measured (35% behind the best);
- the laptop's 15 W sustained power limit means twelve threads add only about 1.5–1.7× on top of a vectorised single core, while short runs see about 3×;
- naive benchmarking on such a machine gives wrong rankings — this happened twice during Phase 1.

**Intended user:** someone who needs exact protein similarity search on an ordinary laptop, and — for the study — anyone who has to run or measure vectorised parallel code on a power-limited hybrid CPU.

## Objectives

1. **Build a correct exact-search tool.** Query FASTA + database FASTA → ranked hits with scores (and alignments for the top hits). Every score verified against an independent established library.
2. **Exploit data-level parallelism portably.** Sixteen database sequences per batch, one per SIMD lane, in plain C that the compiler vectorises; no hand-written intrinsics in the baseline design.
3. **Exploit thread-level parallelism sensibly.** Distribute length-sorted batches across cores; compare stock schedules, block-plus-stealing and cost/capacity-aware mapping; choose a default on evidence.
4. **Characterise the hybrid, power-limited machine for this workload.** Contribution of P-cores, E-cores and hyperthreads; best thread set; burst versus sustained behaviour; effect of query length on cache footprint; energy per search if the student enables energy readings.
5. **Measure without artefacts.** A benchmarking procedure (randomised order, conditioning load, regime labelling) that removes the two pitfalls found in Phase 1/1B, documented so others can reuse it.
6. **Report honestly,** including what did not help (capacity-aware scheduling) and where the hardware limits the result.

## Engineering contribution

| # | Contribution | Why it is defensible |
|---|---|---|
| C1 | An exact Smith–Waterman database search engine in portable C with compiler-vectorised inter-sequence SIMD and multithreading | Prototype exists: 12.3–12.7× over scalar on one core; one-thread speed within ~4% of Parasail on the test workload; 13,209/13,209 scores identical to Parasail |
| C2 | A design and measurement of how the two levels of parallelism interact (length-sorted batching vs thread load balance) and which batch-to-thread mapping is robust | Measured: default `static` 23–47% slower; all dynamic-style mappings tie |
| C3 | A core-selection study for a 2P+8E, 15 W laptop CPU, turned into a simple automatic choice of thread set | Measured: 10 threads > 12 > P-only by 5% and 35% |
| C4 | A reusable benchmarking method for power-limited hybrid laptops, with the evidence of what goes wrong without it | Two artefacts found, explained and removed in this project |
| C5 | A negative result: capacity-aware, feedback-driven scheduling (the Phase 1 idea) does not beat stock `dynamic` on this workload, with the reason | Measured in three applications |

**Not claimed:** any new algorithm; superiority over existing tools beyond same-machine comparisons actually run; that this has never been studied on hybrid CPUs.

## Scope

**In:** protein sequences; score-only search with exact scores; 16-bit lanes with overflow detection and scalar re-scoring; alignments for the top hits by scalar traceback; database preprocessing into a length-sorted binary form; thread-set selection; benchmark harness; comparison with Parasail on the same machine; report, case study, slides, demo script.

**Stretch (only if the core is finished and measured):** 8-bit lanes (32 per vector) with fallback; "performance" power profile as an extra experimental condition; energy per search; a short written mapping of the design onto a GPU (lanes ↔ GPU threads) for the case study.

**Application (added 2026-11-20, `DECISIONS.md` D1):** a local web app — Search screen (query in, ranked hits, alignments, 3D structure of the selected hit out) and Performance Lab (live race, per-core view, SIMD view, measured charts). Design: `APP_DESIGN.md`.

**Out:** nucleotide search, heuristic pre-filters, statistics such as E-values, multiple alignment, MPI, GPU code, user accounts or remote hosting.

## Syllabus mapping

| Unit | What the project exercises |
|---|---|
| III — Data-level parallelism | SIMD vectorisation, loop-level parallelism, what makes a loop vectorisable, lane width vs precision. The same decomposition is what a GPU version would use, which the case study can discuss |
| II — Thread-level parallelism | Shared-memory threads, hyperthreading, per-thread buffers, cache effects |
| IV — Parallel algorithm design | Decomposition (inter- vs intra-sequence), task characteristics (variable-length sequences), mapping and load balancing, overheads |
| I — Performance | Speedup decomposition, measurement practice, power as a limit on performance |

No CUDA, MPI or OpenACC code. The lab's Part B permits any parallel programming concept; this should still be confirmed with the faculty.

## What happens to earlier work

- **Phase 1 scheduler:** kept as one compared mapping policy and as the documented negative result (C5). Its synthetic kernels remain in `research/spikes/` for controlled experiments.
- **Flood model:** runner-up; spike, data and results stay in `research/spikes_1b/` and `research/data/`.

## Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Seen as a familiar "parallel Smith–Waterman" exercise | Lead with what is specific: portable vectorisation at library speed, exactness proof, the hybrid/power-limit findings, the measurement method |
| R2 | Thread scaling looks poor (1.5–1.7× sustained) | It is a finding, explained by the measured 15 W limit; report burst and sustained separately; optionally test the "performance" profile |
| R3 | Compiler vectorisation is fragile across compiler versions or flags | Build-time check that fails if the kernel loop is not vectorised; keep the scalar path as fallback and reference |
| R4 | 16-bit score overflow on very long sequences | Detect saturation risk per lane and re-score those sequences with the scalar kernel; test with artificial long sequences |
| R5 | Database download (94 MB compressed) or licence issue | Size checked; licence text to be read first; the small proteome already in the repository is the fallback |
| R6 | No GPU content | Confirm acceptability with faculty early; the SIMD work covers the data-parallel concepts |
| R7 | One machine | Stated limitation; optional run on a second hybrid machine if available |

## Open points for the student

1. Faculty view on a CPU-only project, and the meaning of "program-specific requirements" in the rubric.
2. Deadline, team size, report format.
3. Optional: enable energy readings and counters (two commands, reset on reboot); optional: allow runs under the "performance" power profile.
