/* ================================================================
 *  pbch.c
 *  PBCH simulation loop
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "pbch.h"
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

void run_pbch_simulation(const L1Config *cfg) {
    const int payload  = 32;
    const int crc_bits = 24;
    const int K        = payload + crc_bits;  /* 56 */
    const int N        = 512;
    const int E        = 864;
    const int bps      = 2;  /* QPSK */

    printf("=== PBCH Simulation ===\n");
    printf("Payload: %d bits\n", payload);
    printf("CRC: CRC-24C (%d bits)\n", crc_bits);
    printf("Polar code: (%d, %d)\n", N, K);
    printf("Rate matching: %d -> %d bits\n", N, E);
    printf("Modulation: QPSK (%d symbols)\n", E/bps);
    printf("Trials per SNR: %d\n\n", cfg->numTrials);

    PolarCodec polar;
    polar_init(&polar, N, K, E, /*I_IL=*/1, /*n_PC=*/0, /*n_PC_wm=*/0);   /* TS 38.212 7.1.4/7.1.5 */

    int *payload_bits   = (int *)malloc(payload * sizeof(int));
    int *with_crc       = (int *)malloc((payload + crc_bits) * sizeof(int));
    int *coded          = (int *)malloc(N * sizeof(int));
    int *rm             = (int *)malloc(E * sizeof(int));
    cx_t *syms          = (cx_t *)malloc((E/bps) * sizeof(cx_t));
    cx_t *rx_syms       = (cx_t *)malloc((E/bps) * sizeof(cx_t));
    double *llr_qam     = (double *)malloc(E * sizeof(double));
    double *llr_dm      = (double *)malloc(N * sizeof(double));
    int *decoded        = (int *)malloc(K * sizeof(int));

    printf("%12s%15s\n", "SNR (dB)", "BLER");
    for (int i=0;i<27;i++) printf("-");
    printf("\n");

    AWGNChannel ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd+0.001; snr += cfg->snrStep) {
        awgn_init(&ch, snr);
        int blk_err = 0;
        for (int trial = 0; trial < cfg->numTrials; trial++) {
            gen_random_bits(payload_bits, payload);
            attach_crc(payload_bits, payload, CRC24C, with_crc);
            polar_encode(&polar, with_crc, coded);
            polar_rate_match(coded, N, E, K, rm);
            qam_modulate(rm, E, "QPSK", syms);
            awgn_add_noise(&ch, syms, E/bps, 0, rx_syms);
            double nv = 1.0 / pow(10.0, snr / 10.0);
            qam_demap_llr(rx_syms, E/bps, "QPSK", nv, llr_qam);
            polar_rate_dematch(llr_qam, E, N, K, llr_dm);
            polar_decode_scl(&polar, llr_dm, POLAR_SCL_L, /*use_crc=*/1, CRC24C,
                              /*use_rnti=*/0, 0, decoded);
            if (!check_crc(decoded, K, CRC24C)) blk_err++;
        }
        double bler = (double)blk_err / cfg->numTrials;
        printf("%12.1f%15.4e\n", snr, bler);
    }
    printf("\nPBCH simulation complete.\n");

    polar_free(&polar);
    free(payload_bits); free(with_crc); free(coded); free(rm);
    free(syms); free(rx_syms); free(llr_qam); free(llr_dm); free(decoded);
}

/* PBCH over a fading channel (FLAT_FADING or TDL), genie-aided (perfect
   CSI). Like PUCCH, PBCH has no DMRS/channel-estimation pipeline in this
   LLS (the SSB has its own PBCH-DMRS in the real spec, but this LLS
   doesn't model it), so rather than inventing a new pilot structure the
   true channel is taken directly from flat_fading_apply()'s true_h /
   tdl_channel_apply()'s h_out and used as if perfectly known at the
   receiver. FLAT_FADING draws one h held constant across all E/bps
   symbols (single-tap, so ZF/MMSE both reduce to matched-filter scaling);
   TDL draws a fresh 6-tap profile per trial and treats each of the E/bps
   symbols as one RE at SCS=cfg->scsKHz (same "no real OFDM grid, symbol
   index doubles as RE index" convention used by PUCCH F2/F3/PRACH-TDL). */
void run_pbch_fading_simulation(const L1Config *cfg) {
    const int payload  = 32;
    const int crc_bits = 24;
    const int K        = payload + crc_bits;  /* 56 */
    const int N        = 512;
    const int E        = 864;
    const int bps      = 2;  /* QPSK */
    const int nsym     = E / bps;
    int is_tdl   = (strcmp(cfg->channelModel, "TDL") == 0);
    int use_mmse = (strcmp(cfg->equalizer, "MMSE") == 0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PBCH Simulation, %s Fading (genie-aided CSI) ===\n",
           is_tdl ? "TDL Frequency-Selective" : "Flat");
    printf("Payload: %d bits\n", payload);
    printf("CRC: CRC-24C (%d bits)\n", crc_bits);
    printf("Polar code: (%d, %d)\n", N, K);
    printf("Rate matching: %d -> %d bits\n", N, E);
    printf("Modulation: QPSK (%d symbols)\n", nsym);
    if (is_tdl)
        printf("Channel: TDL (DS=%.0fns, %d taps), Equalizer: %s\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS, use_mmse ? "MMSE" : "ZF");
    else
        printf("Channel: Flat Rayleigh (single tap, held for whole SSB)\n");
    printf("Trials per SNR: %d\n\n", cfg->numTrials);

    PolarCodec polar;
    polar_init(&polar, N, K, E, /*I_IL=*/1, /*n_PC=*/0, /*n_PC_wm=*/0);   /* TS 38.212 7.1.4/7.1.5 */

    int *payload_bits   = (int *)malloc(payload * sizeof(int));
    int *with_crc       = (int *)malloc((payload + crc_bits) * sizeof(int));
    int *coded          = (int *)malloc(N * sizeof(int));
    int *rm             = (int *)malloc(E * sizeof(int));
    cx_t *syms          = (cx_t *)malloc(nsym * sizeof(cx_t));
    cx_t *rx_syms       = (cx_t *)malloc(nsym * sizeof(cx_t));
    cx_t *eq_syms       = (cx_t *)malloc(nsym * sizeof(cx_t));
    cx_t *h_known       = (cx_t *)malloc(nsym * sizeof(cx_t));
    double *llr_qam     = (double *)malloc(E * sizeof(double));
    double *llr_dm      = (double *)malloc(N * sizeof(double));
    int *decoded        = (int *)malloc(K * sizeof(int));
    cx_t taps[TDL_MAX_TAPS];

    printf("%12s%15s\n", "SNR (dB)", "BLER");
    for (int i=0;i<27;i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    FlatFadingChannel flat_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd+0.001; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        else        flat_fading_init(&flat_ch, snr);
        double N0 = 1.0 / pow(10.0, snr / 10.0);
        int blk_err = 0;
        for (int trial = 0; trial < cfg->numTrials; trial++) {
            gen_random_bits(payload_bits, payload);
            attach_crc(payload_bits, payload, CRC24C, with_crc);
            polar_encode(&polar, with_crc, coded);
            polar_rate_match(coded, N, E, K, rm);
            qam_modulate(rm, E, "QPSK", syms);

            /* Draw the channel: h_known[nsym] is either TDL's per-symbol
               (per-RE) response, or a single flat-fading tap repeated
               across all symbols (constant channel, so ZF/MMSE below
               naturally reduce to the same scalar-channel formulas the
               non-fading PBCH function would use if H were known). */
            if (is_tdl) {
                tdl_draw(&tdl_ch, taps);
                tdl_channel_apply(&tdl_ch, taps, syms, nsym, rx_syms, h_known);
            } else {
                cx_t true_h;
                flat_fading_apply(&flat_ch, syms, nsym, rx_syms, &true_h);
                for (int d=0; d<nsym; d++) h_known[d] = true_h;
            }

            if (use_mmse) {
                mmse_equalize(rx_syms, h_known, nsym, N0, eq_syms, NULL);
                qam_demap_llr_mmse(eq_syms, nsym, "QPSK", h_known, N0, llr_qam);
            } else {
                zf_equalize(rx_syms, h_known, nsym, eq_syms);
                double inv_sum = 0.0;
                for (int d=0; d<nsym; d++) {
                    double hp = CX_NORM(h_known[d]);
                    inv_sum += (hp > 1e-10) ? 1.0/hp : 1.0/1e-10;
                }
                double env = N0 * inv_sum / nsym;
                qam_demap_llr(eq_syms, nsym, "QPSK", env, llr_qam);
            }
            polar_rate_dematch(llr_qam, E, N, K, llr_dm);
            polar_decode_scl(&polar, llr_dm, POLAR_SCL_L, /*use_crc=*/1, CRC24C,
                              /*use_rnti=*/0, 0, decoded);
            if (!check_crc(decoded, K, CRC24C)) blk_err++;
        }
        double bler = (double)blk_err / cfg->numTrials;
        printf("%12.1f%15.4e\n", snr, bler);
    }
    printf("\nPBCH + fading simulation complete.\n");

    polar_free(&polar);
    free(payload_bits); free(with_crc); free(coded); free(rm);
    free(syms); free(rx_syms); free(eq_syms); free(h_known);
    free(llr_qam); free(llr_dm); free(decoded);
}
