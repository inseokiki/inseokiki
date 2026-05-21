#ifndef POLAR_RATE_MATCH_H
#define POLAR_RATE_MATCH_H

#include <vector>

// Polar code rate matching per TS 38.212 Section 5.3.1
// Supports sub-block interleaving, repetition, puncturing, shortening

// Rate match polar coded bits from N to E output bits
// codedBits: N polar coded bits
// E: desired output length
// K: number of information bits (used to choose puncturing vs shortening)
std::vector<int> polarRateMatch(const std::vector<int>& codedBits, int E, int K);

// Rate dematch: map E received LLR values back to N-length LLR vector
// llr: E received LLR values
// N: polar code block length
// E: rate-matched length (same as llr.size())
// K: number of information bits
std::vector<double> polarRateDematch(const std::vector<double>& llr, int N, int E, int K);

#endif
