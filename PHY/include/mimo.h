/* ================================================================
 *  mimo.h
 *  2x2 SU-MIMO channel model + MRC/ZF/MMSE detection
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef MIMO_H
#define MIMO_H

#include "utils.h"

typedef struct { double snr_db; } MIMOChannel;

void mimo_channel_init(MIMOChannel *ch, double snr_db);

/* Draw Rayleigh coefficients ~ CN(0,1) iid, held constant for one trial
   (block-flat, same convention as flat_fading_apply in channel.c). */
void mimo_channel_draw_1x2(cx_t h[2]);       /* 1 Tx layer -> 2 Rx antennas (SIMO) */
void mimo_channel_draw_2x2(cx_t h[2][2]);    /* h[rx][tx], 2 Tx layers -> 2 Rx antennas */

/* y[r] = h[r]*tx + n[r], independent AWGN per Rx antenna, Es/N0 = snr_db */
void mimo_channel_apply_1x2(const MIMOChannel *ch, const cx_t h[2],
                            cx_t tx, cx_t y[2]);

/* y[r] = sum_t h[r][t]*tx[t] + n[r], independent AWGN per Rx antenna */
void mimo_channel_apply_2x2(const MIMOChannel *ch, cx_t h[2][2],
                            const cx_t tx[2], cx_t y[2]);

/* Maximal Ratio Combining (1x2 SIMO, receive diversity).
   x_hat is unbiased; noise_var is the equivalent post-combining noise
   variance, ready to feed into qam_demap_llr(). */
void mrc_combine(const cx_t h[2], const cx_t y[2], double N0,
                 cx_t *x_hat, double *noise_var);

/* Zero-Forcing detection (2x2 spatial multiplexing). x_hat = H^-1 y.
   noise_var[t] = N0 * [(H^-1)(H^-1)^H]_tt (noise-enhanced, unbiased). */
void mimo_zf_detect(cx_t h[2][2], const cx_t y[2], double N0,
                    cx_t x_hat[2], double noise_var[2]);

/* MMSE detection (2x2 spatial multiplexing).
   W = (H^H H + N0 I)^-1 H^H, output is de-biased so it can be fed
   directly into qam_demap_llr() along with noise_var. */
void mimo_mmse_detect(cx_t h[2][2], const cx_t y[2], double N0,
                      cx_t x_hat[2], double noise_var[2]);

#endif
