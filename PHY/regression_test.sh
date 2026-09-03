#!/usr/bin/env bash
# PHY LLS regression test suite
# Usage: cd PHY && bash regression_test.sh
# Requires: lls_sim_c binary already built (make -f c_Makefile)

set -uo pipefail
cd "$(dirname "$0")"

SIM=./lls_sim_c
PASS=0; FAIL=0
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

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
expect_fail() {
    local label="$1"; shift
    local cfg="$TMPD/${label// /_}.cfg"
    write_cfg "$cfg" "$@"
    if run_sim "$cfg" > /dev/null 2>&1; then
        fail "$label (expected non-zero exit)"
    else
        pass "$label"
    fi
}

# expect_pass LABEL PATTERN KEY=VAL ...
# PATTERN="" to skip pattern check
expect_pass() {
    local label="$1"; shift
    local pattern="$1"; shift
    local cfg="$TMPD/${label// /_}.cfg"
    write_cfg "$cfg" "$@"
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
echo ""
echo "=== GROUP 1: config validation ==="

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


expect_fail "OLLA_ENABLE=1 + unsupported MIMO_MODE (CL_4PORT) has no dedicated function" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = CL_4PORT" "CHANNEL_MODEL = AWGN" "CODING = LDPC" "EQUALIZER = MMSE" \
    "OLLA_ENABLE = 1"

expect_fail "BEAM_MGMT_RX_SWEEP=1 + TDL has no dedicated function (AWGN only)" \
    "MCS_INDEX = 5" "MCS_TABLE = TABLE1" "SNR_START = 10" "SNR_END = 10" "SNR_STEP = 1" "NUM_TRIALS = 20" \
    "PHYSICAL_CHANNEL = PDSCH" "USE_DMRS = 1" "MIMO_MODE = BEAM_MGMT" "CHANNEL_MODEL = TDL" "CODING = LDPC" "EQUALIZER = MMSE" \
    "BEAM_MGMT_RX_SWEEP = 1"

# ──────────────────────────────────────────
# GROUP 2: MCS table dispatch (P0-1 regression)
# ──────────────────────────────────────────
echo ""
echo "=== GROUP 2: MCS table dispatch ==="

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
echo ""
echo "=== GROUP 3: PDSCH dispatch ==="

expect_pass "PDSCH SISO AWGN" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SISO" "CHANNEL_MODEL = AWGN"

expect_pass "PDSCH SISO TDL" "BER" \
    "${BASE[@]}" "${PDSCH_COMMON[@]}" "MIMO_MODE = SISO" "${TDL[@]}"

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
echo ""
echo "=== GROUP 4: PUSCH dispatch ==="

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
echo ""
echo "=== GROUP 5: control/reference channels ==="

CTRL_BASE=("MCS_INDEX = 0" "MCS_TABLE = TABLE1" "${BASE[@]}")

expect_pass "PBCH AWGN" "BLER" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PBCH" "CHANNEL_MODEL = AWGN"
expect_pass "PBCH TDL"  "BLER" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PBCH" "${TDL[@]}"
expect_pass "PDCCH AWGN" "BER" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PDCCH" "CHANNEL_MODEL = AWGN"
expect_pass "PDCCH TDL"  "BER" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PDCCH" "${TDL[@]}"
expect_pass "CSI-RS" "" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = CSIRS" "CHANNEL_MODEL = AWGN"
expect_pass "SRS"    "" "${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = SRS"   "CHANNEL_MODEL = AWGN"

PUCCH_BASE=("${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PUCCH"
            "PUCCH_UCI_BITS = 1" "PUCCH_NUM_SYMBOLS = 4" "PUCCH_NUM_PRB = 1")

expect_pass "PUCCH F0 AWGN"     "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 0" "CHANNEL_MODEL = AWGN"
expect_pass "PUCCH F1 AWGN"     "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 1" "CHANNEL_MODEL = AWGN"
expect_pass "PUCCH F1 TDL"      "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 1" "${TDL[@]}"
expect_pass "PUCCH F2 AWGN"     "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 2" "PUCCH_UCI_BITS = 10" "CHANNEL_MODEL = AWGN"
expect_pass "PUCCH F3 AWGN"     "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 3" "PUCCH_UCI_BITS = 20" "CHANNEL_MODEL = AWGN"
expect_pass "PUCCH F3 TDL"      "BER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 3" "PUCCH_UCI_BITS = 20" "${TDL[@]}"
expect_pass "PUCCH F1 TDL HARQ" "BLER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 1" "${TDL[@]}" "${HQ[@]}"
expect_pass "PUCCH F3 TDL HARQ" "BLER" "${PUCCH_BASE[@]}" "PUCCH_FORMAT = 3" "PUCCH_UCI_BITS = 20" "${TDL[@]}" "${HQ[@]}"

PRACH_BASE=("${CTRL_BASE[@]}" "PHYSICAL_CHANNEL = PRACH"
            "PRACH_ROOT_SEQ_INDEX = 1" "PRACH_NUM_CS = 13" "PRACH_MAX_DELAY_SAMPLES = 8")
expect_pass "PRACH SHORT AWGN" "" "${PRACH_BASE[@]}" "PRACH_FORMAT = SHORT" "CHANNEL_MODEL = AWGN"
expect_pass "PRACH SHORT TDL"  "" "${PRACH_BASE[@]}" "PRACH_FORMAT = SHORT" "CHANNEL_MODEL = TDL" "TDL_DELAY_SPREAD_NS = 100"
expect_pass "PRACH LONG AWGN"  "" "${PRACH_BASE[@]}" "PRACH_FORMAT = LONG"  "CHANNEL_MODEL = AWGN"

# ──────────────────────────────────────────
# SUMMARY
# ──────────────────────────────────────────
TOTAL=$((PASS + FAIL))
echo ""
echo "========================================"
echo "Results: $PASS/$TOTAL passed, $FAIL failed"
echo "========================================"
[[ $FAIL -eq 0 ]]
