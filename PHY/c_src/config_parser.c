#include "config_parser.h"
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
    c->codeRate      = 0.5; c->numOfdmSymbols = 50;
    c->snrStart      = -6;  c->snrEnd = 10; c->snrStep = 2;
    c->dciSize       = 39;  c->rnti = 0x1234;
    c->pdcchAL       = 4;   c->mcsIndex = 10;
    c->numTrials     = 1000;
    c->csirsRow      = 2;   c->csirsSymbol = 4;
    c->iqDumpSnr     = 0.0; c->iqDumpTrials = 100;
    c->srsBandwidthRB= 16;  c->srsCombSize = 2;
    strncpy(c->modulation,     "QPSK",   CFG_STR_MAX-1);
    strncpy(c->coding,         "LDPC",   CFG_STR_MAX-1);
    strncpy(c->channelModel,   "AWGN",   CFG_STR_MAX-1);
    strncpy(c->physicalChannel,"NONE",   CFG_STR_MAX-1);
    strncpy(c->searchSpace,    "CSS",    CFG_STR_MAX-1);
    strncpy(c->mcsTableType,   "TABLE1", CFG_STR_MAX-1);
    strncpy(c->equalizer,      "ZF",     CFG_STR_MAX-1);
    strncpy(c->iqDumpFile,     "iq_dump.txt", CFG_STR_MAX-1);
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
        strncpy(p->kv[p->nkv].key, line,   CFG_STR_MAX-1);
        strncpy(p->kv[p->nkv].val, eq + 1, CFG_STR_MAX-1);
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
    kv_str(p, "MODULATION",      "QPSK",   c->modulation,     CFG_STR_MAX);
    kv_str(p, "CODING",          "LDPC",   c->coding,         CFG_STR_MAX);
    kv_str(p, "CHANNEL_MODEL",   "AWGN",   c->channelModel,   CFG_STR_MAX);
    kv_str(p, "PHYSICAL_CHANNEL","NONE",   c->physicalChannel,CFG_STR_MAX);
    kv_str(p, "SEARCH_SPACE",    "CSS",    c->searchSpace,    CFG_STR_MAX);
    kv_str(p, "MCS_TABLE",       "TABLE1", c->mcsTableType,   CFG_STR_MAX);
    kv_str(p, "EQUALIZER",       "ZF",     c->equalizer,      CFG_STR_MAX);
    kv_str(p, "IQ_DUMP_FILE","iq_dump.txt",c->iqDumpFile,     CFG_STR_MAX);
    calc_derived(p);
    return 1;
}

void config_parser_print(const ConfigParser *p) {
    const L1Config *c = &p->cfg;
    printf("=== L1 Configuration ===\n");
    printf("Bandwidth    : %d MHz\n",  c->bandwidthMHz);
    printf("SCS          : %d kHz\n",  c->scsKHz);
    printf("Num RB       : %d\n",      c->numRB);
    printf("Subcarriers  : %d\n",      c->numSubcarriers);
    printf("NFFT         : %d\n",      c->nfft);
    printf("CP (sym 0,7) : %d\n",      c->cpLengthFirst);
    printf("CP (normal)  : %d\n",      c->cpLengthNormal);
    printf("Sample Rate  : %.3f MHz\n",c->samplingRate / 1e6);
    printf("Modulation   : %s\n",      c->modulation);
    printf("Coding       : %s\n",      c->coding);
    printf("Code Rate    : %.3f\n",    c->codeRate);
    printf("Channel      : %s\n",      c->channelModel);
    printf("SNR Range    : %.1f to %.1f dB (step %.1f)\n",
           c->snrStart, c->snrEnd, c->snrStep);
    if (strcmp(c->physicalChannel, "NONE") != 0) {
        printf("Phys Channel : %s\n",  c->physicalChannel);
        printf("Num Trials   : %d\n",  c->numTrials);
        if (strcmp(c->physicalChannel, "PDCCH") == 0) {
            printf("DCI Size     : %d\n",   c->dciSize);
            printf("RNTI         : 0x%04x\n",c->rnti);
            printf("Search Space : %s\n",   c->searchSpace);
            printf("PDCCH AL     : %d\n",   c->pdcchAL);
        }
        if (strcmp(c->physicalChannel, "PDSCH") == 0) {
            printf("MCS Index    : %d\n",  c->mcsIndex);
            printf("MCS Table    : %s\n",  c->mcsTableType);
            if (c->tbSize > 0) printf("TB Size      : %d\n", c->tbSize);
            else               printf("TB Size      : auto\n");
        }
    }
    printf("========================\n");
}

L1Config config_parser_get(const ConfigParser *p) { return p->cfg; }
