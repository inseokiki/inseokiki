#ifndef CSI_RS_H
#define CSI_RS_H

#include "utils.h"
#include "config_parser.h"
#include <cstdint>
#include <vector>

// CSI-RS mapping configuration (TS 38.211 §7.4.1.5)
struct CSIRSMapConfig {
    int row;               // Table 7.4.1.5.3-1 row: 1=density3-1p, 2=density1-1p,
                           //   3=density1-2p, 4=density1-4p
    uint32_t scramblingID; // N_ID^CSI-RS (0..1023)
    int slotIdx;           // n_s_f^mu: slot number in radio frame
    int symbolIdx;         // l_0: OFDM symbol index (0..13)
    int subcarrierOffset;  // k_0: first subcarrier within RB (0..11)
};

// CSI-RS complex sequence per TS 38.211 §7.4.1.5.2 (Gold-based)
// Same shape as DMRS: r(m) = (1/sqrt2)*[(1-2c(2m)) + j*(1-2c(2m+1))]
ComplexVec csirsSequence(uint32_t cInit, int length);

// Per-port CSI-RS subcarrier indices within numRB active RBs
// Returns absolute indices in [0, numRB*12), sorted ascending.
// For multi-port rows (3,4) returns all ports interleaved:
//   [p0_rb0, p1_rb0, ..., p0_rb1, p1_rb1, ...]
std::vector<int> csirsSubcarrierIndices(int numRB, const CSIRSMapConfig& cfg);

// Number of pilots per RB for a given row
int csirsPilotsPerRB(int row);

// Run CSI-RS channel estimation NMSE vs SNR simulation
void runCsirsSimulation(const L1Config& cfg);

#endif
