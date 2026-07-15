/* ================================================================
 *  channel_estimation.c
 *  LS channel estimation, interpolation, ZF/MMSE equalization
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "channel_estimation.h"
#include <math.h>
#include <stdlib.h>

void ls_estimate(const cx_t *rx, const cx_t *tx, int n, cx_t *h) {
    for (int k = 0; k < n; k++) h[k] = rx[k] / tx[k];
}

void interpolate_channel(const cx_t *h_pilots, int num_pilots,
                         const int *pilot_pos, int num_active_sc,
                         cx_t *h_out) {
    if (num_pilots == 0) {
        for (int k = 0; k < num_active_sc; k++) h_out[k] = CX_ZERO;
        return;
    }
    for (int k = 0; k < num_active_sc; k++) {
        /* binary search for first pilot >= k */
        int lo = 0, hi = num_pilots;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (pilot_pos[mid] < k) lo = mid + 1;
            else                    hi = mid;
        }
        int ri = lo;  /* first pilot index >= k */
        if (ri == num_pilots) {
            h_out[k] = h_pilots[num_pilots - 1];
        } else if (pilot_pos[ri] == k) {
            h_out[k] = h_pilots[ri];
        } else if (ri == 0) {
            h_out[k] = h_pilots[0];
        } else {
            int li = ri - 1;
            int kL = pilot_pos[li], kR = pilot_pos[ri];
            double alpha = (double)(k - kL) / (kR - kL);
            h_out[k] = h_pilots[li] * (1.0 - alpha) + h_pilots[ri] * alpha;
        }
    }
}

void zf_equalize(const cx_t *rx, const cx_t *h, int n, cx_t *eq) {
    for (int k = 0; k < n; k++) {
        double hp = CX_NORM(h[k]);
        eq[k] = (hp < 1e-10) ? rx[k] : rx[k] / h[k];
    }
}

void mmse_equalize(const cx_t *rx, const cx_t *h, int n,
                   double N0, cx_t *eq, double *alpha_out) {
    for (int k = 0; k < n; k++) {
        double hp    = CX_NORM(h[k]);
        double denom = hp + N0;
        cx_t   w     = conj(h[k]) / denom;
        eq[k] = w * rx[k];
        if (alpha_out) alpha_out[k] = hp / denom;
    }
}
