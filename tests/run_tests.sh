#!/usr/bin/env bash
# All correctness tests (blueprint §6). Exit status 0 only if everything passes.
#   tests/run_tests.sh          release build
#   tests/run_tests.sh --san    additionally run the kernel tests under address/UB sanitisers
set -uo pipefail
cd "$(dirname "$0")/.."
PY=.venv/bin/python
S=results/scratch; mkdir -p $S results/verification
fail=0
step() { echo; echo "=== $*"; }
run() { "$@" || { echo "*** FAILED: $*"; fail=1; }; }

step "build (includes the vectorisation check)"
./build.sh || exit 1

step "kernel tests: SIMD vs scalar, overflow, traceback, mapping policies x thread sets"
run bin/test_kernels data/BLOSUM62 data/ecoli_k12.fasta

step "Parasail oracle: scalar kernel, 1 thread"
run bin/hysw_engine search --db data/ecoli_k12.fasta --query tests/queries/oracle_set.fasta --mode scalar --top-k 1 --dump-scores $S/o_scalar.bin > /dev/null
run $PY -I scripts/oracle_parasail.py data/ecoli_k12.fasta tests/queries/oracle_set.fasta $S/o_scalar.bin

step "Parasail oracle: SIMD kernel, all CPUs, default policy and profile (prepared .bin database)"
run bin/hysw_engine prep --fasta data/ecoli_k12.fasta --out $S/ecoli_k12.bin > /dev/null
run bin/hysw_engine search --db $S/ecoli_k12.bin --query tests/queries/oracle_set.fasta --cores all --top-k 1 --dump-scores $S/o_simd.bin > /dev/null
run $PY -I scripts/oracle_parasail.py data/ecoli_k12.fasta tests/queries/oracle_set.fasta $S/o_simd.bin --record results/verification/parasail_oracle.json

step "Parasail oracle: SIMD kernel with --profile full (rows for all 24 residue codes)"
run bin/hysw_engine search --db $S/ecoli_k12.bin --query tests/queries/oracle_set.fasta --cores physical --profile full --top-k 1 --dump-scores $S/o_simdq.bin > /dev/null
run $PY -I scripts/oracle_parasail.py data/ecoli_k12.fasta tests/queries/oracle_set.fasta $S/o_simdq.bin

step "Parasail oracle: original untiled SIMD loop order (--kernel simd_untiled, kept for comparison)"
run bin/hysw_engine search --db $S/ecoli_k12.bin --query tests/queries/oracle_set.fasta --cores physical --kernel simd_untiled --top-k 1 --dump-scores $S/o_simdu.bin > /dev/null
run $PY -I scripts/oracle_parasail.py data/ecoli_k12.fasta tests/queries/oracle_set.fasta $S/o_simdu.bin

step "Parasail oracle: scores above the 16-bit limit (overflow path)"
python3 -I tests/make_overflow_fasta.py $S/overflow_db.fasta $S/overflow_q.fasta
run bin/hysw_engine search --db $S/overflow_db.fasta --query $S/overflow_q.fasta --cores physical --top-k 3 --dump-scores $S/o_over.bin > $S/o_over.json
run $PY -I scripts/oracle_parasail.py $S/overflow_db.fasta $S/overflow_q.fasta $S/o_over.bin
python3 -c "import json;d=json.load(open('$S/o_over.json'));print('  engine re-scored', [q['timing']['rescored'] for q in d['queries']], 'sequences; top scores', [q['hits'][0]['score'] for q in d['queries']])"

step "engine interface and robustness"
run $PY -I tests/test_engine.py

step "web app: input rules, API, searches through the app equal the engine's direct output"
run $PY -I tests/test_app.py

if [[ "${1:-}" == "--san" ]]; then
    step "kernel tests under AddressSanitizer + UBSan"
    ./build.sh san && ASAN_OPTIONS=detect_leaks=1 run bin/test_kernels_san data/BLOSUM62 data/ecoli_k12.fasta
fi

echo
if [[ $fail == 0 ]]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit $fail
