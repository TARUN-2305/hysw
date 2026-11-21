# Phase 1 Exit Checklist

Status as of 2026-11-11. FL = `FEASIBILITY_LOG.md`.

| # | Gate condition | Status | Evidence |
|---|---|---|---|
| 1 | A real problem and intended use case are clearly defined | **PASS** | `PROJECT_DECISION.md` — OpenMP loop scheduling on hybrid P/E-core CPUs with stock GCC; user = programmer/student on a hybrid laptop |
| 2 | The contribution is specific and honestly differentiated from existing work | **PASS, with caveats** | `RESEARCH_FINDINGS.md` §3. Stated as integration + comparative study, not a new algorithm. Two differentiation points rest on an automated summary of the AID paper and must be re-read in the PDF |
| 3 | Course relevance is mapped to actual syllabus concepts | **PASS, with a known gap** | Units I, II, IV and part of III; OpenMP lab. **No CUDA/MPI/OpenACC content** — flagged as risk R4 |
| 4 | Core technical approach checked against credible sources | **PASS** | AID (ICPP 2020), Bull (Euro-Par 1998), LB4OMP (TPDS 2022), LLVM and GCC runtime sources, Intel documentation — `SOURCE_REGISTER.md`. Most read as search extracts, not full text |
| 5 | Highest-risk environment, algorithm and performance assumptions tested | **PASS** | FL §1–§5: toolchain works; asymmetry measured; stock schedules' failure modes measured; prototype policy tested on four kernels with exact output checks |
| 6 | Feasible with known resources, or explicit fallback | **PASS** | Needs only GCC + OpenMP (present). Fallbacks in `PHASE_2_BLUEPRINT.md` §10 |
| 7 | Valid sequential baseline and meaningful evaluation plan | **PASS** | Same loop body on one pinned P-core; exact output comparison; interleaved, regime-aware timing — blueprint §6–§7 |
| 8 | Phase 2 blueprint is implementable and scoped | **PASS** | Modules, API, algorithm, tests, methodology, milestones — `PHASE_2_BLUEPRINT.md` |
| 9 | Open risks and unverified assumptions are visible | **PASS** | `PROJECT_DECISION.md` (risks, unverified assumptions); FL §6 |

## Deliverables

| File | Present |
|---|---|
| `research/PROJECT_OPTIONS.md` | Yes |
| `research/RESEARCH_FINDINGS.md` | Yes |
| `research/SOURCE_REGISTER.md` | Yes |
| `research/FEASIBILITY_LOG.md` | Yes |
| `research/PROJECT_DECISION.md` | Yes |
| `research/PHASE_2_BLUEPRINT.md` | Yes |
| `research/PHASE_1_EXIT_CHECKLIST.md` | Yes |
| `research/spikes/` (4 exploratory C files + raw outputs) | Yes — disposable, not project code |

## Not done in Phase 1 (by design or by constraint)

- No project code, benchmark suite, report or slides.
- Weights-vs-stealing ablation not run.
- No second machine; no LLVM `static_steal` comparison; no hardware counters or energy data.
- Python venv / matplotlib install not exercised.
- Rejected candidates' toolchains (CUDA, MPI) not exercised.

## Gate result

**Phase 1 exit gate satisfied.** Work stops here. Phase 2 begins only on the student's explicit instruction.

---

## Phase 1B addendum (2026-11-11)

Phase 1B revisited the selection at the student's request. Outcome: the recommended project changed to **exact protein database search with two-level parallelism (SIMD + threads)**; see `FINAL_PROJECT_PROPOSAL.md` and `PHASE_2_BLUEPRINT_V2.md`.

| Gate condition | Status for the revised project | Evidence |
|---|---|---|
| Real problem and use case | PASS | `REAL_WORLD_DOMAIN_EXPLORATION.md` §3A |
| Contribution specific and honestly differentiated | PASS (modest; no new algorithm claimed) | `NOVELTY_AND_PRIOR_ART_COMPARISON.md` §4 |
| Syllabus mapping | PASS; still no CUDA/MPI/OpenACC code | `FINAL_PROJECT_PROPOSAL.md` |
| Approach checked against credible sources | PASS; several sources still read only as extracts | `SOURCE_REGISTER.md` |
| Highest-risk assumptions tested | PASS — vectorisation without intrinsics, exactness vs an independent library, thread behaviour under the power limit | `APPLICATION_FEASIBILITY_RESULTS.md` §2 |
| Feasible with known resources / fallback | PASS | `PHASE_2_BLUEPRINT_V2.md` §10 |
| Sequential baseline and evaluation plan | PASS | `PHASE_2_BLUEPRINT_V2.md` §6–§7 |
| Blueprint implementable and scoped | PASS | `PHASE_2_BLUEPRINT_V2.md` |
| Risks and unverified assumptions visible | PASS | `FINAL_PROJECT_PROPOSAL.md`, `APPLICATION_FEASIBILITY_RESULTS.md` §6 |

Work stops here again. Phase 2 begins only on the student's explicit instruction.
