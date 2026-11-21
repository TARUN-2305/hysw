# Project Options Comparison — Phase 1B

Three strongest directions after domain exploration and application spikes. Numbers are from `APPLICATION_FEASIBILITY_RESULTS.md` (fair-ordering runs unless stated). The Phase 1 comparison (`PROJECT_OPTIONS.md`) is preserved; this file supersedes its outcome.

## The three directions

**D1 — Exact protein database search with two-level parallelism (SIMD + threads) on a hybrid laptop CPU.**
Smith–Waterman search of a query against a protein database; 16 sequences per SIMD batch in portable C; batches distributed over threads; study of core selection, batch mapping and power-limited behaviour.

**D2 — Flood inundation simulation on Bengaluru terrain with wet-front-aware work distribution.**
Explicit 2-D shallow-water solver on SRTM terrain; contiguous blocks sized by wet-cell counts plus stealing; study of scheduling across flood stages.

**D3 — The original Candidate 1: a general capacity-aware loop scheduler for hybrid CPUs,** demonstrated on synthetic kernels plus a few application loops.

(Graph ranking was spiked and dropped: 1.6–2.1× speedup and no scheduling sensitivity. See `REAL_WORLD_DOMAIN_EXPLORATION.md`.)

## Evaluation

| Criterion | D1 Protein search | D2 Flood simulation | D3 General scheduler |
|---|---|---|---|
| **Real-world relevance, identifiable users** | **High.** Biologists searching curated protein databases; output is exactly what established exact-search tools produce | **High** in motivation (Bengaluru floods; an agency runs a flood model operationally) but our model would not be one they could trust | Low. "People who run OpenMP loops" |
| **Concept ↔ application fit** | **Strong, at two levels.** Independent sequences ↔ SIMD lanes; variable-length sequences ↔ thread load balancing; and the two interact (sorting for SIMD breaks static scheduling) | **Strong at one level.** Moving wet region ↔ dynamic mapping | Weak. The application is an afterthought |
| **Originality / defensibility** | Modest and honest: no new algorithm; portable vectorisation matching a hand-tuned library, plus a power-limited hybrid-CPU study we did not find elsewhere | Modest: cost-hint blocks gave 7–9% over the best stock schedule, but closely related domain work exists and is unread; hybrid-awareness itself added nothing | **Weak after 1B.** Capacity weighting showed no benefit; the broad Alder Lake study already exists (S14) |
| **Parallel-computing depth** | **High.** Data-level + thread-level parallelism, decomposition choice (inter- vs intra-sequence), cache footprint vs query length, load balancing, core selection, power | Medium. Thread-level, load balancing, locality; SIMD possible but untested and branch-heavy | Medium. Scheduling only |
| **Syllabus alignment** | Unit III (SIMD, loop-level parallelism — the data-parallel half of the course, without needing a GPU), Unit II (threads, shared memory), Unit IV (decomposition, mapping, load balancing), Unit I (performance measurement) | Units IV, II, I | Units IV, II, I |
| **Feasibility on this hardware** | **Verified.** Runs today; data are small; correctness oracle installs without admin rights | Verified as a workload; numerical validation is extra domain work | Verified |
| **Measurable improvement** | **~12.5× from SIMD on one core; ~19–21× sustained and ~38× burst over scalar sequential (derived).** Default schedule 23–47% slower than any dynamic one. Core choice worth 35% | ~2× sustained, ~4× burst. Default schedule ~30% slower; custom blocks 7–9% better than best stock in one scenario | 0–2% over stock `dynamic`/`steal` on real loops |
| **Implementation and demo complexity** | Low–medium. ~150-line kernel already prototyped. Demo: search a real protein against a real database, scalar vs SIMD vs parallel, per-core timeline | Medium–high. Numerics needed a stability fix already; validation cases and visualisation add work | Low |
| **Presentation potential** | Good: a clear before/after ("minutes to seconds"), exact agreement with a known tool, and a surprising hardware story (P-cores-only is the worst choice; why 12 threads give 1.5×) | **Best visuals** (flood map over the city) but weak speedup numbers and a model that invites "is this realistic?" | Dry |
| **Student's ability to defend it** | Good. Dynamic programming is core CS; SIMD lanes and scheduling are course material | Harder. Shallow-water numerics, friction, stability and calibration are outside the course | Good |
| **Main risk** | Looks like a familiar student topic unless the hybrid/power study and the Parasail parity are front and centre | Credibility of the flood model; modest numbers | Thin contribution |

## Decisive evidence

1. **The scheduler alone does not justify a project.** Under fair measurement, capacity weighting was within noise everywhere, and in protein search every dynamic-style policy tied.
2. **D1 has a second, much larger source of speedup** that the others lack, and it was demonstrated end-to-end with exact correctness against an independent library.
3. **D2's advantage is visual and local; its disadvantage is scientific credibility** and a speedup ceiling of about 2× sustained on this laptop.
4. **D1 is the direction the student identified as having the right kind of purpose,** and the experiments removed the two objections raised against it in Phase 1: hand-written intrinsics are not needed, and the hybrid CPU does provide an interesting, measurable angle (core selection and power-limited behaviour) — though not the angle originally expected (smart scheduling).

## Recommendation

**Build D1.** Keep the Phase 1 scheduler inside it as one compared batch-mapping policy and as a reported negative result. Keep D2 as documented runner-up; its spike and data stay in the repository.

Details: `FINAL_PROJECT_PROPOSAL.md` and `PHASE_2_BLUEPRINT_V2.md`.
