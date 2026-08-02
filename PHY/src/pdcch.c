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
#include "channel_estimation.h"
#include "tdl.h"
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

/* PDCCH over a fading channel (FLAT_FADING or TDL), genie-aided (perfect
   CSI), same rationale as PBCH/PUCCH (no DMRS/channel-estimation pipeline
   for this control channel in this LLS). Only the actual DCI transmission
   (tx_cand_idx) goes through the fading channel; the other blind-decoding
   candidates stay pure-noise draws exactly as in run_pdcch_simulation()
   above -- those aren't associated with any real transmission, so there is
   no "channel" for them to fade through, fading or not.

   Rather than doing full per-symbol MMSE-aware demapping inside the
   candidate loop (which would need every candidate's h_data threaded
   through, adding real complexity for a control channel), this reuses
   the same "uniform/averaged effective noise variance" simplification
   run_pucch_format3_tdl_simulation() already documents for its ZF path,
   applied to both ZF (mean 1/|h|^2, exact for a single-tap flat channel,
   Parseval-approximate for TDL) and MMSE (mean (1-alpha)/alpha) -- one
   scalar env for the TX candidate, fed into the same qam_demap_llr()
   every candidate already uses. */
void run_pdcch_fading_simulation(const L1Config *cfg) {
    int dci_size = cfg->dciSize;
    int crc_bits = 24;
    int K        = dci_size + crc_bits;
    uint16_t rnti = cfg->rnti;
    int txAL     = cfg->pdcchAL;
    int is_tdl   = (strcmp(cfg->channelModel, "TDL") == 0);
    int use_mmse = (strcmp(cfg->equalizer, "MMSE") == 0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

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
    int nsym_tx = E_tx / 2;

    printf("=== PDCCH Simulation, %s Fading (genie-aided CSI, TX candidate only) ===\n",
           is_tdl ? "TDL Frequency-Selective" : "Flat");
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
    if (is_tdl)
        printf("Channel: TDL (DS=%.0fns, %d taps), Equalizer: %s\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS, use_mmse ? "MMSE" : "ZF");
    else
        printf("Channel: Flat Rayleigh (single tap, held for TX candidate)\n");
    printf("Trials per SNR: %d\n\n", cfg->numTrials);

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

    int *dci        = (int *)malloc(dci_size * sizeof(int));
    int *dci_crc    = (int *)malloc((dci_size + crc_bits) * sizeof(int));
    int *coded      = (int *)malloc(N_tx * sizeof(int));
    int *rm_tx      = (int *)malloc(E_tx * sizeof(int));
    cx_t *tx_syms   = (cx_t *)malloc(nsym_tx * sizeof(cx_t));
    cx_t *rx_raw    = (cx_t *)malloc(nsym_tx * sizeof(cx_t));
    cx_t *eq_tx     = (cx_t *)malloc(nsym_tx * sizeof(cx_t));
    cx_t *h_known   = (cx_t *)malloc(nsym_tx * sizeof(cx_t));
    double *alpha   = (double *)malloc(nsym_tx * sizeof(double));
    cx_t taps[TDL_MAX_TAPS];

    TDLChannel tdl_ch;
    FlatFadingChannel flat_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd+0.001; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        else        flat_fading_init(&flat_ch, snr);
        AWGNChannel ch; awgn_init(&ch, snr);
        int detections=0, misses=0, false_alarms=0;
        long long total_bit_err=0, total_det_bits=0;
        double nv = 1.0 / pow(10.0, snr/10.0);
        double N0 = nv;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(dci, dci_size);
            attach_crc_rnti(dci, dci_size, CRC24C, rnti, dci_crc);

            PolarCodec polar_tx;
            polar_init(&polar_tx, N_tx, K, E_tx);
            polar_encode(&polar_tx, dci_crc, coded);
            polar_free(&polar_tx);

            polar_rate_match(coded, N_tx, E_tx, K, rm_tx);
            qam_modulate(rm_tx, E_tx, "QPSK", tx_syms);

            /* channel + equalize for the actual TX candidate only */
            if (is_tdl) {
                tdl_draw(&tdl_ch, taps);
                tdl_channel_apply(&tdl_ch, taps, tx_syms, nsym_tx, rx_raw, h_known);
            } else {
                cx_t true_h;
                flat_fading_apply(&flat_ch, tx_syms, nsym_tx, rx_raw, &true_h);
                for (int d=0; d<nsym_tx; d++) h_known[d] = true_h;
            }
            double env_tx;
            if (use_mmse) {
                mmse_equalize(rx_raw, h_known, nsym_tx, N0, eq_tx, alpha);
                double mvar = 0.0;
                for (int d=0; d<nsym_tx; d++) mvar += (1.0-alpha[d])/alpha[d];
                env_tx = mvar / nsym_tx;
            } else {
                zf_equalize(rx_raw, h_known, nsym_tx, eq_tx);
                double inv_sum = 0.0;
                for (int d=0; d<nsym_tx; d++) {
                    double hp = CX_NORM(h_known[d]);
                    inv_sum += (hp > 1e-10) ? 1.0/hp : 1.0/1e-10;
                }
                env_tx = N0 * inv_sum / nsym_tx;
            }

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
                double cand_nv = nv;

                if (ci == tx_cand_idx) {
                    int copy = nsym < nsym_tx ? nsym : nsym_tx;
                    for (int s=0;s<copy;s++) rx_c[s] = eq_tx[s];
                    cand_nv = env_tx;
                } else {
                    cx_t *zero = (cx_t *)calloc(nsym, sizeof(cx_t));
                    awgn_add_noise(&ch, zero, nsym, 0, rx_c);
                    free(zero);
                }

                qam_demap_llr(rx_c, nsym, "QPSK", cand_nv, llr);

                polar_rate_dematch(llr, E_c, N_c, K, dm);

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
    printf("\nPDCCH + fading simulation complete.\n");

    free(dci); free(dci_crc); free(coded); free(rm_tx);
    free(tx_syms); free(rx_raw); free(eq_tx); free(h_known); free(alpha);
}
