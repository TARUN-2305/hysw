# Decisions Log

Evidence-based or user-directed changes to the project baseline. Phase 1 / 1B records are in `research/`.

## D0 — Project selection (2026-11-11, Phase 1B)
Exact protein database search (Smith–Waterman) with two-level parallelism (SIMD + OpenMP threads) on the student's hybrid laptop CPU. Supersedes the Phase 1 hybrid-CPU scheduler as the main project; the scheduler survives as one compared mapping policy and a reported negative result.
Evidence: `research/APPLICATION_FEASIBILITY_RESULTS.md`, `research/PROJECT_OPTIONS_COMPARISON.md`, `research/FINAL_PROJECT_PROPOSAL.md`.

## D1 — Local web application as the user interface (2026-11-20, student's explicit instruction)
- **Decision:** the deliverable is a local web app (browser UI + small local Python server + the C engine as a separate process). No command-line-only user experience. Decorative elements, including a 3D structure view of hits, are wanted, provided they are well executed.
- **Overrides:** `CLAUDE.md` §6 ("Do not add … a web frontend … merely to make the project look bigger"). The student explicitly approved this override. Reason: usability for the intended user and clarity of the demonstration video, not project size.
- **Safeguards:** the engine stays independently built, tested and benchmarked; the UI only displays engine output; every animation is driven by real data; estimates are labelled; rigorous benchmarks run from the harness, not the browser.
- **Design:** `research/APP_DESIGN.md`.

## D2 — Phase 2 engine implementation choices (2026-11-20, Phase 2 session 1)
Small deviations from `research/PHASE_2_BLUEPRINT_V2.md`, all covered by the correctness tests (`tests/run_tests.sh`).
- **Interleaved batch layout on disk.** `db.bin` stores each batch of 16 length-sorted sequences column-interleaved (residue *j* of all 16 lanes adjacent, short lanes padded with a padding code scoring −100). The SIMD kernel reads one 16-byte column per step instead of 16 scattered pointers with bounds checks (the prototype's approach). The scalar kernel and traceback extract contiguous copies. One-thread SIMD: 8.0–11.8 GCUPS on the E. coli test queries (prototype: 9.4); this is an observation from single runs, not yet a controlled comparison.
- **Modules.** `src/db.c` replaces the planned `dbprep.c` (preparation + loading in one place); `src/align.c` holds the traceback (planned inside `sw_scalar.c`); tracing is in `search.c` (no separate `trace.c`); `src/json.c` added. The engine also accepts a `.fasta` file as `--db` (prepared in memory), used by tests.
- **Traceback** uses three passes (forward score → reverse score → direction matrix on the bounding rectangle only), so memory is bounded by the aligned region rather than query × sequence (up to 175 MB for a 5,000-residue query against titin).
- **Scalar reference** uses textbook "minus infinity" initialisation in 32-bit; the SIMD kernel clamps gap states at 0 to stay in 16-bit. Both are verified equal to each other and to Parasail.
- **Mapping policies:** 7 kept — `static`, `dynamic,1` (default), `dynamic,chunk`, `guided`, `steal`, `cost+steal`, `costcap+steal`. Dropped from the Phase 1B harness: `share+steal` and `costcap (no steal)`; the capacity-aware feedback idea (negative result C5) is still represented by `costcap+steal`.
- **`--cores auto` = one thread per physical core** (Phase 1B's best set). The calibration-based selector (blueprint §4.5) is not built yet.
- **Result JSON** wraps per-query results in a `queries` array (the `APP_DESIGN.md` §4 sketch showed a single query). Engine options are `--kernel` + `--cores`, with `--mode` as a shorthand.
- **Data and environment.** `data/BLOSUM62` and `data/ecoli_k12.fasta` (1.9 MB) are copied from `research/data/` so the repository is self-contained. The venv lives in the project folder (`.venv`, works on this NTFS mount); `requirements.txt` is pinned.
- **Parasail oracle** re-checks any score ≥ 32,000 with Parasail's 32-bit kernel (Parasail's 16-bit striped kernel returned 37,400 correctly without flagging saturation; the oracle does not rely on that).
- **Power limits seen by `sysinfo.sh`:** the package MSR zone (`intel-rapl:0`) reports 64 W long-term, while the MMIO zone (`intel-rapl-mmio:0`) reports 15 W long-term / 25 W short-term (28 s window). The effective limit is the lower one; both are recorded with every result set.
- **DNA pasted as a query** is accepted by the engine (A, C, G, T are valid amino-acid letters). Detecting it is left to the app's input validation (M6a), as `APP_DESIGN.md` §2.2 specifies.

## D3 — Defaults set from the M6 measurements (2026-11-21, Phase 2 session 1)
Evidence: the first M6 run, now archived in `results/m6_v1_untiled/` (`SUMMARY.md`, randomised, conditioned, 5–9 rounds; `EXPLORATORY_e5_cache_diagnosis.md`). The numbers below are from that run; the re-run with the D4 kernel is `results/m6/SUMMARY.md`.
- **SIMD profile default is now `query`** (rows only for the query's residues). E5, one P-core thread: 2.2× faster at L=10 (1.78 vs 0.82 GCUPS) and 1.2× at L=50. From L=150 up the two modes are equal within noise. It is never slower beyond noise on one thread. Parasail-verified (`tests/run_tests.sh` checks both modes). The benchmark harness always passes `--profile` explicitly, so the M6 runs stay reproducible; E1–E4 and E6 used `full`.
- **Default policy stays `dynamic,1`.** In E3, `steal`, `costcap+steal`, `cost+steal`, `dynamic,1` and `dynamic,chunk` are within 0–4.9% of each other, with IQRs of 7–10%, so they tie. `dynamic,1` is the simplest of them and standard OpenMP. `static` (+22%) and `guided` (+55%) are measurably worse and stay only as comparison points.
- **`--cores auto` stays `physical` for now.** In E2 all 12 logical CPUs are 7.9% faster than one thread per physical core (16.9 vs 15.7 GCUPS), but the IQRs (7.4%, 12%) overlap. The calibration selector will decide this, as the blueprint §4.5 intends.
- **Known limitation (fixed by D4):** at query lengths above ~500 the per-thread H/E arrays (64 B × L) leave L1. When all four E-cores of a cluster spill into their shared 2 MB L2, per-thread speed falls ~60% (exploratory diagnosis). Candidate fix: strip-mine the query into L1-sized strips. Not implemented because it changes the kernel and would require re-running M6. This is the student's decision.

## D4 — Tiled SIMD kernel: column blocks keep the working set in L1 (2026-11-21, student chose "fix it first")
- **Problem (from D3):** the original kernel sweeps every query row for every database column, so its H/E arrays (64 B × query length) are read and written once per column. Above L≈500 they leave L1, and the four E-cores sharing one 2 MB L2 saturate it. On 10 threads, long queries ran at about half speed (`results/m6_v1_untiled/EXPLORATORY_e5_cache_diagnosis.md`).
- **Change:** `src/sw_simd.c` now processes database columns in blocks of 16 (`HYSW_COLBLOCK`). For each query row it updates all 16 columns of the block before moving on. The block's profile (12 KB) and column state (1 KB) stay in L1; the long H/E arrays are touched once per block, so L2 traffic is 16× lower. The per-cell arithmetic is unchanged, so scores are bit-identical. This is standard loop tiling (blocking) for cache locality.
- **Kept for comparison:** the original loop order as `--kernel simd_untiled`. `build.sh` checks that both lane loops are vectorised.
- **Verification:** kernel tests now run SIMD = scalar for both loop orders × both profile modes (2,278 checks). Parasail oracle: 35,224/35,224 for the tiled default and for `simd_untiled`, plus 96/96 on the overflow path. Engine tests: 460 checks. ASan + UBSan run: see `PHASE_2_PROGRESS.md`.
- **Block size:** exploratory, on battery, 2 repeats. 16 and 32 perform the same, 8 is ~3–5% slower. 16 is chosen because it leaves more of the E-core's 32 KB L1 free.
- **Exploratory effect** (before M6, E. coli ×20, on battery, 2 repeats): four E-cores on one L2 at L=990 went from 5.1–5.2 to 12.0–12.1 GCUPS; 10 threads at L=990 from 18.7–20.7 to 34.7–35.2.
- **Measured effect** (M6 re-run, 2026-11-21 16:16–17:53, on AC, randomised + 2 s conditioning; `results/m6/SUMMARY.md`):
  - E7, Swiss-Prot, 5 rounds, median GCUPS (IQR in the summary): at L=990 the tiled kernel is **1.54×** the untiled one on 10 threads (15.54 → 24.00) and **1.50×** on 12 (15.91 → 23.91). At L=150 and L=400 it is 1.01–1.05×, a tie to a small gain.
  - E5, E. coli ×20, 5 rounds, 10 threads: the untiled kernel falls from 30.6 GCUPS (L=400) to 16.3 (L=990) and 16.0 (L=2339); the tiled kernel gives 32.6, 25.5 and 26.8. Long queries are still ~20% below the L=400 peak, so the fix is large but not complete.
  - One E-core thread alone gains only slightly at L=990 (5.08 → 5.39): a single core does not saturate the shared L2, which agrees with the diagnosis.
- **Measured cost:** one P-core thread on short queries (E5): L=50 is 27% slower (4.47 → 3.28 GCUPS), L=150 8% slower (6.83 → 6.26), L=400 4% slower; from L=990 up the tiled kernel is ~5% faster. On 10 threads short queries are within 5% (L=10: 11.98 vs 11.88; L=50: 21.87 vs 20.89). The likely cause is the per-row copies and the 16-column profile build, which a short query cannot amortise (inference, not measured separately). A single code path is kept because the multi-threaded search, the app's normal mode, does not lose; switching to the untiled loop for short queries on few threads is a possible later refinement.
- **Consequence:** M6 is re-run with this kernel. The first run is archived in `results/m6_v1_untiled/`. `bench/run_bench.py` now defaults to the engine's `query` profile, and E5 compares tiled vs untiled (and query vs full profile). E7 is new: tiled vs untiled on Swiss-Prot.

## D5 — Thread-set calibration is opt-in; `--cores auto` stays `physical` (2026-11-21, measured in E8)
- **Built (blueprint §4.5):** `src/calibrate.c`, run with `hysw_engine calibrate --db DB [--budget 1] [--margin 0.05] [--rounds 5]`, or `search --cores calibrate`.
  - **Workload:** the database sequence closest to length 400 as the query, against every n-th full batch, so the length mix of the sorted database is kept.
  - **Timing:** each set is probed on a small slice first. The trial is then sized so that 5 rounds of all sets take about the budget, with the sets run in rotated order. Each set's speed is the median over the rounds.
  - **Candidates:** `physical`, `all`, `e`, `p`; duplicate sets are dropped.
  - **Rule:** keep `physical` unless the fastest median is more than 5% higher.
- **Verification:**
  - Kernel tests (2,309 checks) cover the decision rule on fixed numbers and strided database slices scoring exactly like the same sequences in the full database (scalar and SIMD). They also run a real short calibration that must follow the rule.
  - Engine tests (471 checks) cover `calibrate` output and argument errors, and that `search --cores calibrate` uses the chosen set's CPUs and gives the same scores as `physical`.
  - The full suite passes with ASan + UBSan.
- **Measured (E8, `results/calibration/`; expectations written before the run in `PLAN.md`):**
  - **Choices:** 30 of 30 calibrations at budgets 0.25, 1 and 3 s chose `physical`. The median wall time is close to the budget; single runs vary by about ±40%.
  - **Ranking:** full single searches in the burst regime rank the sets the same way: physical 39.0, all 37.8, e 30.7, p 19.3 GCUPS. In the sustained regime (M6 E2), physical and all tie.
  - **Overstated gap:** calibration makes physical-vs-all look larger than it is (13% against 3.3%), because its trials are short and stay in the burst window.
- **Decision:** `--cores auto` stays `physical`. Calibration always agrees with it on this laptop, and an automatic calibration would add about 1 s to every search for no measured gain. Calibration remains available on request (`calibrate`, or `--cores calibrate`) as a safeguard for other machines; the app can offer it on its Setup page.
- **Limitation:** the candidates are fixed named sets. Calibration cannot route around background load on particular cores, and its benefit on other CPUs is untested.

## D6 — Web app part 1 (M6a): server and Search screen (2026-11-21)
- **Built:** `app/server.py` (Flask, HTTP routes only), `app/engine.py` (input rules, database list, one engine process per search, exports), `app/static/` (plain HTML/CSS/JS, no build step, no external files), `app/examples.json` (made by `scripts/make_examples.py` from the local Swiss-Prot file, so no sequence was typed by hand).
- **Engine additions:** `hysw_engine dbinfo --db F` reports a database's size without searching (tested in `tests/test_engine.py`).
- **One search at a time.** A second request gets "busy" (HTTP 409) rather than a queue, because each search already uses every core it is given; two at once would only slow both and blur the live core view. Calibration takes the same lock.
- **One-thread modes get no `--cores`.** The app passes `--cores` only for SIMD + threads. Before this fix, "Scalar, one thread" would have run the scalar kernel on 10 threads; a test now checks that scalar and SIMD modes report one thread.
- **Live core view.** It comes from the engine's own progress lines: cumulative batches per thread, mapped to CPUs through the same thread-set lists that `hysw_engine info` reports. Nothing is simulated. On a 1–2 ms E. coli search the view jumps straight to the end; on Swiss-Prot it updates about every 10 ms.
- **Estimate in the run summary:** "a scalar one-thread search would take about X" = cells ÷ the median scalar speed from M6 E1 (0.71 GCUPS, sustained regime). It is labelled as an estimate on screen. It mixes regimes (a burst search against a sustained scalar rate), so the "× faster than that estimate" line is indicative only.
- **Security (local app):**
  - The server binds to 127.0.0.1.
  - It refuses requests whose Host header is not 127.0.0.1/localhost (DNS rebinding) and POSTs that are not JSON (cross-site form posts).
  - It sends a strict Content-Security-Policy (only the app's own files).
  - Queries are written to a temporary file and passed as an argument list, never through a shell.
  - Header text is shown with `textContent`, never parsed as HTML.
  - CSV cells starting with `=`, `+` or `@` are prefixed so spreadsheets do not run them.
- **Input rules** (APP_DESIGN §2.2):
  - **Accepted forms:** raw letters or one FASTA record. Digits, whitespace and a trailing `*` are ignored; lower case is allowed.
  - **Length:** 10–5,000 letters.
  - **Rejected with a clear message:** DNA/RNA (≥ 90% A/C/G/T/U/N, at least 20 letters), several records, other characters.
  - **Warned:** U/O/J (scored as X) and B/Z/X.
- **Verification:** `tests/test_app.py` (66 checks; in `tests/run_tests.sh`). It covers input rules, API errors and guards, and searches through the app returning hits and settings identical to the engine run directly (4 query × mode × core combinations). It also covers progress ending complete, the SSE stream, all three exports, the busy refusal, cancel stopping the engine process, and calibration. Repeated 20 times, as concurrent code. Checked by hand in the browser: Swiss-Prot search (haemoglobin β, 1.16 s, 26.5 GCUPS, single run), E. coli search, DNA rejection, cancel of a scalar Swiss-Prot search, Setup and calibration, light and dark themes, narrow window.
- **Deferred to M6b:** Swiss-Prot download from the Setup screen (now: a message pointing to `scripts/get_swissprot.sh`), 3D structure view, Performance Lab, launcher.

## D7 — Web app part 2 (M6b): 3D structure view, Performance Lab, launcher (2026-11-21)
- **Design process.** The Impeccable frontend skill (installed by the student):
  - PRODUCT.md records product truth, confirmed by the student: the examiner/demo comes first, the Search screen stays a real tool, every number is real or labelled.
  - The Lab's page structure was chosen by the student on the skill's decision page, from dealt structures. The first hand was re-rolled; from the second, "Before and after" was locked.
  - The direction contract is in `.impeccable/surfaces/app-static-index-html.md`.
  - The 3D view is an extension of the Search screen in the existing look.
- **3D viewer: 3Dmol.js 2.5.5** (BSD-3-Clause, 538 KB; npm/jsdelivr; SHA-256 f7cc7892…; licence files in `app/static/vendor/`). Download approved by the student.
  - Chosen over Mol* 5.13.1 (MIT; the viewer of the PDB and AlphaFold DB websites, but much heavier than one highlighted model needs) and NGL (MIT; mostly used from notebooks).
  - It is loaded only when a structure is shown.
  - It is compatible with the app's strict Content-Security-Policy: its one `eval` runs only for string callbacks, which the app never passes, and workers are used only for surfaces, which the app never draws.
- **Structures: AlphaFold DB** prediction API (`/api/prediction/<accession>`; model files v6 in November 2026; CC BY 4.0, attribution shown under the viewer).
  - File URLs are taken from the API, never built by hand.
  - Models are cached in `data/structures/<accession>/`. The 4 example models are pre-cached (924 KB, download approved), so the demo works offline.
  - Selecting a hit uses the cache only. A model that isn't cached downloads only after the user clicks once; after that, other hits download when selected.
  - Errors are explicit: not found, offline, refused (the file server answered some requests with HTTP 403, so a refusal suggests retrying), not cached.
- **Honest highlighting.** The aligned region is coloured on the model only if the model's own sequence matches the hit's aligned residues at the same positions; otherwise the app says why. Alignment colours (identical, similar, different) match the alignment view, and a second mode colours the model by AlphaFold's own confidence (pLDDT) bands. Pointing at a residue marks it in the alignment, on the 3D model and on the 2D map.
- **No-WebGL fallback.** The app's built-in browser pane has WebGL disabled, so 3D cannot draw there. The app detects this and says so, and a 2D protein map (alignment class and confidence along the whole hit protein) always works. 3D was verified in Firefox 151 (headless, driven through its Marionette interface; WebGL renderer reported).
- **Engine addition: `hysw_engine simdtrace`.** It records the real H values of one 16-sequence batch from the production tiled kernel, through a shared always-inlined kernel body (`tiled_impl`) called with tracing off by the search and on by the trace.
  - **Verification:** 4,011,861 traced cells identical to a textbook Gotoh matrix (all lanes, three windows including block boundaries), and lane scores identical to the search kernel. `tests/test_engine.py` checks that the query lane's score equals the search score.
  - **Search unaffected:** the tiled kernel's machine code has the same calls, branches and vector instructions (90) as before; only register allocation differs (270 against 275 instructions). Measured: after/before = 1.009 (1 thread) and 1.013 (10 threads), inside the run-to-run spread (6 interleaved runs each, AC, conditioned). That is a tie.
  - A real bug was caught during this work: a trace parameter named `nc` was hidden by the kernel's own block-width variable `nc`. The compiler's unused-parameter warning exposed it, and it was fixed by renaming.
- **Lab ("Before and after"): five pairs, each isolating one change.**
  - Scalar vs SIMD: live, plus the Inside SIMD replay of a real Swiss-Prot batch in the tiled kernel's order.
  - 1 thread vs 10: live core tiles, plus stored E2.
  - Static vs dynamic: two live traced runs on one time scale, plus stored E3.
  - Burst vs sustained: stored E4 curve and E8b against E2, plus the live power-limit reading.
  - HySW vs Parasail: stored E6, plus both oracle records.
  - Live runs use E. coli × 20 (27.1 M residues), labelled as a copied benchmark database. Every number is tagged LIVE, STORED (with its date) or REPLAY (an illustration of the order of work, with real values).
- **Correctness re-checked on the current kernel.** The Swiss-Prot Parasail oracle was re-run on the tiled kernel: 1,152,146 / 1,152,146 identical (2026-11-21). The earlier record, made with the untiled kernel, is kept as `results/verification/parasail_oracle_swissprot_untiled_2026-11-20.json`.
- **Launcher.** `app/launch.sh` builds the engine if it is missing, starts the server unless it is already running, opens the browser, and stops with `--stop`. `app/install_launcher.sh` adds a desktop menu entry for the user; not installed by me.
- **Tests:**
  - `tests/test_app.py` has 86 checks, now including:
    - structure endpoints with the network disabled, the mapping and the confidence data;
    - the Lab results;
    - the race, where all three engines must find the same top hit and score;
    - traced schedules;
    - the SIMD batch endpoint;
    - one engine task at a time across searches and Lab runs.
  - `tests/test_engine.py`: 481 checks. Kernel tests: 2,351 checks.
- **Shown on the decision page but not built:** the other structures (tabs, a long speed-ladder page, examiner questions, control room, race stage).
- **Finish review** (Impeccable's independent finish reviewer; it had no access to this conversation, only the screenshots and files).
  - **First pass, recapture.** Two captures were invalid: a result link pointed at a job lost when the server restarted, and the phone capture was really 500 px wide.
  - **Second pass, fix: seven material fixes.**
    - Provenance and stored figures moved into the first viewport.
    - The same 16-lane instrument on both sides of Scalar vs SIMD.
    - Sentence-case panel headings instead of uppercase labels.
    - One colour meaning across alignment, structure and map ("different" is now neutral grey, not pale blue).
    - A key for the confidence track.
    - Plain-language policy names.
    - A balanced lead paragraph.
  - **Third pass, fix:** one regression, paired instruments not aligned. Fixed with a shared grid; instruments start at the same height (509 px) in all five pairs.
  - **Final verdict, ship,** scoped to the scored fixes. The reviewer could not see from screenshots that the lane strips fill from live progress, so I measured it: the scalar lane rises 3% → 100% with the engine's progress, and all 16 SIMD lanes hold equal values.
  - **Captures:** headless Firefox 151, driven through Marionette, because no Chromium is installed. The 390 px phone view was emulated by constraining the page width, since Firefox keeps the window at least 500 px wide; all breakpoints evaluate identically at both widths.
- **DESIGN.md and `.impeccable/design.json`** were written by the skill's documenter from the shipped build.
  - Two defects it recorded were then fixed: muted text `#77756f` → `#6b6963` (4.25:1 → 5.1:1 on the page background, now WCAG AA), and reduced-motion mode now also stops the lane and progress fills.
  - Open: the design detector reports 30 places where CSS values (for example 0.9rem text, the 9px nav radius) are not in DESIGN.md's token scale. Either the CSS gets tightened to the scale or DESIGN.md gets extended; that is the student's decision.

