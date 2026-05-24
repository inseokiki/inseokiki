#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <string>
#include <map>
#include <cstdint>

struct L1Config {
    // Bandwidth and SCS
    int bandwidthMHz;
    int scsKHz;

    // Resource allocation
    int numRB;
    int nfft;
    int cpLengthFirst;   // CP for symbol 0, 7 (longer)
    int cpLengthNormal;  // CP for symbol 1-6, 8-13

    // Modulation and coding
    std::string modulation;
    std::string coding;
    double codeRate;

    // Simulation parameters
    int numOfdmSymbols;
    double snrStart;
    double snrEnd;
    double snrStep;

    // Channel
    std::string channelModel;

    // Physical channel parameters
    std::string physicalChannel;   // NONE, PBCH, PDCCH, PDSCH
    int dciSize;                   // DCI payload size (PDCCH)
    uint16_t rnti;                 // RNTI for PDCCH
    std::string searchSpace;       // CSS or USS
    int pdcchAL;                   // TX aggregation level (PDCCH)
    int mcsIndex;                  // MCS index (PDSCH)
    std::string mcsTableType;      // TABLE1 or TABLE2
    int tbSize;                    // Transport block size (0=auto)
    int numTrials;                 // Number of simulation trials
    int numBits;                   // Total bits per SNR point (ber_sim; 0 = use numTrials)

    // DMRS / channel estimation
    bool useDmrs;                  // Enable DMRS + channel estimation mode
    std::string equalizer;         // "ZF" or "MMSE"

    // CSI-RS parameters (TS 38.211 §7.4.1.5)
    int csirsRow;                  // Mapping row 1-4 (density & port count)
    int csirsScramID;              // N_ID^CSI-RS (0..1023)
    int csirsSymbol;               // OFDM symbol index l_0 (0..13)
    int csirsK0;                   // Subcarrier offset k_0 within RB (0..11)

    // SRS parameters (TS 38.211 §6.4.1.4)
    int srsBandwidthRB;            // mSRS_b: SRS bandwidth in RBs
    int srsCombSize;               // K_TC: comb size 2 or 4
    int srsCombOffset;             // k_bar_TC: comb offset 0..K_TC-1
    int srsCyclicShift;            // n_CS: cyclic shift
    int srsSeqGroupU;              // Sequence group u (0..29)
    int srsSeqNumV;                // Sequence number v (0 or 1)

    // IQ Dump
    bool iqDumpEnable;
    double iqDumpSnr;
    std::string iqDumpFile;
    int iqDumpTrials;   // number of trials to dump (0 = all)

    // Derived parameters (calculated after loading)
    int numSubcarriers;
    double samplingRate;
};

class ConfigParser {
public:
    ConfigParser();

    bool loadConfig(const std::string& filename);
    L1Config getConfig() const { return config_; }

    void printConfig() const;

private:
    L1Config config_;
    std::map<std::string, std::string> params_;

    void parseFile(const std::string& filename);
    void applyDefaults();
    void calculateDerived();

    int getInt(const std::string& key, int defaultVal);
    double getDouble(const std::string& key, double defaultVal);
    std::string getString(const std::string& key, const std::string& defaultVal);

    int getNRBFromBwScs(int bw, int scs);
    int getFFTSize(int nrb);
    int getCPLength(int nfft, int scs);
};

#endif
