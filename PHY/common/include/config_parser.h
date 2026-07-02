#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdint.h>

#define CFG_STR_MAX  64
#define CFG_KV_MAX   128

typedef struct {
    int    bandwidthMHz;
    int    scsKHz;
    int    numRB;
    int    nfft;
    int    cpLengthFirst;
    int    cpLengthNormal;
    char   modulation[CFG_STR_MAX];
    char   coding[CFG_STR_MAX];
    double codeRate;
    int    numOfdmSymbols;
    double snrStart;
    double snrEnd;
    double snrStep;
    char   channelModel[CFG_STR_MAX];
    char   physicalChannel[CFG_STR_MAX];
    int    dciSize;
    uint16_t rnti;
    char   searchSpace[CFG_STR_MAX];
    int    pdcchAL;
    int    mcsIndex;
    char   mcsTableType[CFG_STR_MAX];
    int    tbSize;
    int    numTrials;
    int    numBits;
    int    useDmrs;
    char   equalizer[CFG_STR_MAX];
    int    csirsRow;
    int    csirsScramID;
    int    csirsSymbol;
    int    csirsK0;
    int    srsBandwidthRB;
    int    srsCombSize;
    int    srsCombOffset;
    int    srsCyclicShift;
    int    srsSeqGroupU;
    int    srsSeqNumV;
    int    iqDumpEnable;
    double iqDumpSnr;
    char   iqDumpFile[CFG_STR_MAX];
    int    iqDumpTrials;
    int    numSubcarriers;
    double samplingRate;

    /* TDL channel + HARQ IR */
    char   tdlModel[CFG_STR_MAX];  /* TDL_A / TDL_C / TDL_D             */
    double tdlDsRmsNs;             /* RMS delay spread [ns]              */
    double dopplerHz;              /* max Doppler frequency [Hz]         */
    int    harqMaxRounds;          /* max HARQ retransmissions (1-4)     */

    /* MIMO */
    int    numRxAnt;               /* number of receive antennas (1-4)   */

    /* CSV output */
    int    outputCsv;              /* 1 = write CSV file                 */
    char   outputFile[CFG_STR_MAX];/* CSV output filename                */

    /* Monte Carlo stopping */
    int    minBlockErrors;         /* stop SNR point when this many block errors accumulated */
} L1Config;

typedef struct {
    char key[CFG_STR_MAX];
    char val[CFG_STR_MAX];
} KVEntry;

typedef struct {
    L1Config cfg;
    KVEntry  kv[CFG_KV_MAX];
    int      nkv;
} ConfigParser;

void config_parser_init(ConfigParser *p);
int  config_parser_load(ConfigParser *p, const char *filename);
void config_parser_print(const ConfigParser *p);
L1Config config_parser_get(const ConfigParser *p);

#endif
