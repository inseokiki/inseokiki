/* ================================================================
 *  mimo.h
 *  SU-MIMO channel model + MRC/ZF/MMSE detection (2x2 and 4x4)
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
void mimo_channel_draw_4x4(cx_t h[4][4]);    /* h[rx][tx], 4 Tx layers -> 4 Rx antennas */

/* Apply TX-side spatial correlation (Kronecker model, H_corr = H_iid * Rtx^(1/2))
   in place to an already-drawn iid channel from mimo_channel_draw_4x4().
   Rtx = R_pol (x) R_ant (Kronecker product, "(x)" = tensor product), matching
   codebook.h's port ordering [pol1_ant0, pol1_ant1, pol2_ant0, pol2_ant1]
   (pol = outer index, ant = inner index):
     - R_ant = 2x2 exponential-correlation block [[1,rho],[rho,1]] between the
       two co-located antennas of the SAME polarization (ports 0-1 and 2-3).
     - R_pol = 2x2 exponential-correlation block [[1,rho_xpol],[rho_xpol,1]]
       between the two polarizations at the SAME antenna position (ports 0-2
       and 1-3), representing finite XPD leakage (rho_xpol=0 <=> infinite
       XPD, i.e. the two polarizations stay fully independent).
   Because sqrt(A (x) B) = sqrt(A) (x) sqrt(B) for PSD A,B, and (A(x)I)(I(x)B)
   = A(x)B, applying Rtx^(1/2) reduces to two independent elementary mixing
   passes (rho across ports {0,1}/{2,3}, then rho_xpol across ports {0,2}/
   {1,3}) using the SAME closed-form 2x2 block square root for both (a=avg,
   b=half-diff of sqrt(1+-rho)) -- no general eigendecomposition needed.
   RX side is left uncorrelated -- the gNB's compact array is the relevant
   correlated side for a CSI-RS codebook study, not the UE. This is a
   standard textbook Kronecker model (Kermoal et al. 2002; see also Tse &
   Viswanath, Bjornson "Massive MIMO Networks"; the polarization factor
   follows the same dual-pol Kronecker-extension philosophy as Coldrey,
   "Modeling and Capacity of Polarized MIMO Channels", 2008), NOT the exact
   TS 38.101-4 Annex B correlation matrices (implementation-defined
   approximation, same philosophy as tdl.c's PDP). rho<=0 and/or
   rho_xpol<=0 is a no-op for that factor (rho=rho_xpol=0 exactly recovers
   the iid channel). */
void mimo_apply_tx_correlation_4x4(cx_t h[4][4], double rho, double rho_xpol);

/* y[r] = h[r]*tx + n[r], independent AWGN per Rx antenna, Es/N0 = snr_db */
void mimo_channel_apply_1x2(const MIMOChannel *ch, const cx_t h[2],
                            cx_t tx, cx_t y[2]);

/* y[r] = sum_t h[r][t]*tx[t] + n[r], independent AWGN per Rx antenna */
void mimo_channel_apply_2x2(const MIMOChannel *ch, cx_t h[2][2],
                            const cx_t tx[2], cx_t y[2]);
void mimo_channel_apply_4x4(const MIMOChannel *ch, cx_t h[4][4],
                            const cx_t tx[4], cx_t y[4]);

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

/* 4x4 SU-MIMO detectors (4 spatial layers, 4 Rx antennas).
   Same ZF / MMSE semantics as the 2x2 versions; internally uses
   Gauss-Jordan elimination for the 4x4 matrix inverse. */
void mimo_zf_detect_4x4(cx_t h[4][4], const cx_t y[4], double N0,
                         cx_t x_hat[4], double noise_var[4]);
void mimo_mmse_detect_4x4(cx_t h[4][4], const cx_t y[4], double N0,
                           cx_t x_hat[4], double noise_var[4]);

/* MRC combining for 4-Rx antennas.  Used when a rank-1 precoder W reduces
   the effective channel to h_eff[4] = H·W (a single 4-element vector).
   Generalises mrc_combine() from 2 to 4 Rx antennas; interface is identical. */
void mrc_combine_4rx(const cx_t h[4], const cx_t y[4], double N0,
                     cx_t *x_hat, double *noise_var);

/* MMSE detection for 4Rx × 2Layer overdetermined system.
   h_eff[r][l] = (H·W)[r][l] is the 4×2 effective channel after rank-2 precoding.
   Builds the 2×2 Gramian A = H_eff^H H_eff + N0·I and applies the same
   de-biased MMSE formula as mimo_mmse_detect():
     x_hat[l]     = (A⁻¹ H_eff^H y)[l] / α_l
     noise_var[l] = (1 - α_l) / α_l
     α_l          = 1 - N0·Re{(A⁻¹)_ll} */
void mimo_mmse_detect_4rx2(const cx_t h_eff[4][2], const cx_t y[4], double N0,
                            cx_t x_hat[2], double noise_var[2]);

#endif
