# Novelty and Prior-Art Comparison — Phase 1B

Source IDs refer to `SOURCE_REGISTER.md`. "Read" states how well each source is known; claims about sources that were only seen as search extracts are marked †.

## 1. Correction to Phase 1

Phase 1 said no peer-reviewed evaluation of OpenMP loop scheduling on Intel Alder Lake had been found. **One exists and was missed:** the PMCSched journal paper (S14). It is the most relevant prior work for the original Candidate 1 and it narrows what that candidate could claim.

## 2. Heterogeneous-CPU scheduling: mechanisms compared

| Work | Level | Mechanism | Handles unequal cores | Handles irregular iterations | Stealing | Memory across loop executions | Evaluated on | Read |
|---|---|---|---|---|---|---|---|---|
| **AID** (S1, 2020) | Inside libgomp + GCC patch | Sampling phase at loop start measures big-to-small speed factor; iterations split in that proportion (AID-static), or 80% so and the rest dynamically (AID-hybrid), or dynamic with larger chunks for big cores (AID-dynamic) | Yes | Partly (hybrid/dynamic variants) | No† | No† | ARM Odroid-XU4; emulated asymmetric Xeon | Partly direct, partly summary; mechanisms confirmed by S14 |
| **Flexible AID / PMCSched** (S14, 2023) | GCC 11.2 runtime + Linux kernel module | AID without fixed thread pinning (runtime and OS share per-thread core type and priority through shared memory); new AID-guided; automatic fallback to AID-static when iterations are very short; OS scheduler that places threads using speed-factor predictions | Yes | Partly | No (not found in full-text search) | No per-loop history (history tables are used for the OS's speed-factor prediction, not for loop partitioning) | **Intel i9-12900K (8P+8E), Turbo off**; NAS, PARSEC, Rodinia, SPEC | **Full text** |
| **Intel Thread Director** (described in S14) | Hardware hints to the OS | Per-thread class and performance/efficiency scores for the OS scheduler | Yes (thread placement) | n/a | n/a | n/a | — | Via S14 |
| **LLVM `static_steal`** (S7) | LLVM OpenMP runtime | Equal static blocks, idle threads steal | No | Yes | Yes | No | — | Extract† |
| **LB4OMP / Auto4OMP** (S4) | Modified LLVM runtime | 14 self-scheduling techniques and automatic selection among them | Indirectly | Yes | Technique-dependent† | Some adaptive techniques† | HPC nodes† | Extract† |
| **Feedback-guided scheduling** (S5, 1998) | Research runtime | Partition from timings of the previous execution of the same loop | No (symmetric machines) | Yes | No† | **Yes** | Distributed shared memory | Extract† |
| **Asymmetry-aware work stealing** (S15, 2016) | Task runtime | — | Yes† | Yes† | Yes† | — | — | **Not read** |
| **HASpMV** (S16, 2023) | Application kernel | Heterogeneity-aware partitioning and storage format for sparse matrix–vector product | Yes | Yes | —† | — | 12th/13th-gen Intel, AMD | Extract† |
| **Phase 1/1B prototype (ours)** | User-level C on stock GCC | Contiguous blocks sized by an application cost hint × measured thread speed; stealing; per-loop memory | Yes | Yes | Yes | Yes | i3-1220P (2P+8E), three real applications | — |

### What S14 already establishes (and we therefore cannot claim)

- Asymmetry-aware loop scheduling for GCC OpenMP on a real Alder Lake CPU, across four benchmark suites.
- That no single standard schedule is best everywhere on such CPUs — the same observation Phase 1 presented as its motivation.
- That `static` wastes big cores, shown with execution traces.

### What S14 leaves open (in its own words or by omission)

- Per-loop choice of policy inside one application: the authors note this "requires making changes in the application" and leave it for future work.
- Application knowledge of where the work is (cost hints).
- Power-limited operation: their experiments disable Turbo Boost on a desktop CPU; a laptop under a 15 W cap behaves differently (`APPLICATION_FEASIBILITY_RESULTS.md` §1.3).
- Hyperthreading effects on the asymmetry are not what the paper is about.

### What our own measurements say about the prototype

Tested fairly on real applications, the prototype's distinguishing features did **not** earn their place:

| Feature | Evidence |
|---|---|
| Capacity weighting | ≤2% and within noise in every application |
| Cross-execution feedback | No measurable benefit; the Phase 1 share-based form is unsound for irregular loops |
| Cost hints | Useful only in the flood model's localised case (matches the best policy even without stealing) |
| Stealing over contiguous blocks | The one consistently good ingredient — and that is LLVM's `static_steal`, already existing outside GCC |

**Honest verdict on Candidate 1 as a scheduling contribution:** at best "a user-level `static_steal` for GCC with optional cost hints, plus a negative result on capacity weighting". That is too thin to carry the project, and S14 has already done the broad Alder Lake evaluation with far more resources. Its work is not wasted: it becomes one compared policy and a documented negative result inside an application project.

## 3. Prior art in the candidate application domains

### 3.1 Protein database search

| Work | What it does | Relation to a student project |
|---|---|---|
| SWIPE (S10, 2011) | Inter-sequence SIMD: 16 database sequences per vector with hand-written SSE; 106 billion cell updates/s on a dual six-core Xeon | The decomposition our spike uses. Not new |
| Parasail (S11, 2016) | Library of hand-vectorised intra-sequence kernels (SSE, AVX2) | Independent oracle and speed yardstick (S31); our spike matched its scores exactly and came within ~4% of its one-thread speed on one workload |
| SWIMM / SWIMM 2.0 (S30, 2015–2019) | Smith–Waterman for Intel multicore and manycore (AVX2, AVX-512, Xeon Phi) with energy analysis; reports GCUPS per watt† | Shows energy-aware evaluation of this workload is established on servers and accelerators |
| GPU tools (CUDASW++ family) | — | Known to exist; **not researched this session** |

**Searched and not found:** an evaluation of Smith–Waterman search on an Intel hybrid (P+E) CPU, or under a laptop power cap (one web search; see "Searches that found nothing" in the register). "Not found" only.

### 3.2 Flood inundation

| Work | What it does | Relation |
|---|---|---|
| Bates et al. (S17) | The inertial scheme | The numerics we borrowed |
| Neal et al. 2009 (S18) | OpenMP parallelisation; load imbalance limited speedup† | Confirms the problem |
| Neal et al. 2018 (S19) | Dry-cell skipping, vectorisation; 4.2–8.4× single-core | Confirms irregular work is deliberate practice |
| Ginting & Mundani 2019 (S20) | "Weighted-dynamic load balancing" for wet/dry imbalance among threads† | **Closest prior art; unread.** Our cost-hint blocks may be the same idea |
| Sanders et al. (S21), Roberts et al. (S22) | Wet-weighted static partitioning; dynamic rebalancing on clusters† | Same theme, distributed memory |

## 4. Does the application domain reveal a better contribution?

Yes. The measurements point away from "a smarter scheduler" and toward three things that are both real and defensible:

1. **Two-level parallelism in one real workload, with the levels interacting.** In protein search, SIMD delivered ~12.5× and required length-sorting; length-sorting then made the default thread schedule 23–47% slower. Explaining and resolving that interaction is a genuine design problem, not a library call.
2. **Portable vectorisation that matches a hand-tuned library.** Plain C with `#pragma omp simd` reached Parasail-level speed with identical scores. That is a concrete, checkable engineering result, and it removes the main objection to this candidate in Phase 1 (hard-to-defend intrinsics).
3. **Honest characterisation of a power-limited hybrid laptop.** Which cores to use changed performance by 35%, more than any scheduling policy; sustained and burst speedups differ by 2×; and two separate benchmarking pitfalls (run-order throttling, neighbour carry-over) each reversed conclusions before being fixed. We did not find this combination studied for this workload.

### Defensible contribution statement for the recommended project

> An exact protein database search engine in portable C that combines compiler-vectorised inter-sequence SIMD with multithreading; verified score-for-score against an established library; and a controlled study of how it behaves on a power-limited hybrid laptop CPU — which cores to use, how to map batches to threads, and how to measure it without the artefacts that initially misled us.

### What must not be claimed

- A new alignment algorithm, a new SIMD technique or a new scheduling algorithm.
- Being faster than SWIPE, Parasail or GPU tools in general. Only same-machine comparisons that were actually run may be stated.
- That no one has studied this on hybrid CPUs — only that limited searches did not find it.
- That capacity-aware scheduling "works": our evidence says it does not matter here.

### Reading still owed before the final report

S15 (asymmetry-aware work stealing), S20 (wet/dry weighted balancing), the full S1 PDF, S10/S11/S30 full texts, and one targeted look at GPU Smith–Waterman tools for the "how this maps to a GPU" discussion.
