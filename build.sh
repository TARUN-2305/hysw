#!/usr/bin/env bash
# Build the HySW engine and its kernel test program. No make/cmake needed.
#   ./build.sh          release build: bin/hysw_engine, bin/test_kernels (+ vectorisation check)
#   ./build.sh san      the same with address + undefined-behaviour sanitisers: bin/*_san
set -euo pipefail
cd "$(dirname "$0")"
CC=${CC:-gcc}
LIB="src/fasta.c src/matrix.c src/db.c src/sw_scalar.c src/sw_simd.c src/align.c src/mapping.c src/topology.c src/search.c src/calibrate.c src/json.c"
WARN="-Wall -Wextra -Wno-unknown-pragmas -D_GNU_SOURCE"
mkdir -p bin build

if [[ "${1:-release}" == "san" ]]; then
    FLAGS="-std=gnu11 -O1 -g -march=native -fopenmp $WARN -fsanitize=address,undefined -fno-omit-frame-pointer"
    $CC $FLAGS $LIB src/main.c -o bin/hysw_engine_san -lm
    $CC $FLAGS -Isrc $LIB tests/test_kernels.c -o bin/test_kernels_san -lm
    echo "built bin/hysw_engine_san bin/test_kernels_san"
    exit 0
fi

FLAGS="-std=gnu11 -O2 -march=native -fopenmp $WARN"
# Vectorisation check: both lane loops in sw_simd.c (markers HYSW_VEC_LOOP and HYSW_VEC_LOOP_TILED)
# must be reported as vectorised.
$CC $FLAGS -c src/sw_simd.c -o build/sw_simd.o -fopt-info-vec-optimized 2> build/vec_report.txt
for marker in HYSW_VEC_LOOP: HYSW_VEC_LOOP_TILED:; do
    line=$(grep -n "$marker" src/sw_simd.c | head -1 | cut -d: -f1)
    # GCC reports the loop at the 'for' line or at the first statement of its body.
    if ! grep -qE "sw_simd.c:(${line}|$((line + 1))):[0-9]+: optimized: loop vectorized" build/vec_report.txt; then
        echo "ERROR: the SIMD lane loop ${marker%:} (src/sw_simd.c:${line}) was NOT vectorised by $CC. See build/vec_report.txt" >&2
        exit 1
    fi
    echo "vectorisation check ${marker%:}: OK ($(grep -E "sw_simd.c:(${line}|$((line + 1))):" build/vec_report.txt | head -1 | sed 's/.*optimized: //'))"
done
$CC $FLAGS $LIB src/main.c -o bin/hysw_engine -lm
$CC $FLAGS -Isrc $LIB tests/test_kernels.c -o bin/test_kernels -lm
echo "built bin/hysw_engine bin/test_kernels ($($CC --version | head -1))"
