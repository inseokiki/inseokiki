/* ================================================================
 *  config_parser.h
 *  Config file parser + MCS-based automatic parameter derivation
 *
 *  Author : Inseok Kang
 * ================================================================ */
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
    char   mimoMode[CFG_STR_MAX];
    int    harqEnable;
    int    harqMaxRetx;
    char   harqRvSeq[CFG_STR_MAX];
    double tdlDelaySpreadNs;
    int    transformPrecoding;
    int    puschDfeEnable;
    int    puschTurboEnable;
    int    puschTurboIters;
    int    pucchFormat;
    int    pucchUciBits;
    int    pucchNumSymbols;
    int    pucchNumPrb;
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
    char   prachFormat[CFG_STR_MAX];
    int    prachRootSeqIndex;
    int    prachNumCs;
    int    prachMaxDelaySamples;
    /* ── UL Closed-Loop Power Control (TS 38.213 §7.2.1) ── */
    double ulpcP0Dbm;           /* 목표 수신 파워/RB at gNB [dBm]        */
    double ulpcPcmaxDbm;        /* UE 최대 송신 파워 [dBm]               */
    double ulpcAlpha;           /* 경로손실 부분 보상 계수 α (0~1)       */
    double ulpcNfDb;            /* gNB 잡음지수 NF [dB]                  */
    double ulpcSinrTargetDb;    /* 내부루프 SINR 목표 [dB]               */
    double ulpcPlDb;            /* UE가 측정한 DL 경로손실 (시계열용) [dB] */
    int    ulpcNumSf;           /* 시계열 시뮬레이션 서브프레임 수       */
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
