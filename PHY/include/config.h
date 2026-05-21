#ifndef CONFIG_H
#define CONFIG_H

#include <map>
#include <stdexcept>

// Subcarrier Spacing (kHz)
enum class SCS {
    SCS_15kHz = 15,
    SCS_30kHz = 30,
    SCS_60kHz = 60,
    SCS_120kHz = 120
};

// Bandwidth (MHz)
enum class Bandwidth {
    BW_5MHz = 5,
    BW_10MHz = 10,
    BW_15MHz = 15,
    BW_20MHz = 20,
    BW_25MHz = 25,
    BW_30MHz = 30,
    BW_40MHz = 40,
    BW_50MHz = 50,
    BW_60MHz = 60,
    BW_80MHz = 80,
    BW_100MHz = 100
};

// Number of Resource Blocks (NRB) table per 3GPP TS 38.101
// Key: (Bandwidth, SCS) -> NRB
inline int getNRB(Bandwidth bw, SCS scs) {
    // FR1 NRB table (simplified)
    static const std::map<std::pair<int,int>, int> nrbTable = {
        // SCS 15kHz
        {{5, 15}, 25},   {{10, 15}, 52},  {{15, 15}, 79},  {{20, 15}, 106},
        {{25, 15}, 133}, {{30, 15}, 160}, {{40, 15}, 216}, {{50, 15}, 270},
        // SCS 30kHz
        {{5, 30}, 11},   {{10, 30}, 24},  {{15, 30}, 38},  {{20, 30}, 51},
        {{25, 30}, 65},  {{30, 30}, 78},  {{40, 30}, 106}, {{50, 30}, 133},
        {{60, 30}, 162}, {{80, 30}, 217}, {{100, 30}, 273},
        // SCS 60kHz
        {{10, 60}, 11},  {{15, 60}, 18},  {{20, 60}, 24},  {{25, 60}, 31},
        {{30, 60}, 38},  {{40, 60}, 51},  {{50, 60}, 65},  {{60, 60}, 79},
        {{80, 60}, 107}, {{100, 60}, 135}
    };

    auto key = std::make_pair(static_cast<int>(bw), static_cast<int>(scs));
    auto it = nrbTable.find(key);
    if (it != nrbTable.end()) {
        return it->second;
    }
    throw std::invalid_argument("Invalid BW/SCS combination");
}

// Calculate FFT size (smallest power of 2 >= numSubcarriers)
inline int getFFTSize(int nrb) {
    int numSubcarriers = nrb * 12;  // 12 subcarriers per RB
    int fftSize = 128;
    while (fftSize < numSubcarriers) {
        fftSize *= 2;
    }
    return fftSize;
}

// Calculate CP length (normal CP)
// For normal CP: ~7% of symbol duration
inline int getCPLength(int fftSize, SCS scs) {
    // Simplified: CP length as percentage of FFT size
    // Normal CP is approximately 7% of symbol
    return fftSize * 7 / 100;
}

// Sampling rate (Hz)
inline double getSamplingRate(int fftSize, SCS scs) {
    return static_cast<double>(fftSize) * static_cast<int>(scs) * 1000.0;
}

// Configuration struct
struct SimConfig {
    Bandwidth bandwidth;
    SCS scs;
    int nrb;
    int fftSize;
    int cpLength;
    double samplingRate;
    int numOfdmSymbols;

    SimConfig(Bandwidth bw, SCS subcarrierSpacing, int ofdmSymbols = 100)
        : bandwidth(bw), scs(subcarrierSpacing), numOfdmSymbols(ofdmSymbols) {
        nrb = getNRB(bw, scs);
        fftSize = getFFTSize(nrb);
        cpLength = getCPLength(fftSize, scs);
        samplingRate = getSamplingRate(fftSize, scs);
    }

    int numSubcarriers() const { return nrb * 12; }
};

#endif
