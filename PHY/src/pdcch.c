/* ================================================================
 *  pdcch.c
 *  PDCCH simulation loop (with blind decoding)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "pdcch.h"
#include "crc.h"
#include "polar.h"
#include "polar_rate_match.h"
#include "modulation.h"
#include "channel.h"
#include "utils.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int coded_bits_for_al(int al) { return 108 * al; }

static int polar_n_for_e_k(int E, int K) {
    int nMin = K < 32 ? 32 : K;
    int target = nMin > E ? nMin : E;
    int N = 32;
    while (N < target && N < 512) N *= 2;
    if (N < nMin) N = nMin;
    int p = 32; while (p < N) p *= 2;
    return p;
}

static int uss_max_cand(int al) {
    switch (al) {
        case 1: return 6; case 2: return 6;
        case 4: return 4; case 8: return 2;
        case 16: return 1; default: return 4;
    }
}

typedef struct { int al; int cand_idx; } Candidate;

/* per-AL cached Polar N and E */
typedef struct { int al; int N; int E; } ALEntry;

static int al_get_N(const ALEntry *t, int n, int al) {
    for (int i=0;i<n;i++) if (t[i].al==al) return t[i].N;
    return 512;
}
static int al_get_E(const ALEntry *t, int n, int al) {
    for (int i=0;i<n;i++) if (t[i].al==al) return t[i].E;
    return 108;
}

void run_pdcch_simulation(const L1Config *cfg) {
    int dci_size = cfg->dciSize;
    int crc_bits = 24;
    int K        = dci_size + crc_bits;
    uint16_t rnti = cfg->rnti;
    int txAL     = cfg->pdcchAL;

    /* build candidate list */
    Candidate cands[32]; int nc = 0;
    if (strcmp(cfg->searchSpace, "CSS") == 0) {
        int css_al[]   = {4,8,16};
        int css_num[]  = {4,2,1};
        for (int i=0;i<3;i++)
            for (int j=0;j<css_num[i];j++) {
                cands[nc].al = css_al[i]; cands[nc].cand_idx = j; nc++;
            }
    } else {
        int mc = uss_max_cand(txAL);
        for (int j=0;j<mc;j++) { cands[nc].al=txAL; cands[nc].cand_idx=j; nc++; }
    }

    int tx_cand_idx = -1;
    for (int i=0;i<nc;i++)
        if (cands[i].al==txAL && cands[i].cand_idx==0) { tx_cand_idx=i; break; }

    int E_tx = coded_bits_for_al(txAL);
    int N_tx = polar_n_for_e_k(E_tx, K);

    printf("=== PDCCH Simulation ===\n");
    printf("DCI size: %d bits\n", dci_size);
    printf("CRC: CRC-24C with RNTI masking (%d bits)\n", crc_bits);
    printf("RNTI: 0x%04x\n", rnti);
    printf("TX Aggregation Level: %d\n", txAL);
    printf("Polar code: (%d, %d)\n", N_tx, K);
    printf("Rate matching: %d -> %d bits\n", N_tx, E_tx);
    printf("Search Space: %s\n", cfg->searchSpace);
    printf("Blind decoding candidates: %d\n", nc);
    for (int i=0;i<nc;i++) {
        printf("  Candidate %d: AL%d #%d%s\n",
               i, cands[i].al, cands[i].cand_idx,
               i==tx_cand_idx ? " (TX)" : "");
    }
    printf("Trials per SNR: %d\n\n", cfg->numTrials);

    /* build AL table */
    ALEntry al_tbl[8]; int n_al = 0;
    for (int i=0;i<nc;i++) {
        int al = cands[i].al;
        int found = 0;
        for (int j=0;j<n_al;j++) if (al_tbl[j].al==al) { found=1; break; }
        if (!found) {
            al_tbl[n_al].al = al;
            al_tbl[n_al].E  = coded_bits_for_al(al);
            al_tbl[n_al].N  = polar_n_for_e_k(al_tbl[n_al].E, K);
            n_al++;
        }
    }

    printf("%12s%12s%12s%15s%15s\n",
           "SNR (dB)","P_detect","P_miss","P_false_alarm","BER");
    for (int i=0;i<66;i++) printf("-");
    printf("\n");

    /* allocate buffers */
    int *dci        = (int *)malloc(dci_size * sizeof(int));
    int *dci_crc    = (int *)malloc((dci_size + crc_bits) * sizeof(int));
    int *coded      = (int *)malloc(N_tx * sizeof(int));
    int *rm_tx      = (int *)malloc(E_tx * sizeof(int));
    cx_t *tx_syms   = (cx_t *)malloc((E_tx/2) * sizeof(cx_t));
    cx_t *rx_tx     = (cx_t *)malloc((E_tx/2) * sizeof(cx_t));

    AWGNChannel ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd+0.001; snr += cfg->snrStep) {
        awgn_init(&ch, snr);
        int detections=0, misses=0, false_alarms=0;
        long long total_bit_err=0, total_det_bits=0;
        double nv = 1.0 / pow(10.0, snr/10.0);

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(dci, dci_size);
            attach_crc_rnti(dci, dci_size, CRC24C, rnti, dci_crc);

            PolarCodec polar_tx;
            polar_init(&polar_tx, N_tx, K, E_tx);
            polar_encode(&polar_tx, dci_crc, coded);
            polar_free(&polar_tx);

            polar_rate_match(coded, N_tx, E_tx, K, rm_tx);
            qam_modulate(rm_tx, E_tx, "QPSK", tx_syms);
            awgn_add_noise(&ch, tx_syms, E_tx/2, 0, rx_tx);

            int detected=0, false_alarm=0;
            for (int ci=0; ci<nc && !detected && !false_alarm; ci++) {
                int al   = cands[ci].al;
                int E_c  = al_get_E(al_tbl, n_al, al);
                int N_c  = al_get_N(al_tbl, n_al, al);
                int nsym = E_c / 2;

                cx_t  *rx_c  = (cx_t *)malloc(nsym * sizeof(cx_t));
                double *llr  = (double *)malloc(E_c * sizeof(double));
                double *dm   = (double *)malloc(N_c * sizeof(double));
                int    *dec  = (int *)malloc(K * sizeof(int));

                if (ci == tx_cand_idx) {
                    int copy = nsym < E_tx/2 ? nsym : E_tx/2;
                    for (int s=0;s<copy;s++) rx_c[s] = rx_tx[s];
                } else {
                    cx_t *zero = (cx_t *)calloc(nsym, sizeof(cx_t));
                    awgn_add_noise(&ch, zero, nsym, 0, rx_c);
                    free(zero);
                }

                qam_demap_llr(rx_c, nsym, "QPSK", nv, llr);

                int *rm_c = (int *)malloc(E_c * sizeof(int));
                /* for rate-dematch we need a Polar N for this AL */
                polar_rate_dematch(llr, E_c, N_c, K, dm);
                free(rm_c);

                PolarCodec polar_rx;
                polar_init(&polar_rx, N_c, K, E_c);
                polar_decode(&polar_rx, dm, dec);
                polar_free(&polar_rx);

                if (check_crc_rnti(dec, K, CRC24C, rnti)) {
                    if (ci == tx_cand_idx) {
                        detected = 1;
                        for (int b=0;b<dci_size;b++)
                            if (dec[b]!=dci[b]) total_bit_err++;
                        total_det_bits += dci_size;
                    } else {
                        false_alarm = 1;
                    }
                }
                free(rx_c); free(llr); free(dm); free(dec);
            }
            if (detected)        detections++;
            else if (false_alarm) false_alarms++;
            else                  misses++;
        }
        double pd = (double)detections / cfg->numTrials;
        double pm = (double)misses     / cfg->numTrials;
        double pf = (double)false_alarms / cfg->numTrials;
        double ber = total_det_bits > 0 ?
                     (double)total_bit_err / total_det_bits : 0.0;
        printf("%12.1f%12.4f%12.4f%15.4e%15.4e\n",
               snr, pd, pm, pf, ber);
    }
    printf("\nPDCCH simulation complete.\n");

    free(dci); free(dci_crc); free(coded); free(rm_tx);
    free(tx_syms); free(rx_tx);
}
