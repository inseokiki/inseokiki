#ifndef SRS_H
#define SRS_H

#include "utils.h"
#include "config_parser.h"
#include <vector>

// SRS configuration (TS 38.211 §6.4.1.4)
struct SRSParams {
    int mSRS_b;        // SRS bandwidth in RBs (e.g., 4,8,12,16,24,36,48,96...)
    int combSize;      // K_TC: comb size — 2 or 4
    int combOffset;    // k_bar_TC: comb offset 0..K_TC-1
    int cyclicShift;   // n_CS: cyclic shift (0..7 for comb-2, 0..11 for comb-4)
    int seqGroupU;     // u: low-PAPR sequence group (0..29)
    int seqNumV;       // v: sequence number within group (0 or 1)
};

// ZC-based SRS low-PAPR type-1 sequence per TS 38.211 §5.2.2 + §6.4.1.4.2
// Sequence length M = mSRS_b * 12 / combSize
// Cyclic shift alpha = 2pi * n_CS / N_ap (N_ap=8 for comb-2, 12 for comb-4)
ComplexVec srsSequence(const SRSParams& p);

// SRS comb subcarrier positions within [0, numActiveSubcarriers)
// Maps n' = 0..M-1 to k = startSC + k_bar_TC + K_TC * n'
// SRS occupies the first mSRS_b*12 subcarriers of active bandwidth.
std::vector<int> srsSubcarrierIndices(int numActiveSubcarriers,
                                      int mSRS_b, int combSize, int combOffset);

// Run SRS channel sounding NMSE vs SNR simulation
void runSrsSimulation(const L1Config& cfg);

#endif
