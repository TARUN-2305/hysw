#!/usr/bin/env bash
# E8: does the thread-set calibration (DECISIONS.md D5) pick a good set, reliably? (~25 min)
#   E8a  repeatability: 10 calibrations at each budget (0.25, 1, 3 s), random order, 20 s idle before each
#   E8b  ground truth for the app's situation: one full Swiss-Prot search (query length 400) with each
#        candidate set, burst regime (20 s idle before each run), 5 rounds in random order
# The sustained-regime ranking of the same sets is M6 E2 (results/m6/). Expectations written before the
# run: results/calibration/PLAN.md. Leave the laptop on AC power and idle.
set -euo pipefail
cd "$(dirname "$0")/.."
PY=.venv/bin/python
SP=${SP:-data/swissprot_2026_04}
OUT=results/calibration
mkdir -p $OUT
$PY -I bench/run_calibration.py --db $SP/swissprot.bin --budgets 0.25,1,3 --rounds 10 --cooldown 20 \
    --out $OUT/e8a_repeatability.csv
$PY -I bench/run_bench.py --db $SP/swissprot.bin --queries tests/queries/q_medium.fasta --rounds 5 \
    --set "simd:physical:dynamic,1;simd:all:dynamic,1;simd:e:dynamic,1;simd:p:dynamic,1" \
    --regime burst --cooldown 20 --out $OUT/e8b_burst_sets.csv
echo "E8 finished: $(date -Iseconds)"
