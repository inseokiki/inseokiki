#include "pdcch.h"
#include "crc.h"
#include "polar.h"
#include "polar_rate_match.h"
#include "modulation.h"
#include "channel.h"
#include "utils.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <random>
#include <map>

// Get coded bits for a given aggregation level
// 1 CCE = 6 REG = 54 RE, each RE carries 2 bits (QPSK) = 108 bits per CCE
static int getCodedBitsForAL(int al) {
    return 108 * al;
}

// Get the smallest power-of-2 >= val for Polar code block length
static int getPolarN(int E, int K) {
    // Polar code length N must be power of 2, N >= K, N >= 32
    int nMin = std::max(32, K);
    // N should be such that N >= E for good performance, but can be smaller
    // Per 3GPP: N = min power of 2 >= max(nMin, E) but capped
    // Simplified: N = smallest power of 2 >= max(K, E/2) but at least 32
    int target = std::max(nMin, E);
    // Cap at 512 for PDCCH
    int N = 32;
    while (N < target && N < 512) {
        N *= 2;
    }
    if (N < nMin) N = nMin;
    // Ensure power of 2
    int p = 32;
    while (p < N) p *= 2;
    return p;
}

struct CSSConfig {
    int al;
    int numCandidates;
};

struct USSConfig {
    int al;
    int numCandidates;
};

void runPdcchSimulation(const L1Config& cfg) {
    const int dciSize = cfg.dciSize;
    const int crcBits = 24;
    const int K = dciSize + crcBits;
    const uint16_t rnti = cfg.rnti;
    const int txAL = cfg.pdcchAL;
    const std::string mod = "QPSK";

    // Setup search space candidates
    std::vector<std::pair<int, int>> candidates;  // (AL, candidate_index)

    if (cfg.searchSpace == "CSS") {
        // Common Search Space: AL4:4, AL8:2, AL16:1
        std::vector<CSSConfig> cssConfig = {{4, 4}, {8, 2}, {16, 1}};
        for (auto& c : cssConfig) {
            for (int i = 0; i < c.numCandidates; i++) {
                candidates.push_back({c.al, i});
            }
        }
    } else {
        // USS: Use the configured AL with up to 6 candidates
        // AL1:6, AL2:6, AL4:4, AL8:2, AL16:1
        std::map<int, int> ussMaxCandidates = {{1, 6}, {2, 6}, {4, 4}, {8, 2}, {16, 1}};
        int numCand = ussMaxCandidates.count(txAL) ? ussMaxCandidates[txAL] : 4;
        for (int i = 0; i < numCand; i++) {
            candidates.push_back({txAL, i});
        }
    }

    // Find which candidate index matches TX AL
    int txCandIdx = -1;
    for (size_t i = 0; i < candidates.size(); i++) {
        if (candidates[i].first == txAL && candidates[i].second == 0) {
            txCandIdx = i;
            break;
        }
    }

    int E_tx = getCodedBitsForAL(txAL);
    int N_tx = getPolarN(E_tx, K);

    std::cout << "=== PDCCH Simulation ===" << std::endl;
    std::cout << "DCI size: " << dciSize << " bits" << std::endl;
    std::cout << "CRC: CRC-24C with RNTI masking (" << crcBits << " bits)" << std::endl;
    std::cout << "RNTI: 0x" << std::hex << rnti << std::dec << std::endl;
    std::cout << "TX Aggregation Level: " << txAL << std::endl;
    std::cout << "Polar code: (" << N_tx << ", " << K << ")" << std::endl;
    std::cout << "Rate matching: " << N_tx << " -> " << E_tx << " bits" << std::endl;
    std::cout << "Search Space: " << cfg.searchSpace << std::endl;
    std::cout << "Blind decoding candidates: " << candidates.size() << std::endl;
    for (size_t i = 0; i < candidates.size(); i++) {
        std::cout << "  Candidate " << i << ": AL" << candidates[i].first
                  << " #" << candidates[i].second;
        if ((int)i == txCandIdx) std::cout << " (TX)";
        std::cout << std::endl;
    }
    std::cout << "Trials per SNR: " << cfg.numTrials << std::endl;
    std::cout << std::endl;

    // Generate SNR range
    std::vector<double> snrRange;
    for (double snr = cfg.snrStart; snr <= cfg.snrEnd + 0.001; snr += cfg.snrStep) {
        snrRange.push_back(snr);
    }

    std::cout << std::setw(12) << "SNR (dB)"
              << std::setw(12) << "P_detect"
              << std::setw(12) << "P_miss"
              << std::setw(15) << "P_false_alarm"
              << std::setw(15) << "BER" << std::endl;
    std::cout << std::string(66, '-') << std::endl;

    // Pre-create Polar codecs for each AL used in candidates
    std::map<int, int> alToN;
    std::map<int, int> alToE;
    for (auto& c : candidates) {
        int al = c.first;
        if (alToN.find(al) == alToN.end()) {
            int E = getCodedBitsForAL(al);
            int N = getPolarN(E, K);
            alToN[al] = N;
            alToE[al] = E;
        }
    }

    std::random_device rd;
    std::mt19937 rng(rd());

    for (double snr : snrRange) {
        int detections = 0;
        int misses = 0;
        int falseAlarms = 0;
        int totalBitErrors = 0;
        int totalDetectedBits = 0;

        for (int trial = 0; trial < cfg.numTrials; trial++) {
            // TX: Generate random DCI
            std::vector<int> dci = generateRandomBits(dciSize);

            // Attach CRC-24C with RNTI masking
            std::vector<int> dciWithCRC = attachCRCWithRNTI(dci, CRCType::CRC24C, rnti);

            // Polar encode
            PolarCodec polarTx(N_tx, K);
            std::vector<int> codedBits = polarTx.encode(dciWithCRC);

            // Rate match
            std::vector<int> rateMatched = polarRateMatch(codedBits, E_tx, K);

            // QPSK modulate TX signal
            ComplexVec txSymbols = qamModulate(rateMatched, mod);

            // Add noise to TX signal
            AWGNChannel channel(snr);
            ComplexVec rxTxSymbols = channel.addNoise(txSymbols, 0);

            // RX: Blind decoding over all candidates
            bool detected = false;
            bool falseAlarm = false;

            for (size_t ci = 0; ci < candidates.size(); ci++) {
                int al = candidates[ci].first;
                int E_c = alToE[al];
                int N_c = alToN[al];
                int numSymbols = E_c / 2;  // QPSK: 2 bits per symbol

                ComplexVec rxSymbols;

                if ((int)ci == txCandIdx) {
                    // This is the correct candidate: use actual received signal
                    rxSymbols = rxTxSymbols;
                } else {
                    // Wrong candidate: noise-only signal
                    ComplexVec zeroSignal(numSymbols, Complex(0, 0));
                    rxSymbols = channel.addNoise(zeroSignal, 0);
                }

                // Soft demodulation
                double noiseVar = 1.0 / std::pow(10.0, snr / 10.0);
                std::vector<double> llr = qamDemapLLR(rxSymbols, mod, noiseVar);

                // Rate dematch
                std::vector<double> dematchedLLR = polarRateDematch(llr, N_c, E_c, K);

                // Polar decode
                PolarCodec polarRx(N_c, K);
                std::vector<int> decoded = polarRx.decode(dematchedLLR);

                // CRC check with RNTI
                if (checkCRCWithRNTI(decoded, CRCType::CRC24C, rnti)) {
                    if ((int)ci == txCandIdx) {
                        // Correct detection
                        detected = true;

                        // Count bit errors in DCI
                        for (int b = 0; b < dciSize; b++) {
                            if (decoded[b] != dci[b]) totalBitErrors++;
                        }
                        totalDetectedBits += dciSize;
                    } else {
                        // False alarm: CRC passed on wrong candidate
                        falseAlarm = true;
                    }
                    break;  // Stop after first CRC pass
                }
            }

            if (detected) {
                detections++;
            } else if (falseAlarm) {
                falseAlarms++;
            } else {
                misses++;
            }
        }

        double pDetect = static_cast<double>(detections) / cfg.numTrials;
        double pMiss = static_cast<double>(misses) / cfg.numTrials;
        double pFalseAlarm = static_cast<double>(falseAlarms) / cfg.numTrials;
        double ber = (totalDetectedBits > 0) ?
                     static_cast<double>(totalBitErrors) / totalDetectedBits : 0.0;

        std::cout << std::setw(12) << std::fixed << std::setprecision(1) << snr
                  << std::setw(12) << std::fixed << std::setprecision(4) << pDetect
                  << std::setw(12) << std::fixed << std::setprecision(4) << pMiss
                  << std::setw(15) << std::scientific << std::setprecision(4) << pFalseAlarm
                  << std::setw(15) << std::scientific << std::setprecision(4) << ber
                  << std::endl;
    }

    std::cout << std::endl << "PDCCH simulation complete." << std::endl;
}
