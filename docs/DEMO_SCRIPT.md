# Demo video: storyboard, recording sequence and narration (about 6 minutes)

Before recording: laptop **on AC power**, other apps closed, `app/launch.sh`, browser at http://127.0.0.1:8372 (Firefox or Chrome; the 3D view needs WebGL). The student records; nothing below claims a recording exists.

| # | Time | Screen | Narration |
|---|---|---|---|
| 1 | 0:00 | Title slide | "HySW searches every protein in Swiss-Prot exactly, using SIMD and all the cores of this laptop. I'll show the search, then where the speed comes from." |
| 2 | 0:20 | Search: click *Hemoglobin subunit beta*, Search | "Watch the engine activity: each tile is a CPU; P-cores blue, E-cores orange. They light up from the engine's own progress reports." |
| 3 | 0:45 | Results summary and hit table | "576,073 proteins in about a second, around 26 billion cells per second. A plain scalar search would take about 43 seconds; that figure is an estimate from the measured scalar speed." |
| 4 | 1:15 | Select a lower hit; point at residues in the alignment | "The alignment shows identical and similar residues. Pointing at a residue finds it on the predicted structure from AlphaFold DB; the aligned region is coloured only when the model's sequence matches." |
| 5 | 2:00 | Lab → Scalar vs SIMD → Run live | "Same work, one change. Scalar fills one protein's table at a time; SIMD fills sixteen. Live here: about 10×; measured over repeated runs: 12.7×." |
| 6 | 2:40 | Inside the vector: Play | "These are real values traced from the kernel: sixteen globins of the same length, filled in lock-step, in the order the tiled kernel computes them." |
| 7 | 3:15 | 1 thread vs 10 | "Ten cores give only about 2.5× under sustained load. Why? Two reasons, next." |
| 8 | 3:40 | Static vs dynamic → Run live | "With static shares the fast P-cores finish early and wait. Dynamic scheduling hands out the next batch to whoever is free: every core finishes together." |
| 9 | 4:20 | Burst vs sustained | "And the power wall: after about 17 seconds the laptop's 15-watt limit lowers every core's clock, a 41% drop." |
| 10 | 4:55 | HySW vs Parasail | "Every score is identical to the Parasail library, over 1.15 million comparisons. On one core HySW is level with it on long queries and faster on short ones." |
| 11 | 5:30 | Terminal: `tests/run_tests.sh` output (pre-recorded) | "All tests pass, including sanitizers. Everything shown is reproducible from the scripts in the repository." |
| 12 | 5:50 | Closing slide | "Exact search in about a second on a laptop, and a measured answer to why twelve threads are not twelve times faster." |

Recording tips: zoom the browser to 125% for readability; keep the cursor still during live runs; if a live number differs from the narration, say "this run" and keep going (live results vary with power state).
