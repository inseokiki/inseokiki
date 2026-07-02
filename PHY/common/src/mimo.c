#include "mimo.h"

void mrc_combine(const cx_t * const *rx, const cx_t * const *h,
                 int Nrx, int num_sc, double N0,
                 cx_t *y_mrc, double *nv_eff)
{
    for (int k = 0; k < num_sc; k++) {
        /* Compute ||h[k]||^2 = sum_m |h_m[k]|^2 */
        double h_norm2 = 0.0;
        for (int m = 0; m < Nrx; m++)
            h_norm2 += CX_NORM(h[m][k]);

        if (h_norm2 < 1e-12) {
            y_mrc[k]  = CX_ZERO;
            nv_eff[k] = N0;
            continue;
        }

        /* MRC: y_mrc[k] = sum_m conj(h_m[k]) * y_m[k] / ||h[k]||^2 */
        cx_t combined = CX_ZERO;
        for (int m = 0; m < Nrx; m++)
            combined += conj(h[m][k]) * rx[m][k];
        y_mrc[k]  = combined / h_norm2;
        nv_eff[k] = N0 / h_norm2;
    }
}
