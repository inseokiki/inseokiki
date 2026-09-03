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
    double ulpcPlDb;            /* UE가 측정한 DL 경로손실 (시계열용, 정상상태 평균) [dB] */
    int    ulpcNumSf;           /* 시계열 시뮬레이션 서브프레임 수       */
    double ulpcPlVarStdDb;      /* 시변 PL(그림자페이딩/이동성) 정상상태 표준편차 [dB], 0=고정 PL */
    double ulpcPlVarCorr;       /* 시변 PL의 SF간 상관계수 (Gauss-Markov) [0,1) */
    /* ── 4x4 MIMO Tx 공간상관 (Kronecker, R_pol (x) R_ant) ── */
    double spatialCorrTx;       /* 동일편파 내 안테나 간 상관계수 rho [0,1) (R_ant, 4/8포트는
                                    1D 전체, 32포트는 수평(N1) 축) */
    double spatialCorrTxVert;   /* CL_32PORT 전용: 수직(N2) 축 상관계수 rho_v [0,1) (R_vert) */
    double spatialCorrXpol;     /* 편파 간 상관계수 rho_xpol [0,1) — 유한 XPD 누설 (R_pol) */
    /* ── EIGEN_16PORT 채널추정 방식 (imperfect CSI 연구용) ── */
    char   eigen16ChanEst[CFG_STR_MAX];  /* NONE(genie)/LS/MMSE/DFT */
    char   eigen16PrecoderGran[CFG_STR_MAX]; /* WIDEBAND/SUBBAND 프리코더 방향 그래뉼래러티 */
    /* ── CL_4/8/32PORT 코드북 RI+PMI 설계용 채널추정 방식 (imperfect CSI) ── */
    char   chanEstMethod[CFG_STR_MAX];   /* NONE(genie)/LS/MMSE/DFT — TDL 변형에만 적용 */
    /* ── OLLA (Outer Loop Link Adaptation) ── */
    int    ollaEnable;
    double ollaBlerTarget;   /* 목표 BLER (0,1), 기본 0.1 */
    double ollaStepDownDb;   /* NACK 시 오프셋 감소량 [dB], 기본 0.5 */
    double ollaSnrGapDb;     /* Shannon 대비 구현 마진 [dB], 기본 3.0 */
    /* ── 빔 관리(Beam Management) P1 절차 ── */
    int    beamMgmtNumRep;  /* SSB/CSI-RS 빔당 RSRP 반복 관측 횟수, 기본 4 */
    int    beamMgmtRxSweep; /* 1이면 P1->P3(UE Rx 빔 정제)->P2(gNB Tx 빔
                                재정제) 절차 사용, 기본 0(P1만, 기존 동작) */
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
