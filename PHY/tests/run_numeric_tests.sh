#!/usr/bin/env bash
# PHY numeric unit test driver -- see PHY/tests/README.md.
# Usage: cd PHY && bash tests/run_numeric_tests.sh [--list | TEST_NAME ...]
# Requires: build/*.o already built (make -f c_Makefile).

set -uo pipefail
cd "$(dirname "$0")/.."   # -> PHY/

CC=${CC:-gcc}
CFLAGS="-std=c99 -O2 -Wall -Wextra -Iinclude"
BUILD=build
TESTS_DIR=tests
# One registry drives listing, validation, and execution (Bash 3.2 compatible).
TEST_NAMES=()
TEST_OBJECTS=()
register_test() {
    TEST_NAMES+=("$1"); shift
    TEST_OBJECTS+=("$*")
}

register_test test_rng utils.o
register_test test_mumimo mumimo.o utils.o
register_test test_polar polar.o polar_tables.o polar_rate_match.o crc.o
register_test test_ldpc nr_sch.o nr_rate_matching.o ldpc.o ldpc_tables.o ldpc_nr.o crc.o mcs_table.o
register_test test_nr_rate_matching nr_rate_matching.o
register_test test_crc crc.o
register_test test_modulation modulation.o
register_test test_ofdm ofdm.o
register_test test_dft_precode dft_precode.o
register_test test_matrix utils.o
register_test test_mimo_correlation mimo.o utils.o
register_test test_channel_estimation channel_estimation.o dft_precode.o
register_test test_pusch_channel_estimation_noise dmrs.o channel_estimation.o dft_precode.o utils.o
register_test test_mimo_detection mimo.o utils.o
register_test test_beam_mgmt beam_mgmt.o codebook_32port.o utils.o
register_test test_codebook_ri_pmi codebook.o utils.o
register_test test_codebook_8port_olla codebook_8port.o
register_test test_codebook_32port_olla codebook_32port.o
register_test test_pusch_codebook_4port pusch_codebook_4port.o mimo.o utils.o
register_test test_pusch_codebook_4port_integration pusch_codebook_4port.o mimo.o utils.o mcs_table.o crc.o nr_sch.o nr_rate_matching.o ldpc.o ldpc_nr.o ldpc_tables.o tbs.o modulation.o dmrs.o channel_estimation.o dft_precode.o tdl.o tdl_tables.o tdl_time.o tdl_mimo.o
register_test test_link_adaptation olla.o ul_power_ctrl.o mcs_table.o utils.o config_parser.o tdl_tables.o
register_test test_codebook_geometry codebook.o codebook_8port.o codebook_32port.o utils.o
register_test test_tbs tbs.o
register_test test_tdl_time tdl_time.o tdl.o tdl_tables.o utils.o
register_test test_tdl_mimo tdl_mimo.o utils.o

usage() {
    echo "Usage: bash tests/run_numeric_tests.sh [--list | --help | TEST_NAME ...]"
    echo "No arguments runs all tests; names select exact tests in registry order."
}
if [[ $# -eq 1 && "$1" == "--help" ]]; then usage; exit 0; fi
if [[ $# -eq 1 && "$1" == "--list" ]]; then
    printf '%s\n' "${TEST_NAMES[@]}"
    exit 0
fi
# Validate the entire request before creating files or compiling any test.
for requested in "$@"; do
    found=0
    for name in "${TEST_NAMES[@]}"; do
        [[ "$requested" == "$name" ]] && found=1
    done
    if [[ $found -eq 0 ]]; then
        echo "Unknown test or invalid option: $requested (use --list)" >&2
        exit 2
    fi
done

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

for i in "${!TEST_NAMES[@]}"; do
    if [[ $# -gt 0 ]]; then
        selected=0
        for requested in "$@"; do
            [[ "$requested" == "${TEST_NAMES[$i]}" ]] && selected=1
        done
        [[ $selected -eq 0 ]] && continue
    fi
    read -r -a objects <<< "${TEST_OBJECTS[$i]}"
    run_test "${TEST_NAMES[$i]}" "${objects[@]}"
done

echo "========================================"
echo "Numeric test results: $PASS/$((PASS+FAIL)) passed"
echo "========================================"
[[ $FAIL -eq 0 ]]
