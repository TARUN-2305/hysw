# E8 — thread-set calibration: plan and expectations (written 2026-11-21, before the run)

**Question.** Does `hysw_engine calibrate` (blueprint §4.5, `DECISIONS.md` D5) pick a good thread set, and does it pick it reliably?

**Informal observations that shaped the expectations.** These are exploratory: single runs on AC, not controlled.
- On Swiss-Prot with a 1 s budget, 3 of 3 runs chose `physical`. Medians were about 40–46 GCUPS for physical, 31–40 for all, 27 for e and 17.5 for p. The runs took 0.9–1.1 s.
- With a 0.2 s budget on E. coli (the kernel test), one run chose `all`, so short calibrations can flip.

**Expectations, written before running E8:**
1. With a 1 s budget, `physical` is chosen in at least 9 of 10 runs.
2. Choices become less stable at 0.25 s and at least as stable at 3 s.
3. The wall time of a calibration is about the budget plus 0.1–0.2 s of probe and warm-up.
4. E8b, full single searches in the burst regime, ranks the sets physical ≥ all > e > p, the same order as the calibration.
   - The calibration's absolute GCUPS are higher than a full search's, because its trials are short and stay deep in the burst window.
   - In the sustained regime (M6 E2), physical and all tie (23.7 vs 23.3 GCUPS).
5. Consequence if these hold: on this laptop calibration confirms the fixed default and adds nothing measurable. Its value is as a safeguard on other machines, which cannot be tested here.

**What would contradict the design.** Any of these would mean the selector or its margin must change:
- A 1 s calibration often chooses a set that E8b shows to be slower than `physical` by more than its IQR.
- Calibration takes much longer than its budget.

## Outcome (added after the run, 2026-11-21 19:26; numbers from `SUMMARY.md`)
1. **Confirmed.** At 1 s, `physical` was chosen 10/10 times.
2. **Not confirmed.** The choice was 10/10 `physical` at every budget, 0.25 s included.
   - Short calibrations are noisier: at 0.25 s the physical/all ratio ranged 1.011–1.807, against 1.094–1.148 at 1 s. The noise did not change the choice here.
   - The 0.2 s E. coli run that chose `all` (kernel test) was on a database small enough to finish in a few milliseconds per pass.
3. **Partly confirmed.** The median wall time matches the budget (0.28 s, 0.89 s and 3.08 s for 0.25, 1 and 3 s), but single runs vary by about ±40% (1 s budget: 0.70–1.41 s).
   - Cause (inference): the trial size comes from one short probe, and burst speed drifts during the calibration.
   - The budget is a target, not a limit.
4. **Confirmed.**
   - Full burst searches (E8b) rank physical (39.0 GCUPS) > all (37.8, +3.3% time, IQRs 1.4–1.8%) > e (30.7) > p (19.3), the same order as the calibration.
   - Calibration GCUPS are higher for `physical` (45.8) and `all`, but lower for `e` (27.7 vs 30.7).
   - Calibration overstates the physical-vs-all gap: 13% in calibration against 3.3% in full searches.
5. **Confirmed.** On this laptop the calibration always agrees with the fixed default and adds nothing measurable. A machine where another set wins could not be tested here.

**Caveat:** E8a started straight after the sanitizer test run, with the package at 70 °C (20 s idle before each calibration). E8b started at 47 °C.
