#include "config_parser.h"
#include "modulation.h"
#include "channel.h"
#include "utils.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <algorithm>

int main(int argc, char* argv[]) {
    std::string configFile = "config/ber_config.txt";
    if (argc > 1) configFile = argv[1];

    ConfigParser parser;
    parser.loadConfig(configFile);
    L1Config cfg = parser.getConfig();

    int bitsPerSymbol = getBitsPerSymbol(cfg.modulation);
    long long numBits    = static_cast<long long>(cfg.numBits);
    long long numSymbols = numBits / bitsPerSymbol;
    numBits = numSymbols * bitsPerSymbol;   // align to symbol boundary

    // Eb/N0 → Es/N0: Es/N0 = Eb/N0 + 10*log10(bits/symbol)
    double ebN0ToEsN0_dB = 10.0 * std::log10(static_cast<double>(bitsPerSymbol));

    std::vector<double> snrRange;
    for (double s = cfg.snrStart; s <= cfg.snrEnd + 0.001; s += cfg.snrStep)
        snrRange.push_back(s);

    std::cout << "=== BER Simulation (uncoded, no OFDM) ===" << std::endl;
    std::cout << "Modulation   : " << cfg.modulation    << std::endl;
    std::cout << "Channel      : " << cfg.channelModel  << std::endl;
    std::cout << "SNR axis     : Eb/N0"                 << std::endl;
    std::cout << "Total bits   : " << numBits << " /SNR" << std::endl;
    std::cout << std::endl;

    std::cout << std::setw(12) << "Eb/N0 (dB)"
              << std::setw(15) << "BER" << std::endl;
    std::cout << std::string(27, '-') << std::endl;

    const int chunkSymbols = 100000;    // internal batch size (no coding meaning)

    for (double ebN0_dB : snrRange) {
        double esN0_dB = ebN0_dB + ebN0ToEsN0_dB;

        AWGNChannel channel(esN0_dB);
        long long totalBitErrors = 0;
        long long remaining = numSymbols;

        while (remaining > 0) {
            int chunk     = static_cast<int>(std::min((long long)chunkSymbols, remaining));
            int chunkBits = chunk * bitsPerSymbol;

            std::vector<int> txBits = generateRandomBits(chunkBits);
            ComplexVec modSyms      = qamModulate(txBits, cfg.modulation);

            ComplexVec rxSyms = (cfg.channelModel == "NONE")
                                ? modSyms
                                : channel.addNoise(modSyms, 0);

            std::vector<int> rxBits = qamDemodulate(rxSyms, cfg.modulation);

            for (int i = 0; i < chunkBits; i++)
                if (txBits[i] != rxBits[i]) totalBitErrors++;

            remaining -= chunk;
        }

        double ber = static_cast<double>(totalBitErrors) / numBits;

        std::cout << std::fixed      << std::setprecision(4) << std::setw(12) << ebN0_dB
                  << std::scientific << std::setprecision(4) << std::setw(15) << ber
                  << std::endl;
    }

    std::cout << std::endl << "Simulation complete." << std::endl;
    return 0;
}
