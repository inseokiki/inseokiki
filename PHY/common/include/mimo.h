#ifndef MIMO_H
#define MIMO_H

/*
 * MIMO Receive Processing — MRC (Maximal Ratio Combining)
 *
 * Model: single transmit layer, Nrx receive antennas
 *
 *   y_m[k] = h_m[k] * x[k] + n_m[k],   m = 0..Nrx-1,  k = SC index
 *   n_m[k] ~ CN(0, N0)
 *
 * MRC weight at SC k:
 *   w[k] = h^H[k] / ||h[k]||^2,   h[k] = [h_0[k], ..., h_{Nrx-1}[k]]^T
 *
 * Combined signal:
 *   y_mrc[k] = w[k]^H * y[k] = x[k] + n_eff[k]
 *   n_eff[k] ~ CN(0,  N0 / ||h[k]||^2)
 *
 * Effective noise variance per SC:
 *   nv_eff[k] = N0 / sum_m |h_m[k]|^2
 */

#include "utils.h"

#define MIMO_MAX_RX 4

/*
 * mrc_combine — MRC combining of Nrx received signals.
 *
 *  rx[m]    : received SC array for antenna m,  length num_sc  (read-only)
 *  h[m]     : channel estimate for antenna m,   length num_sc  (read-only)
 *  Nrx      : number of receive antennas (1-MIMO_MAX_RX)
 *  num_sc   : number of subcarriers to process
 *  N0       : noise variance per (real+imag) → total CN noise power = N0
 *  y_mrc    : output combined signal           [num_sc]
 *  nv_eff   : output per-SC effective noise variance [num_sc]
 *             = N0 / sum_m |h_m[k]|^2
 *             (passes through N0 when Nrx=1 and h_norm≈0 → avoids NaN)
 */
void mrc_combine(const cx_t * const *rx, const cx_t * const *h,
                 int Nrx, int num_sc, double N0,
                 cx_t *y_mrc, double *nv_eff);

#endif /* MIMO_H */
