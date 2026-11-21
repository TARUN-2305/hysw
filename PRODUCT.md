# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users
- **First: the examiner and audience of the student's CS372IA demonstration** (recorded demo video, live viva, classmates). They want to see why exact protein search is fast on a laptop: what SIMD contributes, what threads contribute, and why 12 logical CPUs are not 12× faster. Confirmed by the student, 2026-11-21.
- **Also served: a researcher or biology student** with one protein sequence who asks "which known proteins does this resemble, how closely, and where?". The Search screen must stay genuinely usable for this person; it is a real tool, not a mock-up.
- The student presents the app and must be able to explain every number on screen.

## Product Purpose
HySW runs an exact Smith–Waterman search of one protein against a whole protein database (UniProtKB/Swiss-Prot, 576,073 proteins, or the bundled E. coli proteome) on the student's own laptop, using SIMD (16 database sequences per vector) and OpenMP threads across hybrid P- and E-cores. The app makes that search usable (input, hits, alignments, 3D structure) and makes its parallel performance visible and explainable (Performance Lab). Success: a search runs end to end from the browser with results identical to the engine's own output, and a viewer leaves understanding where the speed comes from and where it stops.

## Positioning
Exact (not heuristic like BLAST) search whose speed comes from two measured levels of parallelism on a hybrid consumer CPU, with every figure on screen traced to a live engine run or a stored, reproducible benchmark. The Lab shows the engine's real per-thread activity and real benchmark results, not illustrations.

## Operating Context
- A local web app: the browser talks to a Python/Flask server on 127.0.0.1, which runs the C engine as a separate process. No accounts, no hosting.
- Used live during a recorded screen demo and a viva, often on the laptop's built-in screen; must also work with the network off (cached demo proteins).
- The only outbound requests: AlphaFold DB structure files by public accession when a 3D view is opened, and UniProt links the user clicks.
- Rigorous benchmarks run from the command-line harness, never from the browser; the Lab shows their stored results with date and settings.

## Capabilities and Constraints
- Built (M6a): Search screen (paste/upload/examples, live per-CPU progress, hit table, alignment, run summary, CSV/text/JSON export, cancel) and Setup screen (databases, hardware card, thread-set calibration). One search at a time.
- M6b scope (confirmed 2026-11-21): 3D structure view of a selected hit (AlphaFold DB model, aligned region highlighted, cached locally, offline for the four example proteins); Performance Lab with all four panels: the race (scalar vs SIMD vs SIMD + threads, live on E. coli), cores at work (live traced static vs dynamic schedules), benchmarks and power (stored M6/E8 results, 15 W limit, Parasail correctness badge), inside SIMD (replay of real cell values of one batch, needs a new engine trace output); double-click launcher.
- Stack: plain HTML/CSS/JavaScript with no build step and no framework (existing codebase); libraries bundled locally. 3D viewer: 3Dmol.js (BSD-3-Clause), approved for bundling.
- Every animation is driven by real engine output; estimates are labelled as estimates.
- Out of scope: accounts, hosting, DNA search, E-values, multiple alignment, running rigorous benchmarks from the browser.

## Brand Commitments
- Name: HySW (working name; the student may rename it).
- Benchmark figures use a fixed palette: P-core threads blue, E-core threads orange; the app follows the same mapping.

## Evidence on Hand
- Measured results: `results/m6/SUMMARY.md` (E1–E7), `results/calibration/SUMMARY.md` (E8), `results/verification/` (Parasail oracle: 35,224/35,224 on E. coli, 1,152,146/1,152,146 on Swiss-Prot).
- Data: UniProtKB/Swiss-Prot release 2026_04 and the E. coli K-12 proteome (CC BY 4.0); AlphaFold DB models (CC BY 4.0, attribution required).
- No user studies, testimonials or adoption figures exist; none may be implied.

## Product Principles
1. Every number is real or labelled: live engine output, a stored benchmark with its date and settings, or a clearly marked estimate.
2. The search is a real tool first: correct, exact results identical to the engine's own output.
3. Explain the parallelism honestly, including where it stops (power limit, ties, slower cases).
4. Works offline on the student's laptop for the demo.

## Accessibility & Inclusion
No product-specific requirement was established. Default: keyboard-operable controls, sufficient contrast in light and dark themes, colour never the only carrier of meaning (P/E cores and alignment classes also labelled).
