#include "config_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

ConfigParser::ConfigParser() {
    applyDefaults();
}

void ConfigParser::applyDefaults() {
    config_.bandwidthMHz = 20;
    config_.scsKHz = 30;
    config_.numRB = 0;
    config_.nfft = 0;
    config_.cpLengthFirst = 0;
    config_.cpLengthNormal = 0;
    config_.modulation = "QPSK";
    config_.coding = "LDPC";
    config_.codeRate = 0.5;
    config_.numOfdmSymbols = 50;
    config_.snrStart = -6;
    config_.snrEnd = 10;
    config_.snrStep = 2;
    config_.channelModel = "AWGN";

    // Physical channel defaults
    config_.physicalChannel = "NONE";
    config_.dciSize = 39;
    config_.rnti = 0x1234;
    config_.searchSpace = "CSS";
    config_.pdcchAL = 4;
    config_.mcsIndex = 10;
    config_.mcsTableType = "TABLE1";
    config_.tbSize = 0;
    config_.numTrials = 1000;
    config_.numBits   = 0;
    config_.useDmrs = false;
    config_.equalizer = "ZF";

    // CSI-RS defaults
    config_.csirsRow     = 2;
    config_.csirsScramID = 0;
    config_.csirsSymbol  = 4;
    config_.csirsK0      = 0;

    // IQ Dump defaults
    config_.iqDumpEnable = false;
    config_.iqDumpSnr    = 0.0;
    config_.iqDumpFile   = "iq_dump.txt";
    config_.iqDumpTrials = 100;

    // SRS defaults
    config_.srsBandwidthRB = 16;
    config_.srsCombSize    = 2;
    config_.srsCombOffset  = 0;
    config_.srsCyclicShift = 0;
    config_.srsSeqGroupU   = 0;
    config_.srsSeqNumV     = 0;
}

bool ConfigParser::loadConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Warning: Cannot open " << filename << ", using defaults." << std::endl;
        calculateDerived();
        return false;
    }

    parseFile(filename);

    // Apply parsed values
    config_.bandwidthMHz = getInt("BANDWIDTH_MHZ", 20);
    config_.scsKHz = getInt("SCS_KHZ", 30);
    config_.numRB = getInt("NUM_RB", 0);
    config_.nfft = getInt("NFFT", 0);
    config_.cpLengthFirst = getInt("CP_LENGTH_FIRST", 0);
    config_.cpLengthNormal = getInt("CP_LENGTH_NORMAL", 0);
    config_.modulation = getString("MODULATION", "QPSK");
    config_.coding = getString("CODING", "LDPC");
    config_.codeRate = getDouble("CODE_RATE", 0.5);
    config_.numOfdmSymbols = getInt("NUM_OFDM_SYMBOLS", 50);
    config_.snrStart = getDouble("SNR_START", -6);
    config_.snrEnd = getDouble("SNR_END", 10);
    config_.snrStep = getDouble("SNR_STEP", 2);
    config_.channelModel = getString("CHANNEL_MODEL", "AWGN");

    // Physical channel parameters
    config_.physicalChannel = getString("PHYSICAL_CHANNEL", "NONE");
    config_.dciSize = getInt("DCI_SIZE", 39);
    config_.rnti = static_cast<uint16_t>(getInt("RNTI", 0x1234));
    config_.searchSpace = getString("SEARCH_SPACE", "CSS");
    config_.pdcchAL = getInt("PDCCH_AL", 4);
    config_.mcsIndex = getInt("MCS_INDEX", 10);
    config_.mcsTableType = getString("MCS_TABLE", "TABLE1");
    config_.tbSize = getInt("TB_SIZE", 0);
    config_.numTrials = getInt("NUM_TRIALS", 1000);
    config_.numBits   = getInt("NUM_BITS", 0);
    config_.useDmrs   = (getInt("USE_DMRS", 0) != 0);
    config_.equalizer = getString("EQUALIZER", "ZF");

    // CSI-RS
    config_.csirsRow     = getInt("CSIRS_ROW",      2);
    config_.csirsScramID = getInt("CSIRS_SCRAM_ID", 0);
    config_.csirsSymbol  = getInt("CSIRS_SYMBOL",   4);
    config_.csirsK0      = getInt("CSIRS_K0",        0);

    // IQ Dump
    config_.iqDumpEnable = (getInt("IQ_DUMP", 0) != 0);
    config_.iqDumpSnr    = getDouble("IQ_DUMP_SNR", 0.0);
    config_.iqDumpFile   = getString("IQ_DUMP_FILE", "iq_dump.txt");
    config_.iqDumpTrials = getInt("IQ_DUMP_TRIALS", 100);

    // SRS
    config_.srsBandwidthRB = getInt("SRS_BW_RB",    16);
    config_.srsCombSize    = getInt("SRS_COMB",       2);
    config_.srsCombOffset  = getInt("SRS_COMB_OFFSET",0);
    config_.srsCyclicShift = getInt("SRS_CYCLIC_SHIFT",0);
    config_.srsSeqGroupU   = getInt("SRS_SEQ_GROUP", 0);
    config_.srsSeqNumV     = getInt("SRS_SEQ_NUM",   0);

    calculateDerived();
    return true;
}

void ConfigParser::parseFile(const std::string& filename) {
    std::ifstream file(filename);
    std::string line;

    while (std::getline(file, line)) {
        // Remove comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        // Remove whitespace
        line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());

        if (line.empty()) continue;

        // Parse key=value
        size_t eqPos = line.find('=');
        if (eqPos != std::string::npos) {
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            params_[key] = value;
        }
    }
}

void ConfigParser::calculateDerived() {
    // Auto-calculate NRB if not specified
    if (config_.numRB == 0) {
        config_.numRB = getNRBFromBwScs(config_.bandwidthMHz, config_.scsKHz);
    }

    // Auto-calculate FFT size if not specified
    if (config_.nfft == 0) {
        config_.nfft = getFFTSize(config_.numRB);
    }

    // Auto-calculate CP lengths if not specified (3GPP TS 38.211)
    // First symbol (0, 7): 160 * Nfft / 2048
    // Normal symbols (1-6, 8-13): 144 * Nfft / 2048
    if (config_.cpLengthFirst == 0) {
        config_.cpLengthFirst = (160 * config_.nfft) / 2048;
    }
    if (config_.cpLengthNormal == 0) {
        config_.cpLengthNormal = (144 * config_.nfft) / 2048;
    }

    config_.numSubcarriers = config_.numRB * 12;
    config_.samplingRate = config_.nfft * config_.scsKHz * 1000.0;
}

int ConfigParser::getInt(const std::string& key, int defaultVal) {
    auto it = params_.find(key);
    if (it != params_.end()) {
        return std::stoi(it->second);
    }
    return defaultVal;
}

double ConfigParser::getDouble(const std::string& key, double defaultVal) {
    auto it = params_.find(key);
    if (it != params_.end()) {
        return std::stod(it->second);
    }
    return defaultVal;
}

std::string ConfigParser::getString(const std::string& key, const std::string& defaultVal) {
    auto it = params_.find(key);
    if (it != params_.end()) {
        return it->second;
    }
    return defaultVal;
}

int ConfigParser::getNRBFromBwScs(int bw, int scs) {
    // NRB table per 3GPP TS 38.101
    if (scs == 15) {
        switch (bw) {
            case 5: return 25;
            case 10: return 52;
            case 15: return 79;
            case 20: return 106;
            case 25: return 133;
            case 30: return 160;
            case 40: return 216;
            case 50: return 270;
            default: return 52;
        }
    } else if (scs == 30) {
        switch (bw) {
            case 5: return 11;
            case 10: return 24;
            case 15: return 38;
            case 20: return 51;
            case 25: return 65;
            case 30: return 78;
            case 40: return 106;
            case 50: return 133;
            case 60: return 162;
            case 80: return 217;
            case 100: return 273;
            default: return 51;
        }
    } else if (scs == 60) {
        switch (bw) {
            case 10: return 11;
            case 15: return 18;
            case 20: return 24;
            case 25: return 31;
            case 30: return 38;
            case 40: return 51;
            case 50: return 65;
            case 60: return 79;
            case 80: return 107;
            case 100: return 135;
            default: return 24;
        }
    } else if (scs == 120) {
        switch (bw) {
            case 50: return 66;
            case 100: return 132;
            case 200: return 264;
            default: return 66;
        }
    }
    return 51;  // Default
}

int ConfigParser::getFFTSize(int nrb) {
    int numSc = nrb * 12;
    int fftSize = 128;
    while (fftSize < numSc) {
        fftSize *= 2;
    }
    return fftSize;
}

int ConfigParser::getCPLength(int nfft, int scs) {
    // 3GPP TS 38.211 Table 5.3.1-1 and 5.3.1-2
    // Normal CP length in samples = 144 * Nfft / 2048 (for symbols 1-6,8-13)
    // First symbol CP = 160 * Nfft / 2048 (symbol 0 and 7)
    // Using normal symbol CP for simplicity

    // Base reference: 2048 FFT at 15 kHz SCS -> 144 samples normal CP
    // Scale proportionally with FFT size

    int cpNormal = (144 * nfft) / 2048;

    // Minimum CP to avoid issues
    if (cpNormal < 8) cpNormal = 8;

    return cpNormal;
}

void ConfigParser::printConfig() const {
    std::cout << "=== L1 Configuration ===" << std::endl;
    std::cout << "Bandwidth    : " << config_.bandwidthMHz << " MHz" << std::endl;
    std::cout << "SCS          : " << config_.scsKHz << " kHz" << std::endl;
    std::cout << "Num RB       : " << config_.numRB << std::endl;
    std::cout << "Subcarriers  : " << config_.numSubcarriers << std::endl;
    std::cout << "NFFT         : " << config_.nfft << std::endl;
    std::cout << "CP (sym 0,7) : " << config_.cpLengthFirst << std::endl;
    std::cout << "CP (normal)  : " << config_.cpLengthNormal << std::endl;
    std::cout << "Sample Rate  : " << config_.samplingRate / 1e6 << " MHz" << std::endl;
    std::cout << "Modulation   : " << config_.modulation << std::endl;
    std::cout << "Coding       : " << config_.coding << std::endl;
    std::cout << "Code Rate    : " << config_.codeRate << std::endl;
    std::cout << "Channel      : " << config_.channelModel << std::endl;
    std::cout << "SNR Range    : " << config_.snrStart << " to " << config_.snrEnd
              << " dB (step " << config_.snrStep << ")" << std::endl;
    if (config_.physicalChannel != "NONE") {
        std::cout << "Phys Channel : " << config_.physicalChannel << std::endl;
        std::cout << "Num Trials   : " << config_.numTrials << std::endl;
        if (config_.physicalChannel == "PDCCH") {
            std::cout << "DCI Size     : " << config_.dciSize << std::endl;
            std::cout << "RNTI         : 0x" << std::hex << config_.rnti << std::dec << std::endl;
            std::cout << "Search Space : " << config_.searchSpace << std::endl;
            std::cout << "PDCCH AL     : " << config_.pdcchAL << std::endl;
        }
        if (config_.physicalChannel == "PDSCH") {
            std::cout << "MCS Index    : " << config_.mcsIndex << std::endl;
            std::cout << "MCS Table    : " << config_.mcsTableType << std::endl;
            std::cout << "TB Size      : " << (config_.tbSize > 0 ? std::to_string(config_.tbSize) : "auto") << std::endl;
        }
    }
    std::cout << "========================" << std::endl;
}
