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
void mimo_channel_draw_4x8(cx_t h[4][8]);    /* h[rx][tx], 8 Tx ports -> 4 Rx antennas (CL_8PORT) */
void mimo_channel_draw_4x16(cx_t h[4][16]);  /* h[rx][tx], 16 Tx ports -> 4 Rx antennas (EIGEN_16PORT) */
void mimo_channel_draw_4x32(cx_t h[4][32]);  /* h[rx][tx], 32 Tx ports -> 4 Rx antennas (CL_32PORT) */

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

/* Same Kronecker model as mimo_apply_tx_correlation_4x4(), generalised to
   N1=4 (CL_8PORT, port ordering [pol1_ant0..3, pol2_ant0..3]):
     - R_ant = 4x4 exponential-correlation matrix R_ant[i][j] = rho^|i-j|
       between the 4 co-located antennas of the SAME polarization (ports
       0-3 and 4-7) -- the direct N1=4 generalisation of the N1=2 block
       [[1,rho],[rho,1]] used above (which is exactly rho^|i-j| for a
       2-element ULA), same exponential-correlation-model philosophy
       (Loyka 2001; Bjornson et al., "Massive MIMO Networks"). Applied via
       Cholesky factor L (R_ant = L L^T) instead of the symmetric matrix
       square root used for N1=2: statistically equivalent for this
       purpose (input is iid complex Gaussian, whose distribution is
       rotation-invariant, so any L with L L^T = R_ant reproduces the same
       second-order statistics -- verified for N1=2 to match the existing
       a*I+b*J square root's variance/correlation exactly), and avoids a
       general eigendecomposition for the 4x4 case.
     - R_pol = 2x2 exponential-correlation block [[1,rho_xpol],[rho_xpol,1]]
       between the two polarizations at the SAME antenna position (ports
       {0,4},{1,5},{2,6},{3,7}) -- identical in form to the N1=2 case,
       just applied once per n1=0..3 instead of n1=0..1 (this factor is a
       relation between the two polarization groups, independent of N1).
   RX side left uncorrelated, same rationale as mimo_apply_tx_correlation_4x4().
   rho<=0 and/or rho_xpol<=0 is a no-op for that factor. */
void mimo_apply_tx_correlation_4x8(cx_t h[4][8], double rho, double rho_xpol);

/* Same Kronecker exponential-correlation philosophy as mimo_apply_tx_
   correlation_4x8(), generalised to a genuine 2D array: CL_32PORT
   (N1=4 horizontal, N2=4 vertical, port ordering per codebook_32port.c
   [pol1: n1=0..3 outer, n2=0..3 inner; pol2: same layout offset by 16]).
   Now THREE separable Kronecker factors (all commute -- each acts on a
   different tensor axis of the N1 x N2 x Npol structure, so applying
   them in any order gives the same combined operator):
     - R_horiz = 4x4 exponential-correlation matrix rho_h^|i-j| between
       the 4 antennas along the horizontal (N1) axis, for each fixed
       vertical (n2) position and each polarization -- applied via the
       same Cholesky-factor technique as the 8-port N1=4 case.
     - R_vert  = 4x4 exponential-correlation matrix rho_v^|i-j| between
       the 4 antennas along the vertical (N2) axis, for each fixed
       horizontal (n1) position and each polarization.
     - R_pol   = 2x2 exponential-correlation block [[1,rho_xpol],[rho_xpol,1]]
       between the two polarizations at the SAME (n1,n2) position (16
       repetitions, one per antenna position) -- identical in form to
       the 4/8-port case, independent of N1/N2.
   This is the standard separable/Kronecker model for a uniform
   rectangular array (URA): R_2D = R_horiz (x) R_vert, applying the two
   axis factors sequentially realises R_2D^(1/2) = L_horiz (x) L_vert
   exactly (same argument as the 8-port doc: sqrt(A (x) B) = sqrt(A) (x)
   sqrt(B) for PSD A,B). rho_h<=0 / rho_v<=0 / rho_xpol<=0 is a no-op for
   that factor. Implementation-defined approximation, NOT the exact TS
   38.101-4 Annex B matrices -- same philosophy as the other correlation
   models in this file. */
void mimo_apply_tx_correlation_4x32(cx_t h[4][32], double rho_h, double rho_v, double rho_xpol);

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

/* MMSE detection for 4Rx × 3Layer overdetermined system (CL_32PORT rank-3).
   Same de-biased MMSE formula as mimo_mmse_detect_4rx2(), generalised to a
   3×3 Gramian A = H_eff^H H_eff + N0·I. */
void mimo_mmse_detect_4rx3(const cx_t h_eff[4][3], const cx_t y[4], double N0,
                            cx_t x_hat[3], double noise_var[3]);

#endif
