#include "polar_rate_match.h"
#include <cmath>
#include <algorithm>

// TS 38.212 Table 5.3.1.1-1: Sub-block interleaver pattern for polar codes
// Bit-reversal permutation for length 32 (J_max = 5)
static const int subBlockInterleaverPattern[32] = {
     0,  1,  2,  4,  3,  5,  6,  7,
     8, 16,  9, 17, 10, 18, 11, 19,
    12, 20, 13, 21, 14, 22, 15, 23,
    24, 25, 26, 28, 27, 29, 30, 31
};

// Generate sub-block interleaver indices for size N
static std::vector<int> getSubBlockInterleaving(int N) {
    // J_max such that 2^J_max covers the interleaving pattern
    // The pattern maps bit-reversed indices from 0..31 scaled to N
    // For N, we need indices [0, N-1]
    // The interleaver operates on N bits using the pattern table

    std::vector<int> indices;
    indices.reserve(N);

    // Scale factor: N / 32
    // For each entry in the 32-element pattern, generate N/32 consecutive indices
    int ratio = N / 32;

    for (int i = 0; i < 32; i++) {
        int base = subBlockInterleaverPattern[i] * ratio;
        for (int j = 0; j < ratio; j++) {
            indices.push_back(base + j);
        }
    }

    return indices;
}

std::vector<int> polarRateMatch(const std::vector<int>& codedBits, int E, int K) {
    int N = codedBits.size();

    // Step 1: Sub-block interleaving
    std::vector<int> interleaved(N);
    if (N >= 32) {
        std::vector<int> pi = getSubBlockInterleaving(N);
        for (int i = 0; i < N; i++) {
            interleaved[i] = codedBits[pi[i]];
        }
    } else {
        interleaved = codedBits;
    }

    // Step 2: Rate matching (bit selection)
    std::vector<int> output(E);

    if (E >= N) {
        // Repetition: repeat interleaved bits cyclically
        for (int i = 0; i < E; i++) {
            output[i] = interleaved[i % N];
        }
    } else {
        // E < N: puncturing or shortening
        // TS 38.212: if K/E <= 7/16, use puncturing; otherwise shortening
        double ratio_ke = static_cast<double>(K) / E;
        if (ratio_ke <= 7.0 / 16.0) {
            // Puncturing: take the last E bits
            for (int i = 0; i < E; i++) {
                output[i] = interleaved[N - E + i];
            }
        } else {
            // Shortening: take the first E bits
            for (int i = 0; i < E; i++) {
                output[i] = interleaved[i];
            }
        }
    }

    return output;
}

std::vector<double> polarRateDematch(const std::vector<double>& llr, int N, int E, int K) {
    // Reverse the rate matching: map E LLRs back to N positions

    // Step 1: Reverse bit selection
    std::vector<double> deinterleaved(N, 0.0);

    if (E >= N) {
        // Repetition was used: combine LLRs at same positions
        for (int i = 0; i < E; i++) {
            deinterleaved[i % N] += llr[i];
        }
    } else {
        // Puncturing or shortening
        double ratio_ke = static_cast<double>(K) / E;
        if (ratio_ke <= 7.0 / 16.0) {
            // Puncturing: last E positions were transmitted
            // Punctured positions get LLR = 0 (no information)
            for (int i = 0; i < E; i++) {
                deinterleaved[N - E + i] = llr[i];
            }
        } else {
            // Shortening: first E positions were transmitted
            // Shortened positions are known to be 0, so LLR = +infinity (large positive)
            for (int i = 0; i < E; i++) {
                deinterleaved[i] = llr[i];
            }
            for (int i = E; i < N; i++) {
                deinterleaved[i] = 100.0;  // Large positive LLR (known frozen/zero)
            }
        }
    }

    // Step 2: Reverse sub-block interleaving
    std::vector<double> output(N, 0.0);
    if (N >= 32) {
        std::vector<int> pi = getSubBlockInterleaving(N);
        for (int i = 0; i < N; i++) {
            output[pi[i]] = deinterleaved[i];
        }
    } else {
        output = deinterleaved;
    }

    return output;
}
