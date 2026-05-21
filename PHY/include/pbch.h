#ifndef PBCH_H
#define PBCH_H

#include "config_parser.h"
#include <vector>

// PBCH (Physical Broadcast Channel) simulation
// Fixed parameters per 3GPP TS 38.212/38.211:
//   Payload: 32 bits (MIB)
//   CRC: CRC-24C (24 bits) -> 56 bits total
//   Coding: Polar(512, 56)
//   Rate matching: 512 -> 864 bits
//   Modulation: QPSK -> 432 symbols
//
// Output: BLER vs SNR

void runPbchSimulation(const L1Config& cfg);

#endif
