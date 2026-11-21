# Research Findings — Phase 1

> **Phase 1B correction (2026-11-11).** §3 below says no Alder Lake loop-scheduling study was found; one exists (source S14). See `NOVELTY_AND_PRIOR_ART_COMPARISON.md`. Text below is unchanged.

Source IDs (S#) refer to `SOURCE_REGISTER.md`; measurements (FL §n) refer to `FEASIBILITY_LOG.md`.

## 1. Problem context

OpenMP's `parallel for` splits a loop's iterations among threads. With no `schedule` clause GCC uses `static`: equal-sized contiguous blocks. That is ideal when all cores are equally fast and iterations cost the same.

Hybrid CPUs break the first assumption. This laptop's i3-1220P has 2 fast P-cores (4 hyperthreads) and 8 slower E-cores. With equal blocks the P-core threads finish early and wait at the end of the loop while E-cores are still working. Intel's own library documentation states that static splitting "is likely to result in lower performance" on mixed cores and falls back to recommending P-cores only as "simple and predictable" (S2).

What we measured on this machine:

- A P-core is 1.1× to 2.8× faster than an E-core **depending on the kind of loop** (FL §2). The operating system reports the same `cpu_capacity` for both (FL §1), so the ratio cannot simply be looked up.
- With `static`, the fastest thread idled for 49% of a uniform loop, costing ~20% of run time (FL §3, §5).
- `dynamic` repairs the balance but was 22–116% slower than `static` on a stencil that is swept repeatedly and fits in cache (FL §4, §5): threads keep receiving different rows, so their caches are never warm, and dispatch costs are comparable to the work.
- Restricting to P-cores only (the vendor's simple advice) was ~1.9× slower than using all cores on this 2P+8E chip (FL §5c).

So a programmer on a hybrid CPU with stock GCC has to pick per loop between a schedule that wastes fast cores and one that wastes cache locality, and the right choice depends on properties of the loop they may not know.

## 2. Relevant concepts (course vocabulary)

- **Static vs dynamic mapping, load balancing** (Unit IV; S13): static mapping decides who does what before execution; dynamic mapping decides at run time, at a cost in synchronisation and locality.
- **Work-pool model / work stealing:** idle workers take work from busy ones. Balanced like dynamic mapping, but synchronisation only happens when someone runs out.
- **Task characteristics:** uniform vs non-uniform task sizes; known vs unknown cost. Mandelbrot rows are non-uniform; stencil rows are uniform.
- **Asymmetric multiprocessor:** a shared-memory machine whose cores differ in speed — a departure from the *symmetric* shared-memory model of Unit II.
- **Cache locality / affinity:** a thread that processes the same data every sweep finds it in its own L1/L2. Here the P-cores have private 1.25 MB L2 and each group of four E-cores shares 2 MB (FL §1).
- **Speedup on unequal cores:** "speedup on 12 threads" needs a stated baseline. We use the best sequential run on a P-core. The ideal is not 12× because the cores are not equal (Unit I: measuring and reporting performance).

## 3. Prior art and how the proposal differs

| Work | What it does | Gap relevant to us |
|---|---|---|
| AID (S1) | Speed-proportional iteration distribution with online sampling; three variants | Lives **inside a patched libgomp + GCC 8**; recompilation with that toolchain required. Evaluated on ARM big.LITTLE and an emulated asymmetric Xeon. †No work stealing, †no feedback across loop executions, †SMT and power throttling not discussed († = from an automated summary, to be re-checked) |
| LLVM `static_steal` (S7) | Static blocks + stealing | Equal blocks (not speed-aware). Not in GCC's runtime (S6) |
| LB4OMP / Auto4OMP (S4) | Many adaptive self-scheduling techniques and automatic selection | Custom LLVM runtime build; aimed at HPC nodes rather than hybrid laptops |
| Feedback-guided scheduling (S5) | Uses timings from earlier executions of the same loop | 1998, symmetric machines; the idea we reuse |
| LLVM/Intel core-type selection (S3), Intel guidance (S2) | Choose *which* cores run threads | Does not size work by core speed; P-only was 1.9× slower here |

**Honest positioning.** Every ingredient exists somewhere: speed-proportional splitting (S1), stealing (S7), feedback (S5). What we did not find is (a) these combined in a form a **stock GCC** user can drop into a program without rebuilding a compiler or runtime, and (b) a controlled measurement on a real **Intel hybrid (P+E, with SMT)** laptop that also examines cache-sensitive repeated loops and power throttling. Two targeted searches found no peer-reviewed loop-schedule evaluation on Alder/Raptor Lake; that is a limited search, not proof of absence. The project should be described as an **engineering integration and careful comparative study**, not as a new algorithm.

## 4. Technical feasibility evidence

- A ~60-line prototype of the combined policy ran correctly (exact checksums) and stayed within 5.5% of the best stock schedule on four kernels where each stock schedule was 31–116% off on at least one (FL §5b). Exploratory, single session.
- It needs nothing beyond GCC and OpenMP locks, both present.
- The prototype did **not** separate the contribution of weights from that of stealing; on one kernel stealing alone was already near-best (FL §5b). This is the main open technical question.

## 5. Limitations that shape the project

1. **Memory bandwidth.** One P-core nearly saturates this laptop's memory bandwidth; memory-bound loops ran *slower* on 12 threads (FL §3, §4). The benchmark set must be mostly compute-bound or cache-resident, and memory-bound cases reported as negative results.
2. **Power throttling.** ~20 s of all-core load triggers a persistent ~35–40% slowdown (FL §5a), plausibly the 64 W → 28 W power limits (S8; inferred). Sequential benchmarking is biased; interleaved rotation and regime-aware reporting are mandatory.
3. **No hardware counters, no energy readings** without sudo (FL §1). Explanations involving cache misses or power remain hypotheses unless the student enables access.
4. **One machine.** Conclusions apply to this CPU; generality is not claimed.
5. **Modest absolute gains.** Against a programmer who already picks the right stock schedule for each loop, the expected benefit is a few percent. The value is not having to know.

## 6. Notes on the course documents

- The syllabus rubric (S12) reads: *case-study-based teaching/learning (10), program-specific requirements (10), video-based seminar/presentation/demonstration (20)*. `CLAUDE.md` describes the middle item as "creativity and practical implementation". The sentence introducing the rubric does mention creativity and practical implementation, so the two are compatible, but **what "program-specific requirements" means should be confirmed with the course faculty**.
- Lab Part B allows a mini project "using any of the parallel programming concepts", and the lab list includes OpenMP, so a CPU/OpenMP project is within the rules. Course outcome CO2 names MPI, OpenACC and CUDA specifically; the chosen project does not exercise those. This is recorded as a risk in `PROJECT_DECISION.md`.
