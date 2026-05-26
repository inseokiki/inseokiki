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
