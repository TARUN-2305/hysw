#!/usr/bin/env bash
# M6 study: every experiment behind the report's tables and figures (~2 h on the i3-1220P).
# Second run (2026-11-21) with the tiled kernel (DECISIONS.md D4); the first run is kept in results/m6_v1_untiled/.
# Leave the laptop on AC power and otherwise idle while it runs. Raw data: results/m6/.
# Then regenerate all tables and figures with:  .venv/bin/python -I scripts/make_m6_report.py
set -euo pipefail
cd "$(dirname "$0")/.."
PY=.venv/bin/python
SP=${SP:-data/swissprot_2026_04}
OUT=results/m6
mkdir -p $OUT
[[ -f $SP/swissprot.bin ]] || { echo "Swiss-Prot missing: run scripts/get_swissprot.sh" >&2; exit 1; }
if [[ ! -f data/ecoli_x20.bin ]]; then
    python3 -I scripts/replicate_fasta.py data/ecoli_k12.fasta 20 data/ecoli_x20.fasta
    bin/hysw_engine prep --fasta data/ecoli_x20.fasta --out data/ecoli_x20.bin > /dev/null
fi
only=${1:-all}
want() { [[ $only == all || $only == "$1" ]]; }

# E1 speedup decomposition: scalar 1 thread -> SIMD 1 thread -> SIMD one thread per physical core
want e1 && $PY -I bench/run_bench.py --db $SP/swissprot.bin --queries tests/queries/q_medium.fasta --rounds 5 \
    --set "scalar:1:dynamic,1;simd:1:dynamic,1;simd:physical:dynamic,1" --regime sustained --out $OUT/e1_kernels.csv
# E2 thread sets (which cores?)
want e2 && $PY -I bench/run_bench.py --db $SP/swissprot.bin --queries bench/queries_bench3.fasta --rounds 9 \
    --set threadsets --regime sustained --out $OUT/e2_threadsets.csv
# E3 batch-to-thread mapping policies, one thread per physical core
want e3 && $PY -I bench/run_bench.py --db $SP/swissprot.bin --queries bench/queries_bench3.fasta --rounds 9 \
    --set policies --regime sustained --out $OUT/e3_policies.csv
# E3b per-thread timelines for the figure (one traced run each)
if want e3; then
    for p in static dynamic,1; do
        bin/hysw_engine search --db $SP/swissprot.bin --query tests/queries/q_medium.fasta --cores physical --policy $p \
            --trace --top-k 1 --align-k 0 --condition 2 > $OUT/e3_trace_${p%%,*}.json
    done
fi
# E4 burst -> sustained transition
want e4 && $PY -I bench/regime_trace.py --db $SP/swissprot.bin --query tests/queries/q_short.fasta --out $OUT/e4_regime \
    --configs "1:30,physical:80,all:80" --cooldown 60
# E5 query length x kernel variant (tiled/untiled, query/full profile) x core type (E. coli x20: short runs)
E5=""
for c in 1 4 physical; do E5+="simd:$c:dynamic,1:query;simd:$c:dynamic,1:full;simd_untiled:$c:dynamic,1:query;"; done
want e5 && $PY -I bench/run_bench.py --db data/ecoli_x20.bin --queries bench/queries_sweep6.fasta --rounds 5 \
    --set "$E5" --regime sustained --out $OUT/e5_querylen_profile.csv
# E6 same-machine yardstick: Parasail one thread vs HySW one thread
want e6 && $PY -I bench/parasail_yardstick.py --db-fasta $SP/uniprot_sprot.fasta --db-bin $SP/swissprot.bin \
    --queries bench/queries_bench3.fasta --rounds 3 --out $OUT/e6_parasail.csv
# E7 before/after the cache fix on the main database: untiled vs tiled kernel, 10 and 12 threads
want e7 && $PY -I bench/run_bench.py --db $SP/swissprot.bin --queries bench/queries_bench3.fasta --rounds 5 \
    --set "simd_untiled:physical:dynamic,1;simd:physical:dynamic,1;simd_untiled:all:dynamic,1;simd:all:dynamic,1" \
    --regime sustained --out $OUT/e7_tiling.csv
echo "M6 runs finished: $(date -Iseconds)"
