/* ================================================================
 *  channel_estimation.h
 *  LS channel estimation, interpolation, ZF/MMSE equalization
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef CHANNEL_ESTIMATION_H
#define CHANNEL_ESTIMATION_H

#include "utils.h"

/* LS estimate: h_out[n] = rx_pilots[n] / tx_pilots[n] */
void ls_estimate(const cx_t *rx, const cx_t *tx, int n, cx_t *h_out);

/* Linear freq-domain interpolation.  pilot_pos[] sorted ascending.
   h_out[num_active_sc] must be pre-allocated. */
void interpolate_channel(const cx_t *h_pilots, int num_pilots,
                         const int *pilot_pos,
                         int num_active_sc, cx_t *h_out);

/* ZF equalization: eq[n] = rx[n] / h[n] */
void zf_equalize(const cx_t *rx, const cx_t *h, int n, cx_t *eq);

/* MMSE equalization: w[k]=h*[k]/(|h|^2+N0), eq[n]=w[n]*rx[n].
   If alpha_out != NULL, writes per-SC bias alpha[n]=|h|^2/(|h|^2+N0). */
void mmse_equalize(const cx_t *rx, const cx_t *h, int n,
                   double N0, cx_t *eq, double *alpha_out);

#endif
