# Real-World Domain Exploration — Phase 1B

Goal: find a computational problem with real users where the parallel-computing work and the problem genuinely belong together — not an application bolted onto the Phase 1 scheduler.

Evidence labels: **[measured]** = run on the student's laptop in Phase 1 or 1B (details in `APPLICATION_FEASIBILITY_RESULTS.md` and `FEASIBILITY_LOG.md`); **[source S#]** = `SOURCE_REGISTER.md`; **[reasoned]** = judgement, not tested.

## 1. What the laptop rewards and punishes (from measurements)

These facts filtered the domains before any reading was done.

| Fact | Evidence | Consequence for domain choice |
|---|---|---|
| Memory-bound loops do not scale; one P-core nearly saturates memory bandwidth | Phase 1: triad and large Jacobi slower on 12 threads than on 1 | Prefer workloads whose hot data fits in cache or that do a lot of arithmetic per byte |
| Sustained all-core load is power-throttled; a single thread is not | Phase 1 §5a; flood spike: 4.2× in a short run, 2.3× in a long one | Thread speedups of 2–4× are the honest ceiling. Large gains must also come from somewhere else (SIMD) |
| AVX2 works and the compiler can use it | Phase 1 spike 1; 1B SIMD spike | Data-level parallelism is available on this machine |
| Two hyperthreads on a P-core are, for dense arithmetic, each about as fast as one E-core | 1B: measured per-thread speeds in three applications | With all 12 logical CPUs the "fast vs slow core" gap largely disappears; it reappears (≈1.3–1.5×) with one thread per physical core |

## 2. Domains surveyed

| # | Domain / problem | Users | Why parallel | Fit to this hardware | Status |
|---|---|---|---|---|---|
| A | **Protein sequence database search** (Smith–Waterman) | Biologists looking for related proteins | Millions of independent alignments; each is a small dynamic-programming table | Compute-bound, cache-resident, integer SIMD, variable-length tasks | **Spiked** — strongest |
| B | **Urban flood inundation simulation** (Bengaluru terrain) | Disaster-management agencies, hydrologists | Stencil over a grid, thousands of time steps, many scenarios | Work concentrated in wet cells that move over time; moderately compute-bound | **Spiked** — strong fit for scheduling, weak speedup |
| C | **Web/network graph ranking** (PageRank) | Search, network analysis | Same irregular loop every iteration | Random memory reads; latency-bound | **Spiked** — rejected |
| D | Molecular dynamics (GROMACS/LAMMPS-style) | Chemists, materials scientists | Force loop every time step; spatial decomposition | Compute-bound and repeated — attractive — but a credible mini-MD (neighbour lists, thermostat, validation) is a large build, inputs are synthetic lattices rather than data, and production codes already handle hybrid CPUs by advising P-cores only [source S29, forum] | Not spiked [reasoned] |
| E | Sparse solvers for engineering (FEM, SpMV/CG) | Structural and CFD engineers | SpMV repeated hundreds of times | Memory-bandwidth-bound for realistic matrices [measured, Phase 1 triad]; heterogeneity-aware SpMV on these exact CPUs is already published [source S16] | Not pursued |
| F | Medical image reconstruction / image pipelines | Radiologists, microscopists | Per-pixel or per-ray work | Mostly uniform cost and memory-streaming; scheduling is not interesting, and the student asked not to default to imaging | Not pursued [reasoned] |
| G | Monte Carlo (dose calculation, option pricing, path tracing) | Medical physicists, quants, artists | Independent samples | Scales, but `dynamic` scheduling already solves it; the parallel part would be tutorial-level | Not pursued [reasoned] |
| H | Signal processing (FFT, radio astronomy, audio) | Engineers, astronomers | Batched transforms | Dominated by mature libraries (FFTW); little room for a student contribution | Not pursued [reasoned] |
| I | Transportation (shortest paths, traffic simulation) | Planners | Graph traversal | Same memory-latency profile as C | Not pursued (see C) |

Entries D–I were rejected on reasoning plus Phase 1 measurements; they were not individually researched in depth.

## 3. The three spiked directions in detail

### A. Protein sequence database search

1. **Who has the problem?** Biologists and bioinformaticians who have a protein and want to know what it resembles — the first step in guessing function, finding homologues, annotating a genome.
2. **What are they trying to do?** Compare one query protein (or thousands) against every sequence in a reference database such as UniProtKB/Swiss-Prot (~570,000 curated proteins) and rank the best local alignments.
3. **What is expensive?** Smith–Waterman is the exact method: it fills a table of *query length × database residues* cells. One 400-residue query against Swiss-Prot is roughly 80 billion cell updates. The scalar spike managed 0.77 billion cells per second on one P-core **[measured]** — minutes per query, which is why faster heuristics (BLAST) became the default and exact search is often skipped.
4. **Why parallel computing?** Two independent levels: (i) *data-level* — sixteen database sequences can be aligned in lock-step, one per SIMD lane; (ii) *thread-level* — batches are independent. The spike reached 12.3–12.7× from SIMD alone on one core with scores identical to the scalar code and to an independent library **[measured]**; threads added about 1.5–1.7× on top in sustained runs and about 3× in a short burst **[measured]**.
5. **What is interesting on a hybrid CPU?** SIMD efficiency requires sorting sequences by length; sorted order makes thread workloads very unequal (static scheduling was 23–47% slower than the best schedule **[measured, fair ordering]**). The two levels of parallelism therefore interact, and the mapping of batches to unequal cores is a real design decision.
6. **What can we improve or investigate?** How close plain, compiler-vectorised C gets to hand-written intrinsics libraries; which batch-to-thread mapping is robust; how much each of SIMD, P-cores, E-cores and hyperthreads contributes; burst vs sustained performance.
7. **Credible local demonstration?** Yes. Data are small and public (Swiss-Prot ≈ 94 MB compressed; the spike used the 4,403-protein *E. coli* proteome). Correctness is exact and checkable against an independent library (Parasail has a pip wheel).

### B. Urban flood inundation simulation

1. **Who?** Disaster-management and civic agencies. For Bengaluru specifically, IISc developed an urban flood model that the Karnataka State Natural Disaster Monitoring Centre runs with real-time rainfall **[source S23]**.
2. **Goal?** Predict where water will go and how deep, fast enough to warn people, and across many rainfall scenarios.
3. **What is expensive?** Explicit 2-D shallow-water solvers update every cell face every time step; the time step shrinks with cell size, so finer grids cost roughly the cube of the resolution. Thousands of steps per simulated hour.
4. **Why parallel?** Each step is two independent loops over the grid; OpenMP is the established approach for this model family **[source S18]**, and published work reports that load imbalance limited its speedup **[source S18]**.
5. **What is interesting on a hybrid CPU?** Only wet cells cost anything, production codes deliberately skip dry ones **[source S19]**, and the wet region moves. So the work is concentrated, irregular and time-varying — the textbook case for dynamic mapping. Balancing threads under wet/dry imbalance is a published topic in this domain **[source S20, S22]**.
6. **What can we improve or investigate?** Scheduling policy per flood stage; contiguous-block-plus-stealing versus stock schedules; choice of thread set.
7. **Credible local demonstration?** Partly. The spike ran on real SRTM terrain for Bengaluru, conserved mass exactly and was bit-identical across schedules **[measured]**. But a 30 m surface model without drains or buildings is *not* a credible urban flood forecast, and validating the numerics is domain work outside the course.

### C. Graph ranking (PageRank)

1. **Who?** Web search, recommendation, network science.
2. **Goal?** Rank nodes of a large graph by repeated propagation.
3. **Expensive?** Every iteration reads one value per edge from effectively random memory locations.
4. **Why parallel?** Vertices are independent within an iteration.
5. **Interesting on a hybrid CPU?** In principle: irregular degrees, repeated loop, cost known in advance.
6. **Investigate?** Edge-balanced, capacity-aware partitioning.
7. **Local demonstration?** Ran on the real `web-Google` graph, but 12 threads gave only 1.5–2.1× over one P-core and most schedules were within a few percent of each other **[measured]**. The workload is limited by memory latency on this laptop, the scheduling question has little effect, and the area is saturated with mature frameworks. **Rejected.**

## 4. What the exploration says about Candidate 1

- **Does real work benefit from giving P-cores and E-cores different amounts?** Not measurably. In none of the three applications did speed-proportional blocks beat equal blocks once stealing was present. The Phase 1 result that motivated it came from a synthetic loop that hyperthreading happens to accelerate unusually well.
- **Do different stages need different scheduling?** Yes, in the flood model: with rain everywhere all schedules tied; with localised inflow, schedules differed by up to 73%.
- **Does locality-preserving assignment matter?** Somewhat: one-iteration-at-a-time `dynamic` was consistently worse than contiguous blocks in the flood model.
- **Is feedback across executions useful?** Not shown in any application. (The share-based feedback of the Phase 1 prototype also measures the wrong thing on irregular loops: who did how many iterations, not who is how fast.)
- **Does stealing help?** Yes — it is the one ingredient that was at or near the best in every application.
- **Do gains survive end-to-end?** Only in the flood model's localised scenario (about 7–9% over the best stock schedule, about 30% over the default). In protein search every dynamic-style schedule tied; the only large effect was avoiding the default `static`.

**Conclusion:** the scheduler is a useful *component* (contiguous blocks + stealing, which GCC's runtime does not provide), but its headline idea — capacity awareness — did not earn its place on real workloads. It should not be the project.
