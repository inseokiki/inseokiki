#ifndef DMRS_H
#define DMRS_H

#include "utils.h"
#include <cstdint>

// Gold sequence generator per 3GPP TS 38.211 Section 5.2.1
// Uses two degree-31 m-sequences with Nc=1600 advance
std::vector<int> goldSequence(uint32_t cInit, int length);

// DMRS complex-valued sequence per 3GPP TS 38.211 Section 7.4.1.1.1
// r(n) = (1/sqrt(2)) * [(1-2c(2n)) + j*(1-2c(2n+1))]
ComplexVec dmrsSequence(uint32_t cInit, int numPilots);

// Pilot subcarrier indices within active subcarriers (Type 1 mapping)
// Even subcarriers per RB: {0,2,4,6,8,10} -> 6*numRB total pilots
std::vector<int> dmrsPilotIndices(int numRB);

// Data subcarrier indices in a DMRS symbol (Type 1 mapping)
// Odd subcarriers per RB: {1,3,5,7,9,11} -> 6*numRB total data REs
std::vector<int> dmrsDataIndices(int numRB);

#endif
