---
version: 1
slug: "app-static-index-html"
primary_target: "app/static/index.html"
related_targets: []
---

# Surface brief: Performance Lab (the "Lab" view of app/static/index.html)

Scope: the Lab screen of the HySW local app (a third view beside Search and Setup). Visitor mode: Read (the visitor understands where the speed comes from and where it stops). Audience: the examiner and audience of the student's demo video and viva first (PRODUCT.md). Task: pick a comparison, run it live or read the stored measurement, understand the change it isolates. Content: live engine runs (race on E. coli x 20, traced schedules, one real SIMD batch) and stored results (results/m6, results/calibration, results/verification). Constraints: every number live, stored with date and settings, or labelled as an estimate or illustration; works offline; keyboard operable; light and dark themes.

## Direction contract

THESIS: Every speedup is one change, so the Lab shows each as a controlled pair on the same data: before on the left, after on the right, the change named between them. It refuses the dashboard-of-charts default where unrelated plots compete on one screen.

OWN-WORLD: The incumbent HySW world, unchanged: off-white ground #f6f6f3 with #fcfcfb cards and 1px #e1e0d9 rules, ink #0b0b0b / #52514e, one blue #2a78d6 for P-cores and for the "after" side's gain, orange #eb6834 for E-cores, system sans with tabular numerals, pill-shaped pair index, dark-mode tokens from app.css. Lane strips of 16 thin bars are the signature mark for SIMD.

STORY: The visitor picks a pair from the index, presses Run live (or reads the stored pair), watches both sides fill from the engine's own progress, reads the speedup on the after side and the stored measurement under it, and leaves knowing: SIMD gives about 13x, threads about 2.5x more, scheduling matters, the 15 W limit caps it, and the results stay exact.

FIRST VIEWPORT: Header with Search / Lab / Setup. Title "Before and after" with one sentence under it. A row of five pair pills (Scalar vs SIMD, 1 thread vs 10, Static vs dynamic schedule, Burst vs sustained, HySW vs Parasail). Below, the selected pair as two equal panels joined by a centred arrow carrying the change's name; each panel has its label, a large time or rate, one sentence, and its instrument (lane strip, core tiles, timeline, curve). The primary action, Run live, sits in the pair's header row at right. A provenance line under the panels says what is live and what is stored.

FORM: Before and after, position 5 of the second ranked list; seed:6dbcd58aa140aabd.

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

## Pairs (content plan)
1. Scalar vs SIMD (1 thread): live race steps 1-2; Inside SIMD replay of real H values (one Swiss-Prot batch, 16 equal-length proteins) under the SIMD side, one lane under the scalar side; stored M6 E1.
2. 1 thread vs 10 threads: live race steps 2-3 with core tiles; stored M6 E1 and E2.
3. Static vs dynamic schedule: two live traced runs, per-core timelines; stored M6 E3.
4. Burst vs sustained: stored M6 E4 curve and E8b (burst) vs E2 (sustained) thread sets; live power-limit reading.
5. HySW vs Parasail: verification records (35,224 and 1,152,146 identical) and stored M6 E6 speed on one core.
