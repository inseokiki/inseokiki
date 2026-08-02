/* ================================================================
 *  config_parser.c
 *  Config file parser + MCS-based automatic parameter derivation
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "config_parser.h"
#include "mcs_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- helper: find value for key in KV table ---- */
static const char *kv_get(const ConfigParser *p, const char *key) {
    for (int i = 0; i < p->nkv; i++)
        if (strcmp(p->kv[i].key, key) == 0) return p->kv[i].val;
    return NULL;
}
static int kv_int(const ConfigParser *p, const char *k, int def) {
    const char *v = kv_get(p, k);
    return v ? atoi(v) : def;
}
static double kv_dbl(const ConfigParser *p, const char *k, double def) {
    const char *v = kv_get(p, k);
    return v ? atof(v) : def;
}
static void kv_str(const ConfigParser *p, const char *k,
                   const char *def, char *out, int out_sz) {
    const char *v = kv_get(p, k);
    strncpy(out, v ? v : def, out_sz - 1);
    out[out_sz - 1] = '\0';
}

/* ---- NRB / FFT / CP lookup (from config.h logic) ---- */
static int nrb_from_bw_scs(int bw, int scs) {
    if (scs == 15) {
        switch (bw) {
            case 5: return 25; case 10: return 52; case 15: return 79;
            case 20: return 106; case 25: return 133; case 30: return 160;
            case 40: return 216; case 50: return 270; default: return 52;
        }
    } else if (scs == 30) {
        switch (bw) {
            case 5: return 11; case 10: return 24; case 15: return 38;
            case 20: return 51; case 25: return 65; case 30: return 78;
            case 40: return 106; case 50: return 133; case 60: return 162;
            case 80: return 217; case 100: return 273; default: return 51;
        }
    } else if (scs == 60) {
        switch (bw) {
            case 10: return 11; case 15: return 18; case 20: return 24;
            case 25: return 31; case 30: return 38; case 40: return 51;
            case 50: return 65; case 60: return 79; case 80: return 107;
            case 100: return 135; default: return 24;
        }
    } else if (scs == 120) {
        switch (bw) {
            case 50: return 66; case 100: return 132; case 200: return 264;
            default: return 66;
        }
    }
    return 51;
}

static int fft_size(int nrb) {
    int sc = nrb * 12, sz = 128;
    while (sz < sc) sz *= 2;
    return sz;
}

/* ---- defaults ---- */
void config_parser_init(ConfigParser *p) {
    memset(p, 0, sizeof(ConfigParser));
    L1Config *c = &p->cfg;
    c->bandwidthMHz  = 20;  c->scsKHz = 30;
    c->numOfdmSymbols= 50;
    c->snrStart      = -6;  c->snrEnd = 10; c->snrStep = 2;
    c->dciSize       = 39;  c->rnti = 0x1234;
    c->pdcchAL       = 4;   c->mcsIndex = 0;
    c->numTrials     = 1000;
    c->csirsRow      = 2;   c->csirsSymbol = 4;
    c->iqDumpSnr     = 0.0; c->iqDumpTrials = 100;
    c->srsBandwidthRB= 16;  c->srsCombSize = 2;
    strncpy(c->coding,         "LDPC",   CFG_STR_MAX-1);
    strncpy(c->channelModel,   "AWGN",   CFG_STR_MAX-1);
    strncpy(c->physicalChannel,"NONE",   CFG_STR_MAX-1);
    strncpy(c->searchSpace,    "CSS",    CFG_STR_MAX-1);
    strncpy(c->mcsTableType,   "TABLE1", CFG_STR_MAX-1);
    strncpy(c->equalizer,      "ZF",     CFG_STR_MAX-1);
    strncpy(c->mimoMode,       "SISO",   CFG_STR_MAX-1);
    strncpy(c->harqRvSeq,      "IR",     CFG_STR_MAX-1);
    c->harqEnable = 0; c->harqMaxRetx = 4;
    c->tdlDelaySpreadNs = 300.0;
    c->transformPrecoding = 1;
    c->puschDfeEnable = 0;
    c->puschTurboEnable = 0; c->puschTurboIters = 3;
    c->pucchFormat = 0; c->pucchUciBits = 1; c->pucchNumSymbols = 4; c->pucchNumPrb = 1;
    strncpy(c->prachFormat,    "SHORT",  CFG_STR_MAX-1);
    c->prachRootSeqIndex = 1; c->prachNumCs = 13; c->prachMaxDelaySamples = 8;
    c->ulpcP0Dbm        = -95.0;
    c->ulpcPcmaxDbm     =  23.0;
    c->ulpcAlpha        =   0.8;
    c->ulpcNfDb         =   7.0;
    c->ulpcSinrTargetDb =  10.0;
    c->ulpcPlDb         = 100.0;
    c->ulpcNumSf        = 100;
    c->spatialCorrTx    = 0.0;
    strncpy(c->iqDumpFile,     "iq_dump.txt", CFG_STR_MAX-1);
    /* modulation and codeRate are set by calc_derived() via MCS table */
}

/* ---- file parsing ---- */
static void strip(char *s) {
    /* remove inline comments */
    char *h = strchr(s, '#');
    if (h) *h = '\0';
    /* remove whitespace in-place */
    int w = 0;
    for (int i = 0; s[i]; i++)
        if (!isspace((unsigned char)s[i])) s[w++] = s[i];
    s[w] = '\0';
}

static void parse_file(ConfigParser *p, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        strip(line);
        if (!line[0]) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (p->nkv >= CFG_KV_MAX) continue;
        snprintf(p->kv[p->nkv].key, CFG_STR_MAX, "%.*s", CFG_STR_MAX-1, line);
        snprintf(p->kv[p->nkv].val, CFG_STR_MAX, "%.*s", CFG_STR_MAX-1, eq + 1);
        p->nkv++;
    }
    fclose(f);
}

static void calc_derived(ConfigParser *p) {
    L1Config *c = &p->cfg;
    if (c->numRB == 0)
        c->numRB = nrb_from_bw_scs(c->bandwidthMHz, c->scsKHz);
    if (c->nfft == 0)
        c->nfft = fft_size(c->numRB);
    if (c->cpLengthFirst == 0)
        c->cpLengthFirst  = (160 * c->nfft) / 2048;
    if (c->cpLengthNormal == 0)
        c->cpLengthNormal = (144 * c->nfft) / 2048;
    c->numSubcarriers = c->numRB * 12;
    c->samplingRate   = (double)c->nfft * c->scsKHz * 1000.0;

    /* Per 3GPP TS 38.212: channel coding is spec-defined per physical channel type.
     * Config file CODING/MODULATION/CODE_RATE are ignored — derived here. */
    if (strcmp(c->physicalChannel, "PBCH") == 0 ||
        strcmp(c->physicalChannel, "PDCCH") == 0) {
        /* Control channels: always Polar code + QPSK (TS 38.212 Sec 7.3/7.3.3) */
        strncpy(c->coding,     "POLAR", CFG_STR_MAX - 1);
        strncpy(c->modulation, "QPSK",  CFG_STR_MAX - 1);
        c->codeRate = 0.0;  /* N/A: determined by AL and DCI/payload size */
    } else if (strcmp(c->physicalChannel, "PDSCH") == 0) {
        /* Data channel: always LDPC (TS 38.212 Sec 7.2), modulation from MCS table */
        strncpy(c->coding, "LDPC", CFG_STR_MAX - 1);
        MCSTableType tbl = mcs_table_from_str(c->mcsTableType);
        MCSEntry mcs = get_mcs_entry(c->mcsIndex, tbl);
        strncpy(c->modulation, mcs.modulation, CFG_STR_MAX - 1);
        c->codeRate = get_code_rate(&mcs);
    } else {
        /* NONE / BER / legacy: derive modulation and code rate from MCS table */
        MCSTableType tbl = mcs_table_from_str(c->mcsTableType);
        MCSEntry mcs = get_mcs_entry(c->mcsIndex, tbl);
        strncpy(c->modulation, mcs.modulation, CFG_STR_MAX - 1);
        c->codeRate = get_code_rate(&mcs);
    }
    c->modulation[CFG_STR_MAX - 1] = '\0';
}

static void validate_config(const L1Config *c) {
    int errors = 0;

#define CFG_ERR(fmt, ...) \
    do { fprintf(stderr, "[config error] " fmt "\n", ##__VA_ARGS__); errors++; } while(0)

    /* MCS table 문자열 */
    if (strcmp(c->mcsTableType,"TABLE1")!=0 &&
        strcmp(c->mcsTableType,"TABLE2")!=0 &&
        strcmp(c->mcsTableType,"TABLE3")!=0) {
        CFG_ERR("MCS_TABLE='%s' is invalid; must be TABLE1, TABLE2, or TABLE3",
                c->mcsTableType);
    } else {
        /* MCS index range */
        MCSTableType tbl = mcs_table_from_str(c->mcsTableType);
        int max_idx = get_max_mcs_index(tbl);
        if (c->mcsIndex < 0 || c->mcsIndex > max_idx)
            CFG_ERR("MCS_INDEX=%d out of range [0,%d] for %s",
                    c->mcsIndex, max_idx, c->mcsTableType);
    }

    /* SNR sweep */
    if (c->snrStep <= 0.0)
        CFG_ERR("SNR_STEP=%.4g must be > 0", c->snrStep);
    if (c->snrEnd < c->snrStart)
        CFG_ERR("SNR_END=%.1f < SNR_START=%.1f", c->snrEnd, c->snrStart);

    /* 시뮬레이션 횟수 */
    if (c->numTrials <= 0)
        CFG_ERR("NUM_TRIALS=%d must be > 0", c->numTrials);

    /* 이퀄라이저 */
    if (strcmp(c->equalizer,"ZF")!=0 && strcmp(c->equalizer,"MMSE")!=0)
        CFG_ERR("EQUALIZER='%s' is invalid; must be ZF or MMSE", c->equalizer);

    /* 공간 상관 */
    if (c->spatialCorrTx < 0.0 || c->spatialCorrTx >= 1.0)
        CFG_ERR("SPATIAL_CORR_TX=%.4g out of range [0, 1)", c->spatialCorrTx);

    /* HARQ */
    if (c->harqEnable) {
        if (c->harqMaxRetx < 1)
            CFG_ERR("HARQ_MAX_RETX=%d must be >= 1 when HARQ_ENABLE=1", c->harqMaxRetx);
        if (strcmp(c->harqRvSeq,"IR")!=0 && strcmp(c->harqRvSeq,"CHASE")!=0)
            CFG_ERR("HARQ_RV_SEQUENCE='%s' is invalid; must be IR or CHASE", c->harqRvSeq);
    }

    /* 리소스 그리드 기본 정합성 */
    if (c->numRB > 0 && c->nfft > 0 && c->numRB * 12 > c->nfft)
        CFG_ERR("12 * NUM_RB=%d exceeds NFFT=%d", c->numRB * 12, c->nfft);

#undef CFG_ERR

    if (errors > 0) {
        fprintf(stderr, "%d config error(s) found — aborting.\n", errors);
        exit(1);
    }
}

int config_parser_load(ConfigParser *p, const char *filename) {
    config_parser_init(p);
    FILE *test = fopen(filename, "r");
    if (!test) {
        fprintf(stderr, "Warning: Cannot open %s, using defaults.\n", filename);
        calc_derived(p);
        return 0;
    }
    fclose(test);
    parse_file(p, filename);
    L1Config *c = &p->cfg;
    c->bandwidthMHz  = kv_int(p, "BANDWIDTH_MHZ",  20);
    c->scsKHz        = kv_int(p, "SCS_KHZ",         30);
    c->numRB         = kv_int(p, "NUM_RB",            0);
    c->nfft          = kv_int(p, "NFFT",              0);
    c->cpLengthFirst = kv_int(p, "CP_LENGTH_FIRST",   0);
    c->cpLengthNormal= kv_int(p, "CP_LENGTH_NORMAL",  0);
    c->codeRate      = kv_dbl(p, "CODE_RATE",         0.5);
    c->numOfdmSymbols= kv_int(p, "NUM_OFDM_SYMBOLS", 50);
    c->snrStart      = kv_dbl(p, "SNR_START",        -6.0);
    c->snrEnd        = kv_dbl(p, "SNR_END",           10.0);
    c->snrStep       = kv_dbl(p, "SNR_STEP",           2.0);
    c->dciSize       = kv_int(p, "DCI_SIZE",           39);
    c->rnti          = (uint16_t)kv_int(p, "RNTI",   0x1234);
    c->pdcchAL       = kv_int(p, "PDCCH_AL",            4);
    c->mcsIndex      = kv_int(p, "MCS_INDEX",           10);
    c->tbSize        = kv_int(p, "TB_SIZE",              0);
    c->numTrials     = kv_int(p, "NUM_TRIALS",         1000);
    c->numBits       = kv_int(p, "NUM_BITS",              0);
    c->useDmrs       = kv_int(p, "USE_DMRS",             0);
    c->csirsRow      = kv_int(p, "CSIRS_ROW",            2);
    c->csirsScramID  = kv_int(p, "CSIRS_SCRAM_ID",       0);
    c->csirsSymbol   = kv_int(p, "CSIRS_SYMBOL",         4);
    c->csirsK0       = kv_int(p, "CSIRS_K0",             0);
    c->iqDumpEnable  = kv_int(p, "IQ_DUMP",              0);
    c->iqDumpSnr     = kv_dbl(p, "IQ_DUMP_SNR",        0.0);
    c->iqDumpTrials  = kv_int(p, "IQ_DUMP_TRIALS",     100);
    c->srsBandwidthRB= kv_int(p, "SRS_BW_RB",           16);
    c->srsCombSize   = kv_int(p, "SRS_COMB",             2);
    c->srsCombOffset = kv_int(p, "SRS_COMB_OFFSET",      0);
    c->srsCyclicShift= kv_int(p, "SRS_CYCLIC_SHIFT",     0);
    c->srsSeqGroupU  = kv_int(p, "SRS_SEQ_GROUP",        0);
    c->srsSeqNumV    = kv_int(p, "SRS_SEQ_NUM",          0);
    c->harqEnable    = kv_int(p, "HARQ_ENABLE",          0);
    c->harqMaxRetx   = kv_int(p, "HARQ_MAX_RETX",        4);
    c->tdlDelaySpreadNs = kv_dbl(p, "TDL_DELAY_SPREAD_NS", 300.0);
    c->transformPrecoding = kv_int(p, "TRANSFORM_PRECODING", 1);
    c->puschDfeEnable     = kv_int(p, "PUSCH_DFE_ENABLE",     0);
    c->puschTurboEnable  = kv_int(p, "PUSCH_TURBO_ENABLE",   0);
    c->puschTurboIters   = kv_int(p, "PUSCH_TURBO_ITERS",    3);
    c->pucchFormat     = kv_int(p, "PUCCH_FORMAT",       0);
    c->pucchUciBits    = kv_int(p, "PUCCH_UCI_BITS",     1);
    c->pucchNumSymbols = kv_int(p, "PUCCH_NUM_SYMBOLS",  4);
    c->pucchNumPrb     = kv_int(p, "PUCCH_NUM_PRB",      1);
    c->prachRootSeqIndex    = kv_int(p, "PRACH_ROOT_SEQ_INDEX",     1);
    c->prachNumCs           = kv_int(p, "PRACH_NUM_CS",            13);
    c->prachMaxDelaySamples = kv_int(p, "PRACH_MAX_DELAY_SAMPLES",  8);
    c->ulpcP0Dbm        = kv_dbl(p, "UL_PC_P0_DBM",        -95.0);
    c->ulpcPcmaxDbm     = kv_dbl(p, "UL_PC_P_CMAX_DBM",    23.0);
    c->ulpcAlpha        = kv_dbl(p, "UL_PC_ALPHA",           0.8);
    c->ulpcNfDb         = kv_dbl(p, "UL_PC_NF_DB",           7.0);
    c->ulpcSinrTargetDb = kv_dbl(p, "UL_PC_SINR_TARGET_DB", 10.0);
    c->ulpcPlDb         = kv_dbl(p, "UL_PC_PL_DB",         100.0);
    c->ulpcNumSf        = kv_int(p, "UL_PC_NUM_SF",          100);
    c->spatialCorrTx    = kv_dbl(p, "SPATIAL_CORR_TX",        0.0);
    kv_str(p, "MODULATION",      "QPSK",   c->modulation,     CFG_STR_MAX);
    kv_str(p, "CODING",          "LDPC",   c->coding,         CFG_STR_MAX);
    kv_str(p, "CHANNEL_MODEL",   "AWGN",   c->channelModel,   CFG_STR_MAX);
    kv_str(p, "PHYSICAL_CHANNEL","NONE",   c->physicalChannel,CFG_STR_MAX);
    kv_str(p, "SEARCH_SPACE",    "CSS",    c->searchSpace,    CFG_STR_MAX);
    kv_str(p, "MCS_TABLE",       "TABLE1", c->mcsTableType,   CFG_STR_MAX);
    kv_str(p, "EQUALIZER",       "ZF",     c->equalizer,      CFG_STR_MAX);
    kv_str(p, "MIMO_MODE",       "SISO",   c->mimoMode,       CFG_STR_MAX);
    kv_str(p, "HARQ_RV_SEQUENCE","IR",     c->harqRvSeq,      CFG_STR_MAX);
    kv_str(p, "PRACH_FORMAT",    "SHORT",  c->prachFormat,    CFG_STR_MAX);
    kv_str(p, "IQ_DUMP_FILE","iq_dump.txt",c->iqDumpFile,     CFG_STR_MAX);
    calc_derived(p);
    validate_config(&p->cfg);
    return 1;
}

void config_parser_print(const ConfigParser *p) {
    const L1Config *c = &p->cfg;
    MCSTableType tbl = mcs_table_from_str(c->mcsTableType);
    MCSEntry mcs = get_mcs_entry(c->mcsIndex, tbl);
    const char *tbl_ref =
        (tbl == MCS_TABLE2) ? "TS 38.214 Table 5.1.3.1-2" :
        (tbl == MCS_TABLE3) ? "TS 38.214 Table 5.1.3.1-3" :
                              "TS 38.214 Table 5.1.3.1-1";

    printf("=== L1 Configuration ===\n");
    printf("Bandwidth    : %d MHz\n",  c->bandwidthMHz);
    printf("SCS          : %d kHz\n",  c->scsKHz);
    printf("Num RB       : %d\n",      c->numRB);
    printf("Subcarriers  : %d\n",      c->numSubcarriers);
    printf("NFFT         : %d\n",      c->nfft);
    printf("CP (sym 0,7) : %d\n",      c->cpLengthFirst);
    printf("CP (normal)  : %d\n",      c->cpLengthNormal);
    printf("Sample Rate  : %.3f MHz\n",c->samplingRate / 1e6);
    printf("Channel      : %s\n",      c->channelModel);
    if (strcmp(c->channelModel, "TDL") == 0)
        printf("TDL Delay Spread : %.0f ns (approx profile)\n", c->tdlDelaySpreadNs);
    printf("SNR Range    : %.1f to %.1f dB (step %.1f)\n",
           c->snrStart, c->snrEnd, c->snrStep);
    int is_ctrl = (strcmp(c->physicalChannel, "PBCH")  == 0 ||
                   strcmp(c->physicalChannel, "PDCCH") == 0);
    printf("Coding       : %s\n", c->coding);
    if (is_ctrl) {
        printf("Modulation   : QPSK (Qm=2) [fixed by spec]\n");
    } else {
        printf("MCS Table    : %s (%s)\n", c->mcsTableType, tbl_ref);
        printf("MCS Index    : %d\n",      c->mcsIndex);
        printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, mcs.modulationOrder);
        printf("Code Rate    : %.4f (R*1024=%d)\n",
               get_code_rate(&mcs), (int)mcs.targetCodeRate);
        printf("Spectral Eff : %.3f bits/RE\n", mcs.spectralEff);
    }
    if (strcmp(c->physicalChannel, "NONE") != 0) {
        printf("Phys Channel : %s\n",  c->physicalChannel);
        printf("Num Trials   : %d\n",  c->numTrials);
        if (strcmp(c->physicalChannel, "PDCCH") == 0) {
            printf("DCI Size     : %d\n",    c->dciSize);
            printf("RNTI         : 0x%04x\n", c->rnti);
            printf("Search Space : %s\n",    c->searchSpace);
            printf("PDCCH AL     : %d\n",    c->pdcchAL);
        }
        if (strcmp(c->physicalChannel, "PDSCH") == 0) {
            if (c->tbSize > 0) printf("TB Size      : %d\n", c->tbSize);
            else               printf("TB Size      : auto\n");
            if (strcmp(c->mimoMode, "SISO") != 0)
                printf("MIMO Mode    : %s\n", c->mimoMode);
            if (strcmp(c->mimoMode, "CL_4PORT") == 0 && c->spatialCorrTx > 0.0)
                printf("Spatial Corr : Tx rho=%.2f (Kronecker, XPOL 2x2 blocks)\n",
                       c->spatialCorrTx);
            if (c->harqEnable) {
                printf("HARQ         : enabled (%s, max %d tx)\n",
                       c->harqRvSeq, c->harqMaxRetx);
            }
        }
        if (strcmp(c->physicalChannel, "PUSCH") == 0) {
            printf("Transform Precoding : %s\n",
                   c->transformPrecoding ? "ON (DFT-s-OFDM)" : "OFF (CP-OFDM)");
            if (c->puschTurboEnable)
                printf("Turbo Eq     : enabled (%d iters, soft-PIC + LDPC extrinsic)\n",
                       c->puschTurboIters);
            else if (c->puschDfeEnable)
                printf("DFE          : enabled (MMSE+TDL residual-ISI cancellation)\n");
        }
        if (strcmp(c->physicalChannel, "PUCCH") == 0) {
            printf("PUCCH Format : %d\n", c->pucchFormat);
            printf("UCI Bits     : %d\n", c->pucchUciBits);
        }
        if (strcmp(c->physicalChannel, "PRACH") == 0) {
            printf("PRACH Format : %s (L_RA=%d)\n", c->prachFormat,
                   strcmp(c->prachFormat, "LONG") == 0 ? 839 : 139);
            printf("Root Seq u   : %d\n", c->prachRootSeqIndex);
            printf("N_CS         : %d\n", c->prachNumCs);
            printf("Max Delay    : %d samples\n", c->prachMaxDelaySamples);
        }
        if (strcmp(c->physicalChannel, "ULPC") == 0) {
            printf("UL PC P0     : %.1f dBm/RB\n", c->ulpcP0Dbm);
            printf("UL PC P_CMAX : %.1f dBm\n",    c->ulpcPcmaxDbm);
            printf("UL PC alpha  : %.2f\n",          c->ulpcAlpha);
            printf("UL PC NF     : %.1f dB\n",      c->ulpcNfDb);
            printf("SINR target  : %.1f dB\n",      c->ulpcSinrTargetDb);
            printf("Path Loss    : %.1f dB (시계열 고정값)\n", c->ulpcPlDb);
            printf("Num SF       : %d\n",            c->ulpcNumSf);
        }
    }
    printf("========================\n");
}

L1Config config_parser_get(const ConfigParser *p) { return p->cfg; }
