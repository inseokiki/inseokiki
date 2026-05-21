#include "dmrs.h"
#include <cmath>

// 31-bit LFSR-based Gold sequence (3GPP TS 38.211 Section 5.2.1).
// x1: x^31 + x^3 + 1
// x2: x^31 + x^3 + x^2 + x + 1
// Both registers stored LSB-first in a uint32_t (bits 0..30 used).
std::vector<int> goldSequence(uint32_t cInit, int length) {
    uint32_t x1 = 1u;                      // x1(0)=1, x1(1..30)=0
    uint32_t x2 = cInit & 0x7FFFFFFFu;     // lower 31 bits from cInit

    // Advance Nc = 1600 steps before output
    for (int i = 0; i < 1600; i++) {
        uint32_t b1 = ((x1 >> 3) ^ x1) & 1u;
        x1 = (x1 >> 1) | (b1 << 30);
        uint32_t b2 = ((x2 >> 3) ^ (x2 >> 2) ^ (x2 >> 1) ^ x2) & 1u;
        x2 = (x2 >> 1) | (b2 << 30);
    }

    std::vector<int> c(length);
    for (int i = 0; i < length; i++) {
        c[i] = static_cast<int>((x1 ^ x2) & 1u);
        uint32_t b1 = ((x1 >> 3) ^ x1) & 1u;
        x1 = (x1 >> 1) | (b1 << 30);
        uint32_t b2 = ((x2 >> 3) ^ (x2 >> 2) ^ (x2 >> 1) ^ x2) & 1u;
        x2 = (x2 >> 1) | (b2 << 30);
    }
    return c;
}

ComplexVec dmrsSequence(uint32_t cInit, int numPilots) {
    std::vector<int> c = goldSequence(cInit, 2 * numPilots);
    const double scale = 1.0 / std::sqrt(2.0);
    ComplexVec r(numPilots);
    for (int n = 0; n < numPilots; n++) {
        r[n] = Complex(scale * (1 - 2 * c[2*n]), scale * (1 - 2 * c[2*n+1]));
    }
    return r;
}

std::vector<int> dmrsPilotIndices(int numRB) {
    std::vector<int> idx;
    idx.reserve(6 * numRB);
    for (int rb = 0; rb < numRB; rb++) {
        int base = rb * 12;
        for (int k = 0; k < 12; k += 2) {
            idx.push_back(base + k);
        }
    }
    return idx;
}

std::vector<int> dmrsDataIndices(int numRB) {
    std::vector<int> idx;
    idx.reserve(6 * numRB);
    for (int rb = 0; rb < numRB; rb++) {
        int base = rb * 12;
        for (int k = 1; k < 12; k += 2) {
            idx.push_back(base + k);
        }
    }
    return idx;
}
