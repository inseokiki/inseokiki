#ifndef PDCCH_H
#define PDCCH_H

#include "config_parser.h"
#include <vector>

// PDCCH (Physical Downlink Control Channel) simulation
// Per 3GPP TS 38.212/38.211:
//   DCI payload: configurable (e.g., 39 bits)
//   CRC: CRC-24C with RNTI masking (24 bits)
//   Coding: Polar
//   Modulation: QPSK
//   Aggregation Level (AL): 1, 2, 4, 8, 16
//     Coded bits per AL: 108*AL (1 CCE = 6 REG = 54 RE = 108 bits)
//
// Blind Decoding simulation:
//   TX: encode DCI at one candidate position
//   RX: try all candidates across all ALs
//   CSS candidates: AL4:4, AL8:2, AL16:1 (7 total)
//   USS candidates: configurable
//
// Output: P_detect, P_miss, P_false_alarm, BER

void runPdcchSimulation(const L1Config& cfg);

#endif
