#!/usr/bin/env bash
# PHY numeric unit test driver -- see PHY/tests/README.md.
# Usage: cd PHY && bash tests/run_numeric_tests.sh
# Requires: build/*.o already built (make -f c_Makefile).

set -uo pipefail
cd "$(dirname "$0")/.."   # -> PHY/

CC=${CC:-gcc}
CFLAGS="-std=c99 -O2 -Wall -Wextra -Iinclude"
BUILD=build
TESTS_DIR=tests
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if [[ ! -d "$BUILD" ]] || [[ -z "$(ls -A "$BUILD" 2>/dev/null)" ]]; then
    echo "✕ $BUILD/ is empty -- run 'make -f c_Makefile' first." >&2
    exit 1
fi

PASS=0
FAIL=0

run_test() {
    local name="$1"; shift
    local objs=()
    for o in "$@"; do objs+=("$BUILD/$o"); done
    local bin="$TMPD/$name"
    echo "=== $name ==="
    if ! "$CC" $CFLAGS "$TESTS_DIR/$name.c" "${objs[@]}" -o "$bin" -lm 2>"$TMPD/$name.build.log"; then
        echo "[FAIL] $name (build error)"
        cat "$TMPD/$name.build.log"
        FAIL=$((FAIL+1))
        return
    fi
    if "$bin"; then
        echo "[PASS] $name"
        PASS=$((PASS+1))
    else
        echo "[FAIL] $name (test assertions failed, see output above)"
        FAIL=$((FAIL+1))
    fi
    echo ""
}

run_test test_rng utils.o
run_test test_mumimo mumimo.o utils.o
run_test test_polar polar.o polar_tables.o polar_rate_match.o crc.o
run_test test_ldpc nr_sch.o nr_rate_matching.o ldpc.o ldpc_tables.o ldpc_nr.o crc.o mcs_table.o
run_test test_crc crc.o
run_test test_modulation modulation.o
run_test test_ofdm ofdm.o
run_test test_dft_precode dft_precode.o
run_test test_matrix utils.o
run_test test_mimo_correlation mimo.o utils.o

echo "========================================"
echo "Numeric test results: $PASS/$((PASS+FAIL)) passed"
echo "========================================"
[[ $FAIL -eq 0 ]]
