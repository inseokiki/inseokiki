#include "modulation.h"
#include <cmath>
#include <stdexcept>
#include <limits>

// ============================================================
// Helper functions (internal)
// ============================================================

// Normalization factor for unit average power: 1/sqrt(E[|s|^2])
static double getNormFactor(int bitsPerSymbol) {
    switch (bitsPerSymbol) {
        case 2:  return 1.0 / std::sqrt(2.0);    // QPSK
        case 4:  return 1.0 / std::sqrt(10.0);   // 16QAM
        case 6:  return 1.0 / std::sqrt(42.0);   // 64QAM
        case 8:  return 1.0 / std::sqrt(170.0);  // 256QAM
        default: return 1.0;
    }
}

// Compute unnormalized PAM level from dimension bits using 3GPP TS 38.211 recursive formula.
//
// 3GPP constellation mapping (I component example):
//   QPSK:   I = (1-2*b0)
//   16QAM:  I = (1-2*b0) * [2 - (1-2*b2)]
//   64QAM:  I = (1-2*b0) * [4 - (1-2*b2) * (2 - (1-2*b4))]
//   256QAM: I = (1-2*b0) * [8 - (1-2*b2) * (4 - (1-2*b4) * (2 - (1-2*b6)))]
//
// bits: array of dimension bits (e.g., {b0, b2, b4} for I of 64QAM)
// numBits: number of bits per dimension (bitsPerSymbol / 2)
static double computePamLevel(const int* bits, int numBits) {
    if (numBits == 1) {
        return static_cast<double>(1 - 2 * bits[0]);
    }

    // Start from innermost: 2 - (1-2*b[n-1])
    double inner = 2.0 - (1 - 2 * bits[numBits - 1]);

    // Build outward
    for (int k = numBits - 2; k >= 1; k--) {
        double power = static_cast<double>(1 << (numBits - k)); // 2^(numBits-k)
        inner = power - (1 - 2 * bits[k]) * inner;
    }

    return (1 - 2 * bits[0]) * inner;
}

// PAM entry for constellation table
struct PamEntry {
    double level;   // Unnormalized PAM level
    int bits;       // Bit pattern as integer (MSB first)
};

// Build PAM constellation table for one I/Q dimension.
// For bitsPerDim bits, produces 2^bitsPerDim entries.
static std::vector<PamEntry> buildPamTable(int bitsPerDim) {
    int numLevels = 1 << bitsPerDim;
    std::vector<PamEntry> table(numLevels);

    for (int idx = 0; idx < numLevels; idx++) {
        int bitArray[4]; // max 4 bits per dimension (256QAM)
        for (int k = 0; k < bitsPerDim; k++) {
            bitArray[k] = (idx >> (bitsPerDim - 1 - k)) & 1;
        }
        table[idx].level = computePamLevel(bitArray, bitsPerDim);
        table[idx].bits = idx;
    }
    return table;
}

// ============================================================
// Public API
// ============================================================

int getBitsPerSymbol(const std::string& modulation) {
    if (modulation == "QPSK")   return 2;
    if (modulation == "16QAM")  return 4;
    if (modulation == "64QAM")  return 6;
    if (modulation == "256QAM") return 8;
    throw std::invalid_argument("Unknown modulation: " + modulation);
}

// General QAM modulator using 3GPP TS 38.211 constellation mapping.
// bits.size() must be a multiple of bitsPerSymbol.
ComplexVec qamModulate(const std::vector<int>& bits, const std::string& modulation) {
    int bps = getBitsPerSymbol(modulation);
    int bitsPerDim = bps / 2;
    double norm = getNormFactor(bps);

    int numSymbols = bits.size() / bps;
    ComplexVec symbols(numSymbols);

    for (int i = 0; i < numSymbols; i++) {
        int offset = i * bps;

        // I dimension: even-indexed bits (b0, b2, b4, ...)
        int iBits[4];
        for (int k = 0; k < bitsPerDim; k++) {
            iBits[k] = bits[offset + 2 * k];
        }

        // Q dimension: odd-indexed bits (b1, b3, b5, ...)
        int qBits[4];
        for (int k = 0; k < bitsPerDim; k++) {
            qBits[k] = bits[offset + 2 * k + 1];
        }

        double I = computePamLevel(iBits, bitsPerDim) * norm;
        double Q = computePamLevel(qBits, bitsPerDim) * norm;

        symbols[i] = Complex(I, Q);
    }
    return symbols;
}

// General QAM hard demodulator (minimum Euclidean distance per PAM dimension).
std::vector<int> qamDemodulate(const ComplexVec& symbols, const std::string& modulation) {
    int bps = getBitsPerSymbol(modulation);
    int bitsPerDim = bps / 2;
    double norm = getNormFactor(bps);

    auto pamTable = buildPamTable(bitsPerDim);
    int numLevels = static_cast<int>(pamTable.size());

    std::vector<int> bits(symbols.size() * bps);

    for (size_t i = 0; i < symbols.size(); i++) {
        double rI = symbols[i].real();
        double rQ = symbols[i].imag();

        // Find nearest PAM level for I dimension
        double minDistI = std::numeric_limits<double>::max();
        int bestIdxI = 0;
        for (int j = 0; j < numLevels; j++) {
            double d = rI - pamTable[j].level * norm;
            double dist = d * d;
            if (dist < minDistI) {
                minDistI = dist;
                bestIdxI = j;
            }
        }

        // Find nearest PAM level for Q dimension
        double minDistQ = std::numeric_limits<double>::max();
        int bestIdxQ = 0;
        for (int j = 0; j < numLevels; j++) {
            double d = rQ - pamTable[j].level * norm;
            double dist = d * d;
            if (dist < minDistQ) {
                minDistQ = dist;
                bestIdxQ = j;
            }
        }

        // Reconstruct interleaved bits (b0,b1,b2,b3,... = I0,Q0,I1,Q1,...)
        int offset = i * bps;
        for (int k = 0; k < bitsPerDim; k++) {
            bits[offset + 2 * k]     = (bestIdxI >> (bitsPerDim - 1 - k)) & 1;
            bits[offset + 2 * k + 1] = (bestIdxQ >> (bitsPerDim - 1 - k)) & 1;
        }
    }
    return bits;
}

// General QAM soft demodulator (max-log LLR approximation).
//
// For each bit position k:
//   LLR(k) = (1/sigma^2_real) * [min_{s:bk=1} dist^2 - min_{s:bk=0} dist^2]
//
// Since square QAM is separable, I-bits depend only on Re(r) and Q-bits on Im(r).
// noiseVar = N0 (noise power per complex sample).
std::vector<double> qamDemapLLR(const ComplexVec& symbols, const std::string& modulation, double noiseVar) {
    int bps = getBitsPerSymbol(modulation);
    int bitsPerDim = bps / 2;
    double norm = getNormFactor(bps);

    auto pamTable = buildPamTable(bitsPerDim);
    int numLevels = static_cast<int>(pamTable.size());

    // Pre-compute normalized PAM levels
    std::vector<double> levels(numLevels);
    for (int j = 0; j < numLevels; j++) {
        levels[j] = pamTable[j].level * norm;
    }

    std::vector<double> llr(symbols.size() * bps);
    double scale = 2.0 / noiseVar;  // 1/sigma^2_real = 2/N0

    for (size_t i = 0; i < symbols.size(); i++) {
        double rI = symbols[i].real();
        double rQ = symbols[i].imag();

        for (int k = 0; k < bitsPerDim; k++) {
            // I-dimension bit at position 2k
            double minDist0_I = std::numeric_limits<double>::max();
            double minDist1_I = std::numeric_limits<double>::max();

            // Q-dimension bit at position 2k+1
            double minDist0_Q = std::numeric_limits<double>::max();
            double minDist1_Q = std::numeric_limits<double>::max();

            for (int j = 0; j < numLevels; j++) {
                int bitVal = (pamTable[j].bits >> (bitsPerDim - 1 - k)) & 1;

                double dI = rI - levels[j];
                double distI = dI * dI;

                double dQ = rQ - levels[j];
                double distQ = dQ * dQ;

                if (bitVal == 0) {
                    if (distI < minDist0_I) minDist0_I = distI;
                    if (distQ < minDist0_Q) minDist0_Q = distQ;
                } else {
                    if (distI < minDist1_I) minDist1_I = distI;
                    if (distQ < minDist1_Q) minDist1_Q = distQ;
                }
            }

            // LLR > 0 means bit=0 more likely, LLR < 0 means bit=1 more likely
            llr[i * bps + 2 * k]     = scale * (minDist1_I - minDist0_I);
            llr[i * bps + 2 * k + 1] = scale * (minDist1_Q - minDist0_Q);
        }
    }
    return llr;
}

// MMSE-aware soft demodulator.
// rxSymbols is the biased MMSE output: y_eq = h*/(|h|^2+N0) * y
// Constellation points are scaled by alpha = |h|^2/(|h|^2+N0),
// and the noise variance used is sigma2_eff = N0*|h|^2/(|h|^2+N0)^2.
// Deep-fade subcarriers (|h|^2 < 1e-10) get zero LLRs.
std::vector<double> qamDemapLLR_mmse(const ComplexVec& rxSymbols,
                                      const std::string& modulation,
                                      const ComplexVec& hData,
                                      double N0) {
    int bps = getBitsPerSymbol(modulation);
    int bitsPerDim = bps / 2;
    double norm = getNormFactor(bps);

    auto pamTable = buildPamTable(bitsPerDim);
    int numLevels = static_cast<int>(pamTable.size());

    std::vector<double> levels(numLevels);
    for (int j = 0; j < numLevels; j++) {
        levels[j] = pamTable[j].level * norm;
    }

    std::vector<double> llr(rxSymbols.size() * bps, 0.0);

    for (size_t i = 0; i < rxSymbols.size(); i++) {
        double hPow = std::norm(hData[i]);
        if (hPow < 1e-10) continue;  // deep fade → LLR = 0 (erasure)

        double denom  = hPow + N0;
        double alpha  = hPow / denom;            // MMSE bias
        double sigma2 = N0 * hPow / (denom * denom);  // effective noise variance
        double scale  = 2.0 / sigma2;

        double rI = rxSymbols[i].real();
        double rQ = rxSymbols[i].imag();

        for (int k = 0; k < bitsPerDim; k++) {
            double minDist0_I = std::numeric_limits<double>::max();
            double minDist1_I = std::numeric_limits<double>::max();
            double minDist0_Q = std::numeric_limits<double>::max();
            double minDist1_Q = std::numeric_limits<double>::max();

            for (int j = 0; j < numLevels; j++) {
                int bitVal = (pamTable[j].bits >> (bitsPerDim - 1 - k)) & 1;
                double scaledLev = alpha * levels[j];  // MMSE-scaled constellation

                double dI = rI - scaledLev;
                double distI = dI * dI;
                double dQ = rQ - scaledLev;
                double distQ = dQ * dQ;

                if (bitVal == 0) {
                    if (distI < minDist0_I) minDist0_I = distI;
                    if (distQ < minDist0_Q) minDist0_Q = distQ;
                } else {
                    if (distI < minDist1_I) minDist1_I = distI;
                    if (distQ < minDist1_Q) minDist1_Q = distQ;
                }
            }

            llr[i * bps + 2*k]     = scale * (minDist1_I - minDist0_I);
            llr[i * bps + 2*k + 1] = scale * (minDist1_Q - minDist0_Q);
        }
    }
    return llr;
}

// ============================================================
// Legacy QPSK functions (kept for backward compatibility)
// ============================================================

ComplexVec qpskModulate(const std::vector<int>& bits) {
    int numSymbols = bits.size() / 2;
    ComplexVec symbols(numSymbols);
    double scale = 1.0 / std::sqrt(2.0);

    for (int i = 0; i < numSymbols; i++) {
        int b0 = bits[2 * i];
        int b1 = bits[2 * i + 1];
        double I = (1 - 2 * b0) * scale;
        double Q = (1 - 2 * b1) * scale;
        symbols[i] = Complex(I, Q);
    }
    return symbols;
}

std::vector<int> qpskDemodulate(const ComplexVec& symbols) {
    std::vector<int> bits(symbols.size() * 2);
    for (size_t i = 0; i < symbols.size(); i++) {
        bits[2 * i]     = (symbols[i].real() < 0) ? 1 : 0;
        bits[2 * i + 1] = (symbols[i].imag() < 0) ? 1 : 0;
    }
    return bits;
}
