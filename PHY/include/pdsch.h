#ifndef PDSCH_H
#define PDSCH_H

#include "config_parser.h"
#include <vector>

// PDSCH (Physical Downlink Shared Channel) simulation
// Per 3GPP TS 38.212/38.214:
//   MCS index -> modulation + code rate (from MCS table)
//   CRC: CRC-24A (24 bits)
//   Coding: LDPC (BG1 or BG2)
//     BG1: A > 3824 or R > 0.67
//     BG2: otherwise
//   Rate matching: simple circular buffer
//   Single code block (TB <= 8448 bits, no CB segmentation)
//
// Output: BER, BLER vs SNR

void runPdschSimulation(const L1Config& cfg);

// PDSCH simulation with DMRS-based channel estimation.
// Each trial uses one OFDM symbol with DMRS Type 1 (pilots at even subcarriers
// per RB) interleaved with data (odd subcarriers).  Flat Rayleigh block-fading
// channel is estimated via LS + linear interpolation and equalized with ZF.
void runPdschDmrsSimulation(const L1Config& cfg);

#endif
