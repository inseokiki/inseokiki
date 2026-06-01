#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "qam.h"
#include "awgn.h"

/* ─── Config ──────────────────────────────────────────────────────────── */

typedef struct {
    char   modulation[32]; /* QPSK / 16QAM / 64QAM / 256QAM / ALL */
    double snr_start;
    double snr_end;
    double snr_step;
    long long num_bits;    /* bits simulated per SNR point */
    int    show_theory;    /* 1: print theoretical BER alongside */
} BerCfg;

static void cfg_defaults(BerCfg *c) {
    strncpy(c->modulation, "QPSK", sizeof(c->modulation) - 1);
    c->snr_start   = 0.0;
    c->snr_end     = 14.0;
    c->snr_step    = 1.0;
    c->num_bits    = 100000000LL;
    c->show_theory = 1;
}

static void strip(char *s) {
    char *h = strchr(s, '#'); if (h) *h = '\0';
    int w = 0;
    for (int i = 0; s[i]; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n')
            s[w++] = s[i];
    s[w] = '\0';
}

static void cfg_load(BerCfg *c, const char *file) {
    cfg_defaults(c);
    FILE *f = fopen(file, "r");
    if (!f) {
        fprintf(stderr, "Warning: cannot open %s, using defaults.\n", file);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        strip(line);
        if (!line[0]) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line, *val = eq + 1;
        if      (strcmp(key, "MODULATION"  ) == 0)
            strncpy(c->modulation, val, sizeof(c->modulation) - 1);
        else if (strcmp(key, "SNR_START"   ) == 0) c->snr_start   = atof(val);
        else if (strcmp(key, "SNR_END"     ) == 0) c->snr_end     = atof(val);
        else if (strcmp(key, "SNR_STEP"    ) == 0) c->snr_step    = atof(val);
        else if (strcmp(key, "NUM_BITS"    ) == 0) c->num_bits    = atoll(val);
        else if (strcmp(key, "SHOW_THEORY" ) == 0) c->show_theory = atoi(val);
    }
    fclose(f);
}

/* ─── BER simulation for one modulation order ────────────────────────── */

#define SIM_CHUNK 100000   /* symbols per inner loop chunk */

static void run_ber(const BerCfg *cfg, const char *modulation) {
    int qm = qam_order(modulation);
    if (qm < 2) { fprintf(stderr, "Unknown modulation: %s\n", modulation); return; }

    /* Align num_bits to symbol boundary */
    long long nsym_total = cfg->num_bits / qm;
    long long nbits      = nsym_total * qm;

    int    *tx  = (int   *)malloc(SIM_CHUNK * qm * sizeof(int));
    int    *rx  = (int   *)malloc(SIM_CHUNK * qm * sizeof(int));
    cx_t   *sym = (cx_t  *)malloc(SIM_CHUNK      * sizeof(cx_t));
    cx_t   *rsm = (cx_t  *)malloc(SIM_CHUNK      * sizeof(cx_t));

    printf("Modulation   : %s (Qm=%d)\n", modulation, qm);
    printf("Bits/SNR pt  : %lld\n\n", nbits);

    if (cfg->show_theory)
        printf("%12s  %13s  %13s\n", "Eb/N0 (dB)", "BER (sim)", "BER (theory)");
    else
        printf("%12s  %13s\n", "Eb/N0 (dB)", "BER");
    int sep = cfg->show_theory ? 44 : 28;
    for (int i = 0; i < sep; i++) putchar('-');
    putchar('\n');

    for (double ebn0_db = cfg->snr_start;
         ebn0_db <= cfg->snr_end + 1e-9;
         ebn0_db += cfg->snr_step) {

        double ebn0_lin  = pow(10.0, ebn0_db / 10.0);
        long long errors = 0, left = nsym_total;

        while (left > 0) {
            int n = (int)(left < SIM_CHUNK ? left : SIM_CHUNK);
            for (int i = 0; i < n * qm; i++) tx[i] = rand() & 1;
            qam_mod  (tx, n * qm, qm, sym);
            awgn_add (sym, n, qm, ebn0_lin, rsm);
            qam_demod(rsm, n, qm, rx);
            for (int i = 0; i < n * qm; i++)
                if (tx[i] != rx[i]) errors++;
            left -= n;
        }

        double ber_sim = (double)errors / (double)nbits;
        if (cfg->show_theory) {
            double ber_th = qam_ber_theory(qm, ebn0_lin);
            printf("%12.4f  %13.4e  %13.4e\n", ebn0_db, ber_sim, ber_th);
        } else {
            printf("%12.4f  %13.4e\n", ebn0_db, ber_sim);
        }
        fflush(stdout);
    }

    free(tx); free(rx); free(sym); free(rsm);
}

/* ─── Main ────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    const char *cfg_file = "config/ber_config.txt";
    if (argc > 1) cfg_file = argv[1];

    BerCfg cfg;
    cfg_load(&cfg, cfg_file);
    srand((unsigned int)time(NULL));

    printf("=== 5G NR BER Simulator ===\n");
    printf("Config       : %s\n", cfg_file);
    printf("Channel      : AWGN\n");
    printf("SNR axis     : Eb/N0\n");
    printf("SNR range    : %.1f to %.1f dB (step %.1f)\n\n",
           cfg.snr_start, cfg.snr_end, cfg.snr_step);

    if (strcmp(cfg.modulation, "ALL") == 0) {
        /* Sweep all four modulation orders */
        const char *mods[] = {"QPSK", "16QAM", "64QAM", "256QAM"};
        for (int i = 0; i < 4; i++) {
            printf("--- %s ---\n", mods[i]);
            run_ber(&cfg, mods[i]);
            printf("\n");
        }
    } else {
        run_ber(&cfg, cfg.modulation);
    }

    printf("Done.\n");
    return 0;
}
