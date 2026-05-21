#include "pdsch.h"
#include "crc.h"
#include "mcs_table.h"
#include "ldpc.h"
#include "modulation.h"
#include "channel.h"
#include "dmrs.h"
#include "channel_estimation.h"
#include "utils.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

void runPdschSimulation(const L1Config& cfg) {
    // Get MCS parameters
    MCSTableType mcsTable = (cfg.mcsTableType == "TABLE2") ?
                             MCSTableType::TABLE2 : MCSTableType::TABLE1;
    MCSEntry mcs = getMCSEntry(cfg.mcsIndex, mcsTable);
    double codeRate = getCodeRate(mcs);
    std::string mod = mcs.modulation;
    int bitsPerSymbol = mcs.modulationOrder;
    int crcBits = 24;  // CRC-24A

    // Determine TB size
    int tbSize = cfg.tbSize;
    if (tbSize <= 0) {
        // Auto TB size: pick a reasonable size for simulation
        // Use ~1000 info bits as default
        tbSize = 1000;
    }
    // Cap at max single CB size (8448 - 24 CRC bits for LDPC)
    if (tbSize > 8424) tbSize = 8424;

    int A = tbSize;  // TB size (info bits before CRC)
    int K = A + crcBits;  // Info bits + CRC

    // BG selection: BG1 if A > 3824 or R > 0.67, else BG2
    bool useBG1 = (A > 3824) || (codeRate > 0.67);

    // Coded block size: N = K / R
    int codedSize = static_cast<int>(std::ceil(K / codeRate));
    // Make sure codedSize is reasonable
    if (codedSize < K + 1) codedSize = K + 1;

    // Pad coded bits to multiple of bitsPerSymbol
    int paddedCodedSize = codedSize;
    int rem = paddedCodedSize % bitsPerSymbol;
    if (rem != 0) paddedCodedSize += (bitsPerSymbol - rem);

    std::cout << "=== PDSCH Simulation ===" << std::endl;
    std::cout << "MCS Index: " << cfg.mcsIndex << " (Table " << cfg.mcsTableType << ")" << std::endl;
    std::cout << "Modulation: " << mod << " (Qm=" << bitsPerSymbol << ")" << std::endl;
    std::cout << "Code Rate: " << std::fixed << std::setprecision(4) << codeRate
              << " (R x 1024 = " << mcs.targetCodeRate << ")" << std::endl;
    std::cout << "TB Size: " << A << " bits" << std::endl;
    std::cout << "CRC: CRC-24A (" << crcBits << " bits)" << std::endl;
    std::cout << "LDPC Base Graph: " << (useBG1 ? "BG1" : "BG2") << std::endl;
    std::cout << "Coded Size: " << codedSize << " bits" << std::endl;
    std::cout << "Spectral Efficiency: " << std::fixed << std::setprecision(3)
              << mcs.spectralEfficiency << " bits/RE" << std::endl;
    std::cout << "Trials per SNR: " << cfg.numTrials << std::endl;
    std::cout << std::endl;

    // Create LDPC codec
    LDPCCodec ldpc(K, codeRate);
    int actualCodedSize = ldpc.getCodedSize();

    // Adjust padded size based on actual LDPC output
    paddedCodedSize = actualCodedSize;
    rem = paddedCodedSize % bitsPerSymbol;
    if (rem != 0) paddedCodedSize += (bitsPerSymbol - rem);

    // Generate SNR range
    std::vector<double> snrRange;
    for (double snr = cfg.snrStart; snr <= cfg.snrEnd + 0.001; snr += cfg.snrStep) {
        snrRange.push_back(snr);
    }

    std::cout << std::setw(12) << "SNR (dB)"
              << std::setw(15) << "BER"
              << std::setw(15) << "BLER" << std::endl;
    std::cout << std::string(42, '-') << std::endl;

    for (double snr : snrRange) {
        int totalBitErrors = 0;
        int totalInfoBits = 0;
        int blockErrors = 0;

        for (int trial = 0; trial < cfg.numTrials; trial++) {
            // TX: Generate random TB
            std::vector<int> tbBits = generateRandomBits(A);

            // Attach CRC-24A
            std::vector<int> tbWithCRC = attachCRC(tbBits, CRCType::CRC24A);

            // LDPC encode
            std::vector<int> codedBits = ldpc.encode(tbWithCRC);

            // Pad to multiple of bitsPerSymbol
            std::vector<int> txBits = codedBits;
            if (paddedCodedSize > actualCodedSize) {
                txBits.resize(paddedCodedSize, 0);
            }

            // QAM modulation
            ComplexVec modSymbols = qamModulate(txBits, mod);

            // AWGN channel (non-OFDM mode)
            AWGNChannel channel(snr);
            ComplexVec rxSymbols = channel.addNoise(modSymbols, 0);

            // Soft demodulation
            double noiseVar = 1.0 / std::pow(10.0, snr / 10.0);
            std::vector<double> allLLR = qamDemapLLR(rxSymbols, mod, noiseVar);

            // Truncate LLR to actual coded size
            std::vector<double> llr(allLLR.begin(), allLLR.begin() + actualCodedSize);

            // LDPC decode
            std::vector<int> decoded = ldpc.decode(llr, 25);

            // Check CRC
            bool crcPass = checkCRC(decoded, CRCType::CRC24A);

            // Count bit errors (on info bits only, excluding CRC)
            int bitErrors = 0;
            int minLen = std::min(A, (int)decoded.size() - crcBits);
            if (minLen < 0) minLen = 0;
            for (int i = 0; i < minLen; i++) {
                if (tbBits[i] != decoded[i]) bitErrors++;
            }

            totalBitErrors += bitErrors;
            totalInfoBits += A;
            if (!crcPass || bitErrors > 0) blockErrors++;
        }

        double ber = (totalInfoBits > 0) ?
                     static_cast<double>(totalBitErrors) / totalInfoBits : 0.0;
        double bler = static_cast<double>(blockErrors) / cfg.numTrials;

        std::cout << std::setw(12) << std::fixed << std::setprecision(1) << snr
                  << std::setw(15) << std::scientific << std::setprecision(4) << ber
                  << std::setw(15) << std::fixed << std::setprecision(4) << bler
                  << std::endl;
    }

    std::cout << std::endl << "PDSCH simulation complete." << std::endl;
}

void runPdschDmrsSimulation(const L1Config& cfg) {
    int numRB = cfg.numRB;
    int activeSubcarriers = numRB * 12;

    MCSTableType mcsTable = (cfg.mcsTableType == "TABLE2") ?
                             MCSTableType::TABLE2 : MCSTableType::TABLE1;
    MCSEntry mcs = getMCSEntry(cfg.mcsIndex, mcsTable);
    double codeRate = getCodeRate(mcs);
    std::string mod = mcs.modulation;
    int bitsPerSymbol = mcs.modulationOrder;
    int crcBits = 24;

    // DMRS Type 1: pilots at even subcarriers per RB, data at odd
    const uint32_t cInit = 0x12345678u;
    std::vector<int> pilotPos = dmrsPilotIndices(numRB);   // 6*numRB positions
    std::vector<int> dataPos  = dmrsDataIndices(numRB);    // 6*numRB positions
    int numPilots  = static_cast<int>(pilotPos.size());
    int numDataREs = static_cast<int>(dataPos.size());

    ComplexVec dmrsSym = dmrsSequence(cInit, numPilots);

    // TB sizing: fit data bits into numDataREs subcarriers after coding
    int maxDataBits = numDataREs * bitsPerSymbol;
    int tbSize = static_cast<int>(maxDataBits * codeRate);
    tbSize = std::max(1, std::min(tbSize, 8424));

    int K = tbSize + crcBits;
    LDPCCodec ldpc(K, codeRate);
    int actualCodedSize = ldpc.getCodedSize();
    int paddedCodedSize = actualCodedSize;
    int rem = paddedCodedSize % bitsPerSymbol;
    if (rem != 0) paddedCodedSize += (bitsPerSymbol - rem);

    std::cout << "=== PDSCH + DMRS Channel Estimation ===" << std::endl;
    std::cout << "MCS Index    : " << cfg.mcsIndex << " (Table " << cfg.mcsTableType << ")" << std::endl;
    std::cout << "Modulation   : " << mod << " (Qm=" << bitsPerSymbol << ")" << std::endl;
    std::cout << "Code Rate    : " << std::fixed << std::setprecision(4) << codeRate << std::endl;
    std::cout << "Num RB       : " << numRB << std::endl;
    std::cout << "Active SC    : " << activeSubcarriers << std::endl;
    std::cout << "Pilot REs    : " << numPilots << " (DMRS Type 1, 50% overhead)" << std::endl;
    std::cout << "Data REs     : " << numDataREs << std::endl;
    std::cout << "TB Size      : " << tbSize << " bits" << std::endl;
    bool useMmse     = (cfg.equalizer == "MMSE");
    bool useRayleigh = (cfg.channelModel != "AWGN");
    std::cout << "Channel      : " << (useRayleigh ? "Flat Rayleigh fading (i.i.d. per trial)"
                                                   : "AWGN (H=1)") << std::endl;
    std::cout << "Equalizer    : " << (useMmse ? "MMSE" : "ZF")
              << " with LS channel estimation" << std::endl;
    std::cout << "Trials/SNR   : " << cfg.numTrials << std::endl;
    std::cout << std::endl;

    std::vector<double> snrRange;
    for (double snr = cfg.snrStart; snr <= cfg.snrEnd + 1e-6; snr += cfg.snrStep) {
        snrRange.push_back(snr);
    }

    std::cout << std::setw(12) << "SNR (dB)"
              << std::setw(15) << "BER"
              << std::setw(15) << "BLER" << std::endl;
    std::cout << std::string(42, '-') << std::endl;

    for (double snr : snrRange) {
        FlatFadingChannel rayleighCh(snr);
        AWGNChannel       awgnCh(snr);
        int totalBitErrors = 0, totalInfoBits = 0, blockErrors = 0;
        double snrLinear = std::pow(10.0, snr / 10.0);

        for (int trial = 0; trial < cfg.numTrials; trial++) {
            // --- TX ---
            std::vector<int> tbBits = generateRandomBits(tbSize);
            std::vector<int> tbWithCRC = attachCRC(tbBits, CRCType::CRC24A);
            std::vector<int> codedBits = ldpc.encode(tbWithCRC);

            std::vector<int> txBits = codedBits;
            txBits.resize(paddedCodedSize, 0);
            ComplexVec dataSyms = qamModulate(txBits, mod);

            // Build resource grid: DMRS at pilot positions, data at data positions
            ComplexVec txGrid(activeSubcarriers, Complex(0.0, 0.0));
            for (int p = 0; p < numPilots; p++) {
                txGrid[pilotPos[p]] = dmrsSym[p];
            }
            for (int d = 0; d < numDataREs && d < static_cast<int>(dataSyms.size()); d++) {
                txGrid[dataPos[d]] = dataSyms[d];
            }

            // --- Channel ---
            ComplexVec rxGrid = useRayleigh ? rayleighCh.apply(txGrid)
                                            : awgnCh.addNoise(txGrid);

            // --- RX: LS channel estimation from pilots ---
            ComplexVec rxPilots(numPilots);
            for (int p = 0; p < numPilots; p++) {
                rxPilots[p] = rxGrid[pilotPos[p]];
            }
            ComplexVec hAtPilots = lsEstimate(rxPilots, dmrsSym);

            // Interpolate to all active subcarriers
            ComplexVec hFull = interpolateChannel(hAtPilots, pilotPos, activeSubcarriers);

            // Extract and equalize data subcarriers
            ComplexVec rxData(numDataREs), hAtData(numDataREs);
            for (int d = 0; d < numDataREs; d++) {
                rxData[d]  = rxGrid[dataPos[d]];
                hAtData[d] = hFull[dataPos[d]];
            }
            double N0 = 1.0 / snrLinear;
            std::vector<double> allLLR;

            if (useMmse) {
                // MMSE: biased output + per-subcarrier noise/bias in LLR
                ComplexVec eqData = mmseEqualize(rxData, hAtData, N0);
                allLLR = qamDemapLLR_mmse(eqData, mod, hAtData, N0);
            } else {
                // ZF: unbiased output, mean-channel noise variance
                ComplexVec eqData = zfEqualize(rxData, hAtData);
                double meanHPow = 0.0;
                for (const auto& h : hAtData) meanHPow += std::norm(h);
                meanHPow /= numDataREs;
                double effectiveNoiseVar = (meanHPow > 1e-10) ? N0 / meanHPow : N0;
                allLLR = qamDemapLLR(eqData, mod, effectiveNoiseVar);
            }
            std::vector<double> llr(allLLR.begin(),
                                    allLLR.begin() + std::min(actualCodedSize,
                                                               static_cast<int>(allLLR.size())));

            // Decode
            std::vector<int> decoded = ldpc.decode(llr, 25);
            bool crcPass = checkCRC(decoded, CRCType::CRC24A);

            int bitErrors = 0;
            int minLen = std::min(tbSize, static_cast<int>(decoded.size()) - crcBits);
            if (minLen > 0) {
                for (int i = 0; i < minLen; i++) {
                    if (tbBits[i] != decoded[i]) bitErrors++;
                }
            }
            totalBitErrors += bitErrors;
            totalInfoBits  += tbSize;
            if (!crcPass || bitErrors > 0) blockErrors++;
        }

        double ber  = (totalInfoBits > 0) ?
                      static_cast<double>(totalBitErrors) / totalInfoBits : 0.0;
        double bler = static_cast<double>(blockErrors) / cfg.numTrials;

        std::cout << std::setw(12) << std::fixed << std::setprecision(1) << snr
                  << std::setw(15) << std::scientific << std::setprecision(4) << ber
                  << std::setw(15) << std::fixed << std::setprecision(4) << bler
                  << std::endl;
    }

    std::cout << std::endl << "PDSCH+DMRS simulation complete." << std::endl;
}
