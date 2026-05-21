#ifndef MODULATION_H
#define MODULATION_H

#include "utils.h"
#include <string>

// Get bits per symbol for given modulation scheme
// QPSK=2, 16QAM=4, 64QAM=6, 256QAM=8
int getBitsPerSymbol(const std::string& modulation);

// General QAM Modulation (3GPP TS 38.211 Gray-coded constellation)
ComplexVec qamModulate(const std::vector<int>& bits, const std::string& modulation);

// General QAM Hard Demodulation (minimum distance)
std::vector<int> qamDemodulate(const ComplexVec& symbols, const std::string& modulation);

// General QAM Soft Demodulation (max-log LLR approximation)
std::vector<double> qamDemapLLR(const ComplexVec& symbols, const std::string& modulation, double noiseVar);

// MMSE-aware soft demodulation.
// rxSymbols: biased MMSE output  y_eq = w*y  (NOT alpha-normalized).
// hData:     channel estimates at each symbol.
// N0:        noise variance per subcarrier (1/SNR_linear).
// Accounts for MMSE bias alpha=|h|^2/(|h|^2+N0) and effective noise
// variance sigma2_eff = N0*|h|^2/(|h|^2+N0)^2 in LLR computation.
// Deep-fade subcarriers (|h|^2 < 1e-10) produce zero LLRs.
std::vector<double> qamDemapLLR_mmse(const ComplexVec& rxSymbols,
                                      const std::string& modulation,
                                      const ComplexVec& hData,
                                      double N0);

// Legacy QPSK functions (backward compatibility)
ComplexVec qpskModulate(const std::vector<int>& bits);
std::vector<int> qpskDemodulate(const ComplexVec& symbols);

#endif
