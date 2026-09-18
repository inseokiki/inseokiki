#!/usr/bin/env bash
# PHY LLS regression test suite
# Usage: cd PHY && bash regression_test.sh [--list | --match TEXT | LABEL ...]
# Requires: lls_sim_c binary already built (make -f c_Makefile)

set -uo pipefail
cd "$(dirname "$0")"

SIM=./lls_sim_c
PASS=0; FAIL=0
CASE_ID=0
MODE=discover
LABELS=()
REQUESTED=("$@")
MATCH=""

# Enumerate the same cases used for execution, without touching the simulator.
select_case() {
    local label="$1"
    if [[ "$MODE" == discover ]]; then
        LABELS+=("$label")
        return 1
    fi
    if [[ -n "$MATCH" ]]; then [[ "$label" == *"$MATCH"* ]]; return; fi
    if [[ ${#REQUESTED[@]} -eq 0 ]]; then return 0; fi
    local requested
    for requested in "${REQUESTED[@]}"; do
        [[ "$label" == "$requested" ]] && return 0
    done
    return 1
}
group_heading() {
    if [[ "$MODE" == run && ${#REQUESTED[@]} -eq 0 ]]; then printf '\n=== %s ===\n' "$1"; fi
}

pass() { echo "[PASS] $1"; PASS=$((PASS+1)); }
fail() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }

# write_cfg FILE KEY=VAL ...
write_cfg() {
    local f="$1"; shift
    printf '%s\n' "$@" > "$f"
}

run_sim() {
    local cfg="$1"
    "$SIM" "$cfg" 2>&1
}

# expect_fail LABEL KEY=VAL ...
# All current uses trigger config_parser.c's validate_config(), which reports
# each problem as "[config error] ..." and always exits with code 1 (see
# CFG_ERR macro). Checking for that exact signature -- not just "any non-zero
# exit" -- means a crash (SIGSEGV=139, SIGABRT=134, etc.) or an unrelated
# failure is reported as a FAIL here instead of silently counting as the
# expected rejection (lab/PHY_REVIEW_2026-09-10.md 5절 권장사항, 2026-09-10).
expect_fail() {
    local label="$1"; shift
    select_case "$label" || return 0
    CASE_ID=$((CASE_ID+1))
    local cfg="$TMPD/case_${CASE_ID}.cfg"
    if ! write_cfg "$cfg" "$@"; then
        fail "$label (could not write config)"
        return
    fi
    local out rc
    out=$(run_sim "$cfg" 2>&1)
    rc=$?
    if [[ $rc -eq 0 ]]; then
        fail "$label (expected rejection, exited 0)"
    elif [[ $rc -ne 1 ]]; then
        fail "$label (expected deliberate config-error exit code 1, got $rc -- possible crash)"
        echo "  output tail: $(echo "$out" | tail -3)"
    elif ! echo "$out" | grep -qF "config error"; then
        fail "$label (exit=1 but no '[config error]' message -- not the expected rejection path)"
        echo "  output tail: $(echo "$out" | tail -3)"
    else
        pass "$label"
    fi
}

# expect_pass LABEL PATTERN KEY=VAL ...
# PATTERN="" to skip pattern check
expect_pass() {
    local label="$1"; shift
    select_case "$label" || return 0
    local pattern="$1"; shift
    CASE_ID=$((CASE_ID+1))
    local cfg="$TMPD/case_${CASE_ID}.cfg"
    if ! write_cfg "$cfg" "$@"; then
        fail "$label (could not write config)"
        return
    fi
    local out
    if out=$(run_sim "$cfg" 2>&1); then
        if [[ -z "$pattern" ]] || echo "$out" | grep -qF "$pattern"; then
            pass "$label"
        else
            fail "$label (pattern not found: '$pattern')"
            echo "  output tail: $(echo "$out" | tail -3)"
        fi
    else
        fail "$label (crashed/non-zero)"
        echo "  output tail: $(echo "$out" | tail -3)"
    fi
}

define_cases() {
# Common reusable arrays (use "${ARR[@]}" at call sites)
BASE=("SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20")
PDSCH_COMMON=("MCS_INDEX = 10" "MCS_TABLE = TABLE1"
              "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1"
              "CODING = LDPC" "EQUALIZER = MMSE")
PUSCH_COMMON=("MCS_INDEX = 10" "MCS_TABLE = TABLE1"
              "PHYSICAL_CHANNEL = PUSCH" "CODING = LDPC" "EQUALIZER = MMSE")
HQ=("HARQ_ENABLE = 1" "HARQ_MAX_RETX = 2" "HARQ_RV_SEQUENCE = IR")
TDL=("CHANNEL_MODEL = TDL" "TDL_DELAY_SPREAD_NS = 300")

# ──────────────────────────────────────────
# GROUP 1: config validation (expect non-zero exit)
# ──────────────────────────────────────────
group_heading "GROUP 1: config validation"

if select_case "missing config file"; then
if out=$(run_sim "$TMPD/missing.cfg" 2>&1); then
    fail "missing config file (unexpected successful run)"
elif [[ "$out" == *"[config error] Cannot open"* ]]; then
    pass "missing config file"
else
    fail "missing config file (wrong failure path)"
fi

fi

expect_fail "malformed integer" "PHYSICAL_CHANNEL = PDSCH" "NUM_TRIALS = 2x"
expect_fail "non-finite SNR" "PHYSICAL_CHANNEL = PDSCH" "SNR_START = NaN"
expect_fail "unknown config key" "${BASE[@]}" "PHYSICAL_CHANEL = PDSCH"
expect_fail "duplicate config key" "NUM_TRIALS = 1" "NUM_TRIALS = 2"
expect_fail "unsupported BW and SCS pair" "${BASE[@]}" "BANDWIDTH_MHZ = 7"
expect_fail "unsupported physical channel" "${BASE[@]}" "PHYSICAL_CHANNEL = PDSHC"
expect_fail "unsupported channel model" "${BASE[@]}" "PHYSICAL_CHANNEL = PDSCH" "CHANNEL_MODEL = FADING"
expect_fail "unsupported PDSCH MIMO mode" "${BASE[@]}" "PHYSICAL_CHANNEL = PDSCH" "MIMO_MODE = CL_16PORT"
expect_fail "PDSCH MIMO without DMRS" "${BASE[@]}" "PHYSICAL_CHANNEL = PDSCH" "MIMO_MODE = SM_2X2"
expect_fail "legacy fading unimplemented" "${BASE[@]}" "PHYSICAL_CHANNEL = NONE" "CHANNEL_MODEL = FLAT_FADING"
expect_fail "invalid PRACH format" "${BASE[@]}" "PHYSICAL_CHANNEL = PRACH" "PRACH_FORMAT = BAD"
expect_fail "invalid FFT size" "${BASE[@]}" "NFFT = 1000"
expect_fail "invalid TDL delay spread" "${BASE[@]}" "TDL_DELAY_SPREAD_NS = 0"
expect_fail "invalid PUCCH format" "${BASE[@]}" "PHYSICAL_CHANNEL = PUCCH" "PUCCH_FORMAT = 4"

PDSCH_VALID=("MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10"
             "SNR_STEP = 1" "NUM_TRIALS = 20"
             "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1"
             "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE")

expect_fail "bad MCS table" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLEXYZ" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE"

expect_fail "MCS_INDEX=28 out of range for TABLE2 (max=27)" \
    "MCS_INDEX = 28" "MCS_TABLE = TABLE2" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE"

expect_fail "SNR_STEP = 0" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 0" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE"

expect_fail "SNR_END < SNR_START" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 15" "SNR_END = 5" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE"

expect_fail "NUM_TRIALS = 0" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 0" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE"

expect_fail "bad equalizer" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = LMS"

expect_fail "bad TDL_PROFILE (not one of A-E)" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" "CHANNEL_MODEL = TDL" "TDL_PROFILE = Z" "CODING = LDPC" "EQUALIZER = MMSE"

expect_fail "HARQ_MAX_RETX = 0 when HARQ enabled" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE" \
    "HARQ_ENABLE = 1" "HARQ_MAX_RETX = 0" "HARQ_RV_SEQUENCE = IR"

expect_fail "bad HARQ_RV_SEQUENCE" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE" \
    "HARQ_ENABLE = 1" "HARQ_MAX_RETX = 3" "HARQ_RV_SEQUENCE = TURBO"

expect_fail "SM_2X2 + HARQ + flat fading has no dedicated function (only TDL variant exists)" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SM_2X2" "CHANNEL_MODEL = FLAT_FADING" "CODING = LDPC" "EQUALIZER = MMSE" \
    "HARQ_ENABLE = 1" "HARQ_MAX_RETX = 3" "HARQ_RV_SEQUENCE = IR"

expect_fail "PUSCH SM_2X2 + Transform Precoding (TS 38.211 6.3.1.4 forbids >1 layer)" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PUSCH" "MIMO_MODE = SM_2X2" "CHANNEL_MODEL = FLAT_FADING" "CODING = LDPC" "EQUALIZER = MMSE" \
    "TRANSFORM_PRECODING = 1"

expect_fail "PUSCH UL_CB_4PORT + Transform Precoding" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PUSCH" "MIMO_MODE = UL_CB_4PORT" "CHANNEL_MODEL = FLAT_FADING" "CODING = LDPC" "EQUALIZER = MMSE" \
    "TRANSFORM_PRECODING = 1"


# UL codebook drivers do not dispatch the SISO DFE/turbo research paths.
expect_fail "PUSCH UL_CB_4PORT + PUSCH_DFE_ENABLE" \
    "${BASE[@]}" "PHYSICAL_CHANNEL = PUSCH" "MIMO_MODE = UL_CB_4PORT" \
    "CHANNEL_MODEL = TDL" "TRANSFORM_PRECODING = 0" "EQUALIZER = MMSE" "PUSCH_DFE_ENABLE = 1"

expect_fail "PUSCH UL_CB_4PORT + PUSCH_TURBO_ENABLE" \
    "${BASE[@]}" "PHYSICAL_CHANNEL = PUSCH" "MIMO_MODE = UL_CB_4PORT" \
    "CHANNEL_MODEL = TDL" "TRANSFORM_PRECODING = 0" "EQUALIZER = MMSE" "PUSCH_TURBO_ENABLE = 1"



expect_fail "OLLA_ENABLE=1 + unsupported MIMO_MODE (EIGEN_16PORT) has no dedicated function" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = EIGEN_16PORT" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE" \
    "OLLA_ENABLE = 1"

# PHY-01 (2026-09-10, lab/PHY_REVIEW_2026-09-10.md): PUCCH_UCI_BITS outside the
# actually-supported range must be rejected explicitly, not silently clamped
# (previously PUCCH_UCI_BITS=20 on Format 3 ran "successfully" while silently
# using 11 bits -- config and execution must agree or the run must fail).
expect_fail "PUCCH Format 3 + PUCCH_UCI_BITS=20 (unsupported, was silently clamped to 11)" \
    "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 5" \
    "PHYSICAL_CHANNEL = PUCCH" "PUCCH_FORMAT = 3" "PUCCH_UCI_BITS = 20" \
    "PUCCH_NUM_SYMBOLS = 4" "PUCCH_NUM_PRB = 1" "CHANNEL_MODEL = AWGN" \
    "CODING = LDPC" "EQUALIZER = MMSE" "MCS_INDEX = 10" "MCS_TABLE = TABLE1"

expect_fail "PUCCH Format 3 + PUCCH_UCI_BITS=2 (below supported range)" \
    "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 5" \
    "PHYSICAL_CHANNEL = PUCCH" "PUCCH_FORMAT = 3" "PUCCH_UCI_BITS = 2" \
    "PUCCH_NUM_SYMBOLS = 4" "PUCCH_NUM_PRB = 1" "CHANNEL_MODEL = AWGN" \
    "CODING = LDPC" "EQUALIZER = MMSE" "MCS_INDEX = 10" "MCS_TABLE = TABLE1"

expect_fail "PUCCH Format 0 + PUCCH_UCI_BITS=3 (Format 0/1 support only 1-2 bits)" \
    "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 5" \
    "PHYSICAL_CHANNEL = PUCCH" "PUCCH_FORMAT = 0" "PUCCH_UCI_BITS = 3" \
    "CHANNEL_MODEL = AWGN" "MCS_INDEX = 10" "MCS_TABLE = TABLE1"

# ──────────────────────────────────────────
# GROUP 2: MCS table dispatch (P0-1 regression)
# ──────────────────────────────────────────
group_heading "GROUP 2: MCS table dispatch"

expect_pass "TABLE1 dispatch" "TABLE1 (TS 38.214" \
    "${BASE[@]}" "MCS_INDEX = 10" "MCS_TABLE = TABLE1" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" \
    "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE"

expect_pass "TABLE2 dispatch" "TABLE2 (TS 38.214" \
    "${BASE[@]}" "MCS_INDEX = 10" "MCS_TABLE = TABLE2" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" \
    "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE"

expect_pass "TABLE3 dispatch" "TABLE3 (TS 38.214" \
    "${BASE[@]}" "MCS_INDEX = 5" "MCS_TABLE = TABLE3" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = SISO" \
    "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE"

# ──────────────────────────────────────────
# GROUP 3: PDSCH dispatch reachability
# ──────────────────────────────────────────
group_heading "GROUP 3: PDSCH dispatch"

expect_pass "PDSCH SISO AWGN" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN"

expect_pass "PDSCH SISO TDL" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SISO" "${TDL[@]}"

# TDL_PROFILE=D exercises tdl.c's LOS (Rician) tap-0 branch (has_los=1) --
# every other TDL case above implicitly uses the default TDL_PROFILE=A
# (pure NLOS, no LOS branch), so without this case that code path has no
# regression coverage at all (2026-09-15, TS 38.901 exact-profile rollout).
expect_pass "PDSCH SISO TDL profile D (LOS/Rician)" "TDL Profile      : TDL-D" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SISO" "${TDL[@]}" "TDL_PROFILE = D"

expect_pass "PDSCH SIMO_MRC flat" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SIMO_MRC" "CHANNEL_MODEL = FLAT_FADING"

expect_pass "PDSCH SIMO_MRC TDL" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SIMO_MRC" "${TDL[@]}"

expect_pass "PDSCH OLLA SISO" "WindowBLER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SISO" "OLLA_ENABLE = 1"

expect_pass "PDSCH OLLA SIMO_MRC" "WindowBLER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SIMO_MRC" "OLLA_ENABLE = 1"

expect_pass "PDSCH OLLA SM_2X2" "WindowBLER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SM_2X2" "OLLA_ENABLE = 1"

expect_pass "PDSCH OLLA SM_4X4" "WindowBLER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SM_4X4" "OLLA_ENABLE = 1"

expect_pass "PDSCH OLLA CL_4PORT" "WindowBLER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_4PORT" "OLLA_ENABLE = 1"

# high spatial correlation biases RI/PMI selection toward rank-1 (see
# codebook.c/mimo.c docs) -- exercises the rank==1 branch of
# run_pdsch_olla_cl_4port_simulation(), which "PDSCH OLLA CL_4PORT" above
# (default i.i.d., rank-2-heavy at this SNR) doesn't reliably reach.
expect_pass "PDSCH OLLA CL_4PORT high corr (rank-1 branch)" "WindowBLER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_4PORT" "OLLA_ENABLE = 1" \
    "SPATIAL_CORR_TX = 0.9" "SPATIAL_CORR_XPOL = 0.9"

expect_pass "PDSCH OLLA CL_8PORT" "PDSCH OLLA + CL_8PORT simulation complete." \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_8PORT" "OLLA_ENABLE = 1"

expect_pass "PDSCH OLLA CL_8PORT high corr" "PDSCH OLLA + CL_8PORT simulation complete." \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_8PORT" "OLLA_ENABLE = 1" \
    "SPATIAL_CORR_TX = 0.9" "SPATIAL_CORR_XPOL = 0.9"

expect_pass "PDSCH OLLA CL_32PORT" "PDSCH OLLA + CL_32PORT simulation complete." \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_32PORT" "OLLA_ENABLE = 1"

expect_pass "PDSCH OLLA CL_32PORT high corr" "PDSCH OLLA + CL_32PORT simulation complete." \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_32PORT" "OLLA_ENABLE = 1" \
    "SPATIAL_CORR_TX = 0.9" "SPATIAL_CORR_TX_VERT = 0.9" "SPATIAL_CORR_XPOL = 0.9"

expect_pass "PDSCH SM_2X2 flat" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SM_2X2" "CHANNEL_MODEL = FLAT_FADING"

expect_pass "PDSCH SM_2X2 TDL" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SM_2X2" "${TDL[@]}"

expect_pass "PDSCH SM_4X4 flat" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SM_4X4" "CHANNEL_MODEL = FLAT_FADING"

expect_pass "PDSCH SM_4X4 TDL" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SM_4X4" "${TDL[@]}"

expect_pass "PDSCH CL_4PORT flat" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_4PORT" "CHANNEL_MODEL = FLAT_FADING"

expect_pass "PDSCH CL_4PORT TDL" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_4PORT" "${TDL[@]}"

expect_pass "PDSCH CL_8PORT flat" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_8PORT" "CHANNEL_MODEL = FLAT_FADING"

expect_pass "PDSCH CL_8PORT TDL" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_8PORT" "${TDL[@]}"

expect_pass "PDSCH CL_32PORT flat" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_32PORT" "CHANNEL_MODEL = FLAT_FADING"

expect_pass "PDSCH CL_32PORT TDL" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_32PORT" "${TDL[@]}"

expect_pass "PDSCH EIGEN_16PORT flat" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = EIGEN_16PORT" "CHANNEL_MODEL = FLAT_FADING"

expect_pass "PDSCH EIGEN_16PORT TDL" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = EIGEN_16PORT" "${TDL[@]}"

expect_pass "PDSCH EIGEN_16PORT TDL subband" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = EIGEN_16PORT" "${TDL[@]}" "EIGEN16_PRECODER_GRAN = SUBBAND"

expect_pass "PDSCH MU_MIMO flat" "BER_U0" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = MU_MIMO" "CHANNEL_MODEL = FLAT_FADING"

expect_pass "PDSCH MU_MIMO TDL" "BER_U0" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = MU_MIMO" "${TDL[@]}"

expect_pass "PDSCH BEAM_MGMT flat" "P1==Genie" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = BEAM_MGMT" "CHANNEL_MODEL = FLAT_FADING"

expect_pass "PDSCH BEAM_MGMT P1-P3-P2 (UE Rx sweep)" "P2==P1" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = BEAM_MGMT" "CHANNEL_MODEL = AWGN" "BEAM_MGMT_RX_SWEEP = 1"

expect_pass "PDSCH BEAM_MGMT P1-P3-P2 TDL" "P2==P1" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = BEAM_MGMT" "${TDL[@]}" "BEAM_MGMT_RX_SWEEP = 1"

expect_pass "PDSCH BEAM_MGMT TDL" "P1==Genie" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = BEAM_MGMT" "${TDL[@]}"

expect_pass "PDSCH SISO HARQ flat" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SISO" "CHANNEL_MODEL = FLAT_FADING" "${HQ[@]}"

expect_pass "PDSCH SM_2X2 HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SM_2X2" "${TDL[@]}" "${HQ[@]}"

expect_pass "PDSCH SIMO_MRC HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SIMO_MRC" "${TDL[@]}" "${HQ[@]}"

expect_pass "PDSCH SM_4X4 HARQ flat" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SM_4X4" "CHANNEL_MODEL = FLAT_FADING" "${HQ[@]}"

expect_pass "PDSCH MU_MIMO HARQ flat" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = MU_MIMO" "CHANNEL_MODEL = FLAT_FADING" "${HQ[@]}"

expect_pass "PDSCH MU_MIMO HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = MU_MIMO" "${TDL[@]}" "${HQ[@]}"

expect_pass "PDSCH BEAM_MGMT HARQ flat" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = BEAM_MGMT" "CHANNEL_MODEL = FLAT_FADING" "${HQ[@]}"

expect_pass "PDSCH BEAM_MGMT HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = BEAM_MGMT" "${TDL[@]}" "${HQ[@]}"

expect_pass "PDSCH BEAM_MGMT P1-P3-P2 HARQ flat" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = BEAM_MGMT" "CHANNEL_MODEL = FLAT_FADING" "BEAM_MGMT_RX_SWEEP = 1" "${HQ[@]}"

expect_pass "PDSCH BEAM_MGMT P1-P3-P2 HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = BEAM_MGMT" "${TDL[@]}" "BEAM_MGMT_RX_SWEEP = 1" "${HQ[@]}"

expect_pass "PDSCH CL_4PORT HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = CL_4PORT" "${TDL[@]}" "${HQ[@]}"

# run_pdsch_simulation() (PHYSICAL_CHANNEL=PDSCH, USE_DMRS unset/0, OLLA
# off) -- the non-DMRS legacy benchmark, P0-2c (2026-09-03) pilot for TS
# 38.212 5.2.2 multi-code-block segmentation. TB_SIZE=8426 -> B=8450
# (BG1, Kcb=8448) segments into C=2 code blocks that divide evenly
# (nr_seg_compute()'s exit(1) guard for the non-divisible case, see
# nr_sch.h, is not hit here). Not reached by any other PDSCH entry above
# (all set USE_DMRS=1), so these are this function's only coverage.
expect_pass "PDSCH legacy AWGN C=1" "BER" \
    "${BASE[@]}" "MCS_INDEX = 10" "MCS_TABLE = TABLE1" \
    "PHYSICAL_CHANNEL = PDSCH" "CHANNEL_MODEL = AWGN" "TB_SIZE = 3000"

expect_pass "PDSCH legacy AWGN C=2 segmentation" "Code Blocks: C=2" \
    "${BASE[@]}" "MCS_INDEX = 10" "MCS_TABLE = TABLE1" \
    "PHYSICAL_CHANNEL = PDSCH" "CHANNEL_MODEL = AWGN" "TB_SIZE = 8426"

# ──────────────────────────────────────────
# GROUP 4: PUSCH dispatch
# ──────────────────────────────────────────
group_heading "GROUP 4: PUSCH dispatch"

expect_pass "PUSCH AWGN CP-OFDM" "BER" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = AWGN" "TRANSFORM_PRECODING = 0"

expect_pass "PUSCH AWGN DFT-s-OFDM" "BER" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = AWGN" "TRANSFORM_PRECODING = 1"

expect_pass "PUSCH TDL" "BER" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0"

expect_pass "PUSCH HARQ AWGN" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = AWGN" "TRANSFORM_PRECODING = 0" "${HQ[@]}"

expect_pass "PUSCH HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0" "${HQ[@]}"

expect_pass "PUSCH SM_2X2 flat" "BER" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = SM_2X2"

expect_pass "PUSCH UL_CB_4PORT flat" "PUSCH UL 4-port codebook simulation complete." \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "NUM_RB = 24" \
    "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_CB_4PORT"

expect_pass "PUSCH UL_CB_4PORT TDL" "PUSCH UL 4-port codebook extended simulation complete." \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "NUM_RB = 24" \
    "CHANNEL_MODEL = TDL" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_CB_4PORT"

expect_pass "PUSCH UL_CB_4PORT HARQ flat" "BLER(final)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "NUM_RB = 24" "${HQ[@]}" \
    "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_CB_4PORT"

expect_pass "PUSCH UL_CB_4PORT HARQ TDL" "BLER(final)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "NUM_RB = 24" "${HQ[@]}" \
    "CHANNEL_MODEL = TDL" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_CB_4PORT"

expect_pass "PUSCH UL_CB_4PORT fixed LS setting note" "CHAN_EST_METHOD=MMSE applies to DL codebooks; UL_CB_4PORT uses fixed LS" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "NUM_RB = 24" "CHAN_EST_METHOD = MMSE" \
    "CHANNEL_MODEL = TDL" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_CB_4PORT"

# Time correlation is opt-in and restricted to this HARQ driver.
UL_TIME_BASE=("${BASE[@]}" "${PUSCH_COMMON[@]}" "NUM_RB = 4"
              "MIMO_MODE = UL_CB_4PORT" "CHANNEL_MODEL = TDL"
              "TRANSFORM_PRECODING = 0" "${HQ[@]}")
expect_pass "PUSCH UL_CB_4PORT correlated TDL" "TDL time     : correlated" \
    "${UL_TIME_BASE[@]}" "TDL_TIME_CORRELATION = 1" "TDL_MAX_DOPPLER_HZ = 100" "TDL_HARQ_INTERVAL_MS = 1"
expect_pass "PUSCH UL_CB_4PORT static HARQ channel" "fD=0.000 Hz" \
    "${UL_TIME_BASE[@]}" "TDL_TIME_CORRELATION = 1" "TDL_MAX_DOPPLER_HZ = 0"
expect_fail "PUSCH UL_CB_4PORT invalid time correlation flag" \
    "${UL_TIME_BASE[@]}" "TDL_TIME_CORRELATION = 2"
expect_fail "PUSCH UL_CB_4PORT negative Doppler" \
    "${UL_TIME_BASE[@]}" "TDL_TIME_CORRELATION = 1" "TDL_MAX_DOPPLER_HZ = -1"
expect_fail "PUSCH UL_CB_4PORT zero HARQ interval" \
    "${UL_TIME_BASE[@]}" "TDL_TIME_CORRELATION = 1" "TDL_HARQ_INTERVAL_MS = 0"
expect_fail "PUSCH UL_CB_4PORT overflowing time phase" \
    "${UL_TIME_BASE[@]}" "TDL_TIME_CORRELATION = 1" "TDL_HARQ_INTERVAL_MS = 1e300" "TDL_MAX_DOPPLER_HZ = 1e300"
expect_fail "TDL time correlation unsupported PDSCH" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "CHANNEL_MODEL = TDL" "TDL_TIME_CORRELATION = 1" "${HQ[@]}"
expect_fail "PUSCH UL_CB_4PORT time correlation without HARQ" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "MIMO_MODE = UL_CB_4PORT" "CHANNEL_MODEL = TDL" \
    "TRANSFORM_PRECODING = 0" "TDL_TIME_CORRELATION = 1"

expect_pass "PUSCH UL_CB_4PORT spatial TDL" "TDL spatial  : exponential Tx=0.700 Rx=0.600" \
    "${UL_TIME_BASE[@]}" "TDL_SPATIAL_CORR_TX = 0.7" "TDL_SPATIAL_CORR_RX = 0.6"
expect_pass "PUSCH UL_CB_4PORT spatial and temporal TDL" "TDL time     : correlated" \
    "${UL_TIME_BASE[@]}" "TDL_TIME_CORRELATION = 1" "TDL_MAX_DOPPLER_HZ = 100" \
    "TDL_SPATIAL_CORR_TX = 0.7" "TDL_SPATIAL_CORR_RX = 0.6"
expect_fail "PUSCH UL_CB_4PORT invalid spatial rho" \
    "${UL_TIME_BASE[@]}" "TDL_SPATIAL_CORR_TX = 1"
expect_fail "PUSCH UL_CB_4PORT spatial LOS unsupported" \
    "${UL_TIME_BASE[@]}" "TDL_SPATIAL_CORR_RX = 0.6" "TDL_PROFILE = D"

expect_pass "PUSCH SM_2X2 TDL" "BER" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0" "MIMO_MODE = SM_2X2"

expect_pass "PUSCH SM_2X2 HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0" "MIMO_MODE = SM_2X2" "${HQ[@]}"

expect_pass "PUSCH SM_2X2 HARQ flat" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = SM_2X2" "${HQ[@]}"

expect_pass "PUSCH UL_EIGEN_BF flat" "BER_Genie" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF"

expect_pass "PUSCH UL_EIGEN_BF TDL" "BER_Genie" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF"

expect_pass "PUSCH UL_EIGEN_BF HARQ flat" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF" "${HQ[@]}"

expect_pass "PUSCH UL_EIGEN_BF HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF" "${HQ[@]}"

expect_pass "PUSCH UL_EIGEN_BF_2TX flat" "BER_Genie" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF_2TX"

expect_pass "PUSCH UL_EIGEN_BF_2TX TDL" "BER_Genie" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF_2TX"

expect_pass "PUSCH UL_EIGEN_BF_2TX HARQ flat" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF_2TX" "${HQ[@]}"

expect_pass "PUSCH UL_EIGEN_BF_2TX HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF_2TX" "${HQ[@]}"

expect_pass "PUSCH UL_EIGEN_BF_4TX flat" "BER_Genie" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF_4TX"

expect_pass "PUSCH UL_EIGEN_BF_4TX TDL" "BER_Genie" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF_4TX"

expect_pass "PUSCH UL_EIGEN_BF_4TX HARQ flat" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "CHANNEL_MODEL = FLAT_FADING" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF_4TX" "${HQ[@]}"

expect_pass "PUSCH UL_EIGEN_BF_4TX HARQ TDL" "BLER(HARQ)" \
    "${BASE[@]}" "${PUSCH_COMMON[@]}" "${TDL[@]}" "TRANSFORM_PRECODING = 0" "MIMO_MODE = UL_EIGEN_BF_4TX" "${HQ[@]}"

# ──────────────────────────────────────────
# GROUP 5: control/reference channels
# ──────────────────────────────────────────
group_heading "GROUP 5: control/reference channels"

CTRL_BASE=("MCS_INDEX = 0" "MCS_TABLE = TABLE1" "${BASE[@]}")

expect_pass "PBCH AWGN" "BLER" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PBCH" "CHANNEL_MODEL = AWGN"
expect_pass "PBCH TDL"  "BLER" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PBCH" "${TDL[@]}"
expect_pass "PDCCH AWGN" "BER" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PDCCH" "CHANNEL_MODEL = AWGN"
expect_pass "PDCCH TDL"  "BER" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PDCCH" "${TDL[@]}"
expect_pass "CSI-RS" "" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = CSIRS" "CHANNEL_MODEL = AWGN"
expect_pass "SRS"    "" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = SRS"   "CHANNEL_MODEL = AWGN"

# PUCCH_UCI_BITS는 의도적으로 여기 넣지 않는다 -- config_parser.c의 kv_get()은
# 같은 키가 여러 번 나오면 "첫 번째" 값을 쓴다(2026-09-10, PHY-01 검증 중 발견).
# 이 배열에 기본값을 넣어두면 아래 각 테스트가 뒤에 붙이는
# "PUCCH_UCI_BITS = N" override가 전부 조용히 무시되고 이 기본값만 쓰인다 --
# 예전엔 pucch.c가 범위를 벗어난 값을 무조건 clamp했기 때문에 이 override
# 무시가 드러나지 않았을 뿐, F2/F3 테스트들은 실제로는 의도한 비트수를 한
# 번도 검증하지 못하고 있었다. 각 테스트가 자기 PUCCH_UCI_BITS를 직접 명시한다.
PUCCH_BASE=("${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PUCCH"
            "PUCCH_NUM_SYMBOLS = 4" "PUCCH_NUM_PRB = 1")

expect_pass "PUCCH F0 AWGN"     "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 0" "PUCCH_UCI_BITS = 1" "CHANNEL_MODEL = AWGN"
expect_pass "PUCCH F1 AWGN"     "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 1" "PUCCH_UCI_BITS = 1" "CHANNEL_MODEL = AWGN"
expect_pass "PUCCH F1 TDL"      "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 1" "PUCCH_UCI_BITS = 1" "${TDL[@]}"
expect_pass "PUCCH F2 AWGN"     "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 2" "PUCCH_UCI_BITS = 10" "CHANNEL_MODEL = AWGN"
expect_pass "PUCCH F3 AWGN"     "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 3" "PUCCH_UCI_BITS = 11" "CHANNEL_MODEL = AWGN"
expect_pass "PUCCH F3 TDL"      "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 3" "PUCCH_UCI_BITS = 11" "${TDL[@]}"
expect_pass "PUCCH F1 TDL HARQ" "BLER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 1" "PUCCH_UCI_BITS = 1" "${TDL[@]}" "${HQ[@]}"
expect_pass "PUCCH F3 TDL HARQ" "BLER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 3" "PUCCH_UCI_BITS = 11" "${TDL[@]}" "${HQ[@]}"

PRACH_BASE=("${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PRACH"
            "PRACH_ROOT_SEQ_INDEX = 1" "PRACH_NUM_CS = 13" "PRACH_MAX_DELAY_SAMPLES = 8")
expect_pass "PRACH SHORT AWGN" "" "${PRACH_BASE[@]}" "PRACH_FORMAT = SHORT" "CHANNEL_MODEL = AWGN"
expect_pass "PRACH SHORT TDL"  "" "${PRACH_BASE[@]}" "PRACH_FORMAT = SHORT" "CHANNEL_MODEL = TDL" "TDL_DELAY_SPREAD_NS = 100"
expect_pass "PRACH LONG AWGN"  "" "${PRACH_BASE[@]}" "PRACH_FORMAT = LONG"  "CHANNEL_MODEL = AWGN"

# PHYSICAL_CHANNEL=ULPC previously had zero regression coverage (only ever
# smoke-tested manually via CLI, tasks/todo.md 2026-09-11) -- TPC decision/
# f(i)-accumulator correctness itself is covered by tests/test_link_
# adaptation.c's unit tests (calls the real ulpc_tpc_decide()), so these
# stay CLI-level smoke/behavior checks (crash-free + expected branch of
# output text), not numeric re-derivation.
ULPC_BASE=("${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = ULPC")
expect_pass "ULPC fixed PL"        "UL CLPC simulation complete." "${ULPC_BASE[@]}"
expect_pass "ULPC time-varying PL" "UL CLPC simulation complete." "${ULPC_BASE[@]}" \
    "UL_PC_PL_VAR_STD_DB = 5.0" "UL_PC_PL_VAR_CORR = 0.9"
# PL=150dB is above this config's own printed P_CMAX-limit PL (124.4dB for
# the default P0/P_CMAX/alpha/NF/SINR_target) -- checks the clamping branch
# actually fires, not just that the binary exits 0.
expect_pass "ULPC P_CMAX clamped (cell edge)" "P_CMAX 클램핑 발생" "${ULPC_BASE[@]}" \
    "UL_PC_PL_DB = 150.0"

expect_fail "ULPC UL_PC_PL_VAR_STD_DB < 0" \
    "${ULPC_BASE[@]}" "UL_PC_PL_VAR_STD_DB = -1.0"
expect_fail "ULPC UL_PC_PL_VAR_CORR >= 1" \
    "${ULPC_BASE[@]}" "UL_PC_PL_VAR_CORR = 1.0"

}

define_cases
usage() {
    echo 'Usage: bash regression_test.sh [--list | --help | --match TEXT | LABEL ...]'
    echo 'Exact labels select cases; --match uses a literal, case-sensitive substring.'
    echo 'No arguments runs all cases. Quote labels containing spaces.'
}
if [[ $# -eq 1 && "$1" == --help ]]; then usage; exit 0; fi
if [[ $# -eq 1 && "$1" == --list ]]; then
    printf '%s\n' "${LABELS[@]}"
    exit 0
fi
if [[ $# -gt 0 && "$1" == --match ]]; then
    if [[ $# -ne 2 || -z "$2" ]]; then usage >&2; exit 2; fi
    MATCH="$2"
    found=0
    for label in "${LABELS[@]}"; do
        [[ "$label" == *"$MATCH"* ]] && found=1
    done
    if [[ $found -eq 0 ]]; then echo "No cases match: $MATCH" >&2; exit 2; fi
else
    for requested in "$@"; do
        found=0
        for label in "${LABELS[@]}"; do
            [[ "$label" == "$requested" ]] && found=1
        done
        if [[ $found -eq 0 ]]; then
            echo "Unknown case or invalid option: $requested (use --list)" >&2
            exit 2
        fi
    done
fi
TMPD=$(mktemp -d) || exit 1
trap 'rm -rf "$TMPD"' EXIT
MODE=run
define_cases

# ──────────────────────────────────────────
# SUMMARY
# ──────────────────────────────────────────
TOTAL=$((PASS + FAIL))
echo ""
echo "========================================"
echo "Results: $PASS/$TOTAL passed, $FAIL failed"
echo "========================================"
[[ $FAIL -eq 0 ]]
