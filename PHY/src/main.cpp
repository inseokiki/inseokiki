#include <iostream>
#include <iomanip>
#include <cmath>
#include "config_parser.h"
#include "utils.h"
#include "modulation.h"
#include "ofdm.h"
#include "channel.h"
#include "ldpc.h"
#include "polar.h"
#include "pbch.h"
#include "pdcch.h"
#include "pdsch.h"
#include "csi_rs.h"
#include "srs.h"

// Pure QAM BER simulation — no OFDM, uses numTrials, block-level BLER
static void runBerSimulation(const L1Config& cfg) {
    int bitsPerSymbol = getBitsPerSymbol(cfg.modulation);
    const int blockSize = 1000;
    int paddedSize = ((blockSize + bitsPerSymbol - 1) / bitsPerSymbol) * bitsPerSymbol;

    std::vector<double> snrRange;
    for (double snr = cfg.snrStart; snr <= cfg.snrEnd + 0.001; snr += cfg.snrStep)
        snrRange.push_back(snr);

    std::cout << "=== BER Simulation (uncoded, no OFDM) ===" << std::endl;
    std::cout << "Modulation   : " << cfg.modulation << std::endl;
    std::cout << "Channel      : " << cfg.channelModel << std::endl;
    std::cout << "Block Size   : " << blockSize << " bits" << std::endl;
    std::cout << "Trials/SNR   : " << cfg.numTrials << std::endl;
    std::cout << std::endl;

    std::cout << std::setw(12) << "SNR (dB)"
              << std::setw(15) << "BER"
              << std::setw(15) << "BLER" << std::endl;
    std::cout << std::string(42, '-') << std::endl;

    for (double snr : snrRange) {
        int totalBitErrors = 0;
        int blockErrors    = 0;

        for (int trial = 0; trial < cfg.numTrials; trial++) {
            std::vector<int> infoBits = generateRandomBits(blockSize);
            std::vector<int> txBits   = infoBits;
            txBits.resize(paddedSize, 0);

            ComplexVec modSymbols = qamModulate(txBits, cfg.modulation);

            ComplexVec rxSymbols;
            if (cfg.channelModel == "NONE") {
                rxSymbols = modSymbols;
            } else {
                AWGNChannel channel(snr);
                rxSymbols = channel.addNoise(modSymbols, 0);
            }

            std::vector<int> rxBits = qamDemodulate(rxSymbols, cfg.modulation);

            int bitErrors = 0;
            for (int i = 0; i < blockSize; i++) {
                if (infoBits[i] != rxBits[i]) bitErrors++;
            }
            totalBitErrors += bitErrors;
            if (bitErrors > 0) blockErrors++;
        }

        double ber  = static_cast<double>(totalBitErrors) / (cfg.numTrials * blockSize);
        double bler = static_cast<double>(blockErrors)    /  cfg.numTrials;

        std::cout << std::setw(12) << snr
                  << std::setw(15) << std::scientific << std::setprecision(4) << ber
                  << std::setw(15) << std::fixed      << std::setprecision(4) << bler
                  << std::endl;
    }

    std::cout << std::endl << "Simulation complete." << std::endl;
}

// Legacy simulation mode (original behavior when PHYSICAL_CHANNEL = NONE)
static void runLegacySimulation(const L1Config& cfg) {
    // Modulation parameters
    int bitsPerSymbol = getBitsPerSymbol(cfg.modulation);

    // Create OFDM modulator/demodulator (using normal CP for simplicity)
    OFDM ofdm(cfg.nfft, cfg.cpLengthNormal);

    // Setup coding
    int infoBlockSize = 128;
    LDPCCodec ldpc(infoBlockSize, cfg.codeRate);
    PolarCodec polar(256, infoBlockSize);

    bool useLDPC = (cfg.coding == "LDPC");
    bool usePolar = (cfg.coding == "POLAR");
    bool useCoding = useLDPC || usePolar;

    int codedBits = useLDPC ? ldpc.getCodedSize() :
                    usePolar ? polar.getCodedSize() : infoBlockSize;

    // Pad coded bits to multiple of bitsPerSymbol for modulation
    int paddedBits = codedBits;
    int remainder = paddedBits % bitsPerSymbol;
    if (remainder != 0) {
        paddedBits += (bitsPerSymbol - remainder);
    }

    // Generate SNR range
    std::vector<double> snrRange;
    for (double snr = cfg.snrStart; snr <= cfg.snrEnd; snr += cfg.snrStep) {
        snrRange.push_back(snr);
    }

    std::cout << "=== Legacy Simulation (OFDM-based) ===" << std::endl;
    std::cout << "Modulation   : " << cfg.modulation << std::endl;
    std::cout << "Channel      : " << cfg.channelModel << std::endl;
    std::cout << "Block Size   : " << infoBlockSize << " bits" << std::endl;
    std::cout << std::endl;

    std::cout << std::setw(10) << "SNR (dB)" << std::setw(15) << "BER"
              << std::setw(15) << "BLER" << std::endl;
    std::cout << std::string(40, '-') << std::endl;

    for (double snr : snrRange) {
        AWGNChannel channel(snr);
        int totalInfoBits = 0;
        int totalBitErrors = 0;
        int totalBlocks = 0;
        int blockErrors = 0;

        for (int sym = 0; sym < cfg.numOfdmSymbols; sym++) {
            // Generate random information bits
            std::vector<int> infoBits = generateRandomBits(infoBlockSize);

            // Encode
            std::vector<int> txCodedBits;
            if (useLDPC) {
                txCodedBits = ldpc.encode(infoBits);
            } else if (usePolar) {
                txCodedBits = polar.encode(infoBits);
            } else {
                txCodedBits = infoBits;
            }

            // Pad to multiple of bitsPerSymbol
            std::vector<int> txBits = txCodedBits;
            if (paddedBits > codedBits) {
                txBits.resize(paddedBits, 0);
            }

            // QAM modulation
            ComplexVec modSymbols = qamModulate(txBits, cfg.modulation);

            // Pad to FFT size
            ComplexVec freqSymbols(cfg.nfft, Complex(0, 0));
            for (size_t i = 0; i < modSymbols.size(); i++) {
                freqSymbols[i] = modSymbols[i];
            }

            // OFDM modulation
            ComplexVec txSignal = ofdm.modulate(freqSymbols);

            // Channel (pass nfft for correct per-subcarrier SNR scaling)
            ComplexVec rxSignal = channel.addNoise(txSignal, cfg.nfft);

            // OFDM demodulation
            ComplexVec rxFreqSymbols = ofdm.demodulate(rxSignal);

            // Extract data symbols
            ComplexVec rxModSymbols(modSymbols.size());
            for (size_t i = 0; i < modSymbols.size(); i++) {
                rxModSymbols[i] = rxFreqSymbols[i];
            }

            // Decode
            std::vector<int> rxInfoBits;
            if (useCoding) {
                // Soft demodulation: compute LLR
                double noiseVar = 1.0 / std::pow(10.0, snr / 10.0);
                std::vector<double> allLlr = qamDemapLLR(rxModSymbols, cfg.modulation, noiseVar);

                // Truncate LLR to coded size (remove padding)
                std::vector<double> llr(allLlr.begin(), allLlr.begin() + codedBits);

                if (useLDPC) {
                    rxInfoBits = ldpc.decode(llr, 20);
                } else {
                    rxInfoBits = polar.decode(llr);
                }
            } else {
                // Hard demodulation
                std::vector<int> allBits = qamDemodulate(rxModSymbols, cfg.modulation);
                rxInfoBits.assign(allBits.begin(), allBits.begin() + infoBlockSize);
            }

            // Count errors
            int bitErrors = 0;
            for (int i = 0; i < infoBlockSize; i++) {
                if (infoBits[i] != rxInfoBits[i]) bitErrors++;
            }
            totalBitErrors += bitErrors;
            totalInfoBits += infoBlockSize;
            totalBlocks++;
            if (bitErrors > 0) blockErrors++;
        }

        double ber = static_cast<double>(totalBitErrors) / totalInfoBits;
        double bler = static_cast<double>(blockErrors) / totalBlocks;

        std::cout << std::setw(10) << snr
                  << std::setw(15) << std::scientific << std::setprecision(4) << ber
                  << std::setw(15) << std::fixed << std::setprecision(4) << bler
                  << std::endl;
    }

    std::cout << std::endl << "Simulation complete." << std::endl;
}

int main(int argc, char* argv[]) {
    // Load configuration
    std::string configFile = "config/sim_config.txt";
    if (argc > 1) {
        configFile = argv[1];
    }

    ConfigParser parser;
    parser.loadConfig(configFile);
    L1Config cfg = parser.getConfig();

    std::cout << "=== 5G PHY Link Level Simulator ===" << std::endl;
    parser.printConfig();
    std::cout << std::endl;

    // Mode dispatch based on physical channel
    if (cfg.physicalChannel == "PBCH") {
        runPbchSimulation(cfg);
    } else if (cfg.physicalChannel == "PDCCH") {
        runPdcchSimulation(cfg);
    } else if (cfg.physicalChannel == "PDSCH") {
        if (cfg.useDmrs) {
            runPdschDmrsSimulation(cfg);
        } else {
            runPdschSimulation(cfg);
        }
    } else if (cfg.physicalChannel == "CSIRS") {
        runCsirsSimulation(cfg);
    } else if (cfg.physicalChannel == "SRS") {
        runSrsSimulation(cfg);
    } else if (cfg.physicalChannel == "BER") {
        runBerSimulation(cfg);
    } else {
        // NONE or unrecognized: run legacy simulation
        runLegacySimulation(cfg);
    }

    return 0;
}
