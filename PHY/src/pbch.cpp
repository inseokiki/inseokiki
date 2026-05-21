#include "pbch.h"
#include "crc.h"
#include "polar.h"
#include "polar_rate_match.h"
#include "modulation.h"
#include "channel.h"
#include "utils.h"
#include <iostream>
#include <iomanip>
#include <cmath>

void runPbchSimulation(const L1Config& cfg) {
    // PBCH fixed parameters
    const int payloadBits = 32;       // MIB payload
    const int crcBits = 24;           // CRC-24C
    const int K = payloadBits + crcBits;  // 56 info bits to Polar encoder
    const int N = 512;                // Polar code block length
    const int E = 864;                // Rate-matched output length
    const std::string mod = "QPSK";
    const int bitsPerSymbol = 2;

    std::cout << "=== PBCH Simulation ===" << std::endl;
    std::cout << "Payload: " << payloadBits << " bits" << std::endl;
    std::cout << "CRC: CRC-24C (" << crcBits << " bits)" << std::endl;
    std::cout << "Polar code: (" << N << ", " << K << ")" << std::endl;
    std::cout << "Rate matching: " << N << " -> " << E << " bits" << std::endl;
    std::cout << "Modulation: " << mod << " (" << E / bitsPerSymbol << " symbols)" << std::endl;
    std::cout << "Trials per SNR: " << cfg.numTrials << std::endl;
    std::cout << std::endl;

    PolarCodec polar(N, K);

    // Generate SNR range
    std::vector<double> snrRange;
    for (double snr = cfg.snrStart; snr <= cfg.snrEnd + 0.001; snr += cfg.snrStep) {
        snrRange.push_back(snr);
    }

    std::cout << std::setw(12) << "SNR (dB)" << std::setw(15) << "BLER" << std::endl;
    std::cout << std::string(27, '-') << std::endl;

    for (double snr : snrRange) {
        int blockErrors = 0;

        for (int trial = 0; trial < cfg.numTrials; trial++) {
            // TX: Generate random MIB payload
            std::vector<int> payload = generateRandomBits(payloadBits);

            // Attach CRC-24C
            std::vector<int> payloadWithCRC = attachCRC(payload, CRCType::CRC24C);

            // Polar encode
            std::vector<int> codedBits = polar.encode(payloadWithCRC);

            // Rate match: 512 -> 864
            std::vector<int> rateMatched = polarRateMatch(codedBits, E, K);

            // QPSK modulation
            ComplexVec modSymbols = qamModulate(rateMatched, mod);

            // AWGN channel (non-OFDM mode, nfft=0)
            AWGNChannel channel(snr);
            ComplexVec rxSymbols = channel.addNoise(modSymbols, 0);

            // QPSK soft demodulation
            double noiseVar = 1.0 / std::pow(10.0, snr / 10.0);
            std::vector<double> llr = qamDemapLLR(rxSymbols, mod, noiseVar);

            // Rate dematch: 864 -> 512
            std::vector<double> dematchedLLR = polarRateDematch(llr, N, E, K);

            // Polar decode
            std::vector<int> decoded = polar.decode(dematchedLLR);

            // Check CRC
            if (!checkCRC(decoded, CRCType::CRC24C)) {
                blockErrors++;
            }
        }

        double bler = static_cast<double>(blockErrors) / cfg.numTrials;

        std::cout << std::setw(12) << std::fixed << std::setprecision(1) << snr
                  << std::setw(15) << std::scientific << std::setprecision(4) << bler
                  << std::endl;
    }

    std::cout << std::endl << "PBCH simulation complete." << std::endl;
}
