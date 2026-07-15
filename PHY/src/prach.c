/* ================================================================
 *  prach.c
 *  PRACH random access -- preamble detection + timing advance estimation
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "prach.h"
#include "tdl.h"
#include "utils.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* PRACH (UL random access), TS 38.211 Sec 6.3.3. Unlike PDSCH/PUSCH/PUCCH,
 * this LLS models PRACH entirely in the sequence domain (no OFDM grid, no
 * CP/guard-time) -- the same simplification already used for SRS/PUCCH F0/F1.
 * The preamble is a cyclically-shifted Zadoff-Chu root sequence:
 *
 *   x_u(n)   = exp(-j*pi*u*n*(n+1)/L_RA),           n = 0..L_RA-1
 *   x_u,v(n) = x_u((n + v*N_CS) mod L_RA)             (TS 38.211 6.3.3.1)
 *
 * L_RA is prime for both long (839) and short (139) formats, so -- unlike
 * PUCCH's length-12 approximation -- the exact ZC formula applies with no
 * approximation. Only a single root sequence is supported (spec fills the
 * 64-preamble set from multiple roots if one root's cyclic-shift count is
 * too small -- out of scope here, implementation-defined simplification).
 *
 * The unknown propagation delay is modeled as a circular shift (mod L_RA)
 * of the received sequence rather than linear convolution into a padded
 * guard-time buffer -- this keeps the ideal periodic autocorrelation
 * property of the ZC sequence (which is exactly what makes single-shot
 * joint preamble+timing detection possible) while avoiding a second,
 * unrelated buffer-sizing model (implementation-defined simplification).
 *
 * Detection sweeps every circular shift s=0..L_RA-1 in one correlation
 * pass (direct O(L_RA^2) sum, same "no FFT dependency" philosophy as
 * dft_precode.c's O(M^2) DFT) and recovers both which preamble (v_hat =
 * s_hat/N_CS) and the timing advance within that preamble's zone
 * (d_hat = s_hat mod N_CS) from the single correlation peak -- this is
 * the same principle a real gNB's matched-filter/FFT-based PRACH
 * correlator uses. */

static void zc_root_sequence(int u, int L_RA, cx_t *out) {
    for (int n = 0; n < L_RA; n++) {
        double phase = -PHY_PI * (double)u * (double)n * (n + 1) / L_RA;
        out[n] = CX_MAKE(cos(phase), sin(phase));
    }
}

void run_prach_simulation(const L1Config *cfg) {
    int is_long = (strcmp(cfg->prachFormat, "LONG") == 0);
    int L_RA = is_long ? 839 : 139;

    int N_CS = cfg->prachNumCs > 0 ? cfg->prachNumCs : 13;
    if (N_CS > L_RA) N_CS = L_RA;
    int num_preambles = L_RA / N_CS;
    if (num_preambles < 1) num_preambles = 1;

    int max_delay = cfg->prachMaxDelaySamples;
    if (max_delay < 0) max_delay = 0;
    if (max_delay >= N_CS) max_delay = N_CS - 1;

    int u = cfg->prachRootSeqIndex;
    if (u < 1) u = 1;
    if (u >= L_RA) u = u % L_RA;
    if (u < 1) u = 1;

    cx_t *root = (cx_t *)malloc(L_RA * sizeof(cx_t));
    zc_root_sequence(u, L_RA, root);

    printf("=== PRACH (Random Access), Preamble Detection + TA Estimation ===\n");
    printf("Format       : %s (L_RA=%d)\n", is_long ? "LONG" : "SHORT", L_RA);
    printf("Root Seq u   : %d\n", u);
    printf("N_CS         : %d samples (%d preambles from this root)\n", N_CS, num_preambles);
    printf("Max Delay    : %d samples (true delay drawn uniformly in [0, max])\n", max_delay);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%18s%18s\n", "SNR (dB)", "Preamble ErrRate", "TA MAE (samples)");
    for (int i=0;i<48;i++) printf("-");
    printf("\n");

    cx_t *tx  = (cx_t *)malloc(L_RA * sizeof(cx_t));
    cx_t *rx  = (cx_t *)malloc(L_RA * sizeof(cx_t));
    double *corr_mag = (double *)malloc(L_RA * sizeof(double));

    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        double snrlin = pow(10.0, snr/10.0);
        double sigma  = sqrt(1.0 / (2.0 * snrlin));
        int preamble_err = 0;
        long long ta_abs_err_sum = 0;
        int ta_err_trials = 0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            int v_true = rand_uniform_int(num_preambles);
            int d_true = (max_delay > 0) ? rand_uniform_int(max_delay + 1) : 0;
            int s_true = v_true * N_CS + d_true;

            for (int n=0; n<L_RA; n++) tx[n] = root[(n + s_true) % L_RA];
            for (int n=0; n<L_RA; n++)
                rx[n] = tx[n] + CX_MAKE(randn()*sigma, randn()*sigma);

            for (int s=0; s<L_RA; s++) {
                cx_t acc = CX_ZERO;
                for (int n=0; n<L_RA; n++)
                    acc += rx[n] * conj(root[(n + s) % L_RA]);
                corr_mag[s] = CX_NORM(acc);
            }
            int s_hat = 0; double best = -1.0;
            for (int s=0; s<L_RA; s++)
                if (corr_mag[s] > best) { best = corr_mag[s]; s_hat = s; }

            int v_hat = s_hat / N_CS;
            int d_hat = s_hat % N_CS;

            if (v_hat != v_true) {
                preamble_err++;
            } else {
                int diff = d_hat - d_true;
                if (diff < 0) diff = -diff;
                ta_abs_err_sum += diff;
                ta_err_trials++;
            }
        }

        double err_rate = (double)preamble_err / cfg->numTrials;
        double ta_mae = ta_err_trials > 0 ? (double)ta_abs_err_sum / ta_err_trials : 0.0;
        printf("%12.1f%18.4f%18.4f\n", snr, err_rate, ta_mae);
    }
    printf("\nPRACH simulation complete.\n");

    free(root); free(tx); free(rx); free(corr_mag);
}

/* PRACH + TDL frequency-selective multipath, layered on top of the
 * propagation-delay model above rather than replacing it. The L_RA ZC
 * samples from zc_root_sequence() are re-interpreted here as frequency-
 * domain RE values occupying L_RA contiguous subcarriers of PRACH's own
 * resource grid (k1=0 starting offset -- absolute position within the
 * carrier doesn't affect detection, only the relative subcarrier
 * spacing fed to the channel does, so a fixed k1 is an implementation-
 * defined simplification, same rationale as PUCCH's fixed u=1 root).
 *
 * PRACH subcarrier spacing Delta_f_RA is itself implementation-defined:
 * TS 38.211 Table 6.3.3.2-x specifies several options per format
 * subtype, but this LLS's PRACH_FORMAT only distinguishes LONG/SHORT,
 * not individual subtypes (0-3 / A1-C2). LONG uses the 1.25kHz option
 * (5kHz for format 3 is out of scope); SHORT reuses the already-
 * configured carrier SCS (cfg->scsKHz), matching the spec's
 * Delta_f_RA = 15*2^mu kHz family used for short preambles.
 *
 * The true propagation delay s_true (same circular-shift-of-DFT-bins
 * construction as run_prach_simulation() above) is, by the DFT shift
 * theorem, exactly equivalent to a continuous delay
 * tau = s_true / (L_RA * Delta_f_RA) seconds -- i.e. what was an ad-hoc
 * sample-domain trick there is, here, the literal and physically exact
 * frequency-domain representation of a propagation delay on an
 * L_RA-subcarrier PRACH grid, so the shift construction itself needs no
 * change. What IS new is tdl_channel_apply() layering genuine per-
 * subcarrier multipath fading (reusing tdl.c unchanged, scs_hz=
 * Delta_f_RA) and its complex noise on top of that delay, in place of
 * the flat/single-path AWGN used above -- this spreads/attenuates the
 * correlation peak the way real multipath delay spread does, degrading
 * (not replacing) the timing-advance estimation target. The detection
 * algorithm itself (circular correlation sweep + N_CS-zone peak
 * decomposition into preamble index / TA) is unchanged. */
void run_prach_tdl_simulation(const L1Config *cfg) {
    int is_long = (strcmp(cfg->prachFormat, "LONG") == 0);
    int L_RA = is_long ? 839 : 139;

    int N_CS = cfg->prachNumCs > 0 ? cfg->prachNumCs : 13;
    if (N_CS > L_RA) N_CS = L_RA;
    int num_preambles = L_RA / N_CS;
    if (num_preambles < 1) num_preambles = 1;

    int max_delay = cfg->prachMaxDelaySamples;
    if (max_delay < 0) max_delay = 0;
    if (max_delay >= N_CS) max_delay = N_CS - 1;

    int u = cfg->prachRootSeqIndex;
    if (u < 1) u = 1;
    if (u >= L_RA) u = u % L_RA;
    if (u < 1) u = 1;

    double delta_f_ra = is_long ? 1250.0 : (double)cfg->scsKHz * 1000.0;

    cx_t *root = (cx_t *)malloc(L_RA * sizeof(cx_t));
    zc_root_sequence(u, L_RA, root);

    printf("=== PRACH (Random Access) + TDL Multipath, Preamble Detection + TA Estimation ===\n");
    printf("Format       : %s (L_RA=%d)\n", is_long ? "LONG" : "SHORT", L_RA);
    printf("Root Seq u   : %d\n", u);
    printf("N_CS         : %d samples (%d preambles from this root)\n", N_CS, num_preambles);
    printf("Max Delay    : %d samples (true delay drawn uniformly in [0, max])\n", max_delay);
    printf("PRACH SCS    : %.3f kHz (RE grid, %s)\n", delta_f_ra/1000.0,
           is_long ? "fixed 1.25kHz option" : "reuses carrier SCS");
    printf("Channel      : TDL, %d taps, DS=%.0fns, layered on true delay\n",
           TDL_MAX_TAPS, cfg->tdlDelaySpreadNs);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%18s%18s\n", "SNR (dB)", "Preamble ErrRate", "TA MAE (samples)");
    for (int i=0;i<48;i++) printf("-");
    printf("\n");

    cx_t *tx      = (cx_t *)malloc(L_RA * sizeof(cx_t));
    cx_t *rx      = (cx_t *)malloc(L_RA * sizeof(cx_t));
    cx_t *h_known = (cx_t *)malloc(L_RA * sizeof(cx_t));
    double *corr_mag = (double *)malloc(L_RA * sizeof(double));
    cx_t *taps    = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, delta_f_ra, snr);
        int preamble_err = 0;
        long long ta_abs_err_sum = 0;
        int ta_err_trials = 0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            int v_true = rand_uniform_int(num_preambles);
            int d_true = (max_delay > 0) ? rand_uniform_int(max_delay + 1) : 0;
            int s_true = v_true * N_CS + d_true;

            for (int n=0; n<L_RA; n++) tx[n] = root[(n + s_true) % L_RA];
            tdl_draw(&tdl_ch, taps);
            tdl_channel_apply(&tdl_ch, taps, tx, L_RA, rx, h_known);

            for (int s=0; s<L_RA; s++) {
                cx_t acc = CX_ZERO;
                for (int n=0; n<L_RA; n++)
                    acc += rx[n] * conj(root[(n + s) % L_RA]);
                corr_mag[s] = CX_NORM(acc);
            }
            int s_hat = 0; double best = -1.0;
            for (int s=0; s<L_RA; s++)
                if (corr_mag[s] > best) { best = corr_mag[s]; s_hat = s; }

            int v_hat = s_hat / N_CS;
            int d_hat = s_hat % N_CS;

            if (v_hat != v_true) {
                preamble_err++;
            } else {
                int diff = d_hat - d_true;
                if (diff < 0) diff = -diff;
                ta_abs_err_sum += diff;
                ta_err_trials++;
            }
        }

        double err_rate = (double)preamble_err / cfg->numTrials;
        double ta_mae = ta_err_trials > 0 ? (double)ta_abs_err_sum / ta_err_trials : 0.0;
        printf("%12.1f%18.4f%18.4f\n", snr, err_rate, ta_mae);
    }
    printf("\nPRACH + TDL simulation complete.\n");

    free(root); free(tx); free(rx); free(h_known); free(corr_mag); free(taps);
}
