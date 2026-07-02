#!/bin/bash
# Run ber_sim for QPSK / 16QAM / 64QAM and save separate result files

CFG="config/ber_config.txt"
BIN="./ber_sim"

run_mod() {
    local mod=$1
    local snr_start=$2
    local snr_end=$3
    local out="results/ber_result_${mod}.txt"
    sed -i "s/^MODULATION = .*/MODULATION = ${mod}/" "$CFG"
    sed -i "s/^SNR_START = .*/SNR_START = ${snr_start}/" "$CFG"
    sed -i "s/^SNR_END   = .*/SNR_END   = ${snr_end}/" "$CFG"
    $BIN "$CFG" > "$out"
    echo "[$mod] Eb/N0 ${snr_start}~${snr_end} dB -> $out"
}

run_mod QPSK   0  10
run_mod 16QAM  0  16
run_mod 64QAM  0  22

echo "Done."
