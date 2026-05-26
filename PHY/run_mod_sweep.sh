#!/bin/bash

# Run lls_sim_c for QPSK / 16QAM / 64QAM and save separate result files

CFG="config/ber_config.txt"
CFG_BAK="config/ber_config.txt.bak"
BIN="./ber_sim_c"

cp "$CFG" "$CFG_BAK"

run_mod() {
    local mod=$1
    local snr_start=$2
    local snr_end=$3
    local out="ber_result_${mod}.txt"
    sed -i "s/^MODULATION = .*/MODULATION = ${mod}/" "$CFG"
    sed -i "s/^SNR_START = .*/SNR_START = ${snr_start}/" "$CFG"
    sed -i "s/^SNR_END   = .*/SNR_END   = ${snr_end}/" "$CFG"
    $BIN > "$out"
    echo "[$mod] Eb/N0 ${snr_start}~${snr_end} dB -> $out"
}

run_mod QPSK   0  10
run_mod 16QAM  0  16
run_mod 64QAM  0  22

cp "$CFG_BAK" "$CFG"
echo "Done. Config restored."
