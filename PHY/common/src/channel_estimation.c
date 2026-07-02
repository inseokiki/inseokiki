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

/* ── LMMSE channel estimation ────────────────────────────────────────────── */

/* Solve A*X = B in-place (partial pivoting Gaussian elimination).
 * A: n×n, B: n×nrhs, both row-major.  B is overwritten with the solution. */
static void gauss_solve(double *A, double *B, int n, int nrhs) {
    for (int k = 0; k < n; k++) {
        /* find max-magnitude pivot */
        int pk = k;
        double amax = fabs(A[k*n+k]);
        for (int i = k+1; i < n; i++) {
            if (fabs(A[i*n+k]) > amax) { amax = fabs(A[i*n+k]); pk = i; }
        }
        if (pk != k) {
            for (int j = 0; j < n;    j++) { double t = A[k*n+j];    A[k*n+j]    = A[pk*n+j];    A[pk*n+j]    = t; }
            for (int j = 0; j < nrhs; j++) { double t = B[k*nrhs+j]; B[k*nrhs+j] = B[pk*nrhs+j]; B[pk*nrhs+j] = t; }
        }
        double piv = A[k*n+k];
        if (fabs(piv) < 1e-15) continue;
        for (int i = k+1; i < n; i++) {
            double fac = A[i*n+k] / piv;
            for (int j = k; j < n;    j++) A[i*n+j]    -= fac * A[k*n+j];
            for (int j = 0; j < nrhs; j++) B[i*nrhs+j] -= fac * B[k*nrhs+j];
        }
    }
    for (int k = n-1; k >= 0; k--) {
        if (fabs(A[k*n+k]) < 1e-15) continue;
        for (int j = 0; j < nrhs; j++) {
            for (int i = k+1; i < n; i++) B[k*nrhs+j] -= A[k*n+i] * B[i*nrhs+j];
            B[k*nrhs+j] /= A[k*n+k];
        }
    }
}

void lmmse_filter_build(LMMSEFilter *f,
                        const int *pilot_pos, int np,
                        const int *data_pos,  int nd,
                        double N0, double scs_hz, double tau_rms_ns) {
    f->np = np; f->nd = nd;
    f->W  = (double *)malloc((size_t)nd * np * sizeof(double));

    double tau_s = tau_rms_ns * 1e-9;
    double beta  = 2.0 * M_PI * tau_s * scs_hz;  /* 2π·τ_rms·Δf */

    /* A = R_pp + N0·I  (np×np) */
    double *A = (double *)malloc((size_t)np * np * sizeof(double));
    for (int i = 0; i < np; i++) {
        for (int j = 0; j < np; j++) {
            double dk = fabs((double)(pilot_pos[i] - pilot_pos[j]));
            A[i*np+j] = (beta > 1e-12) ? exp(-beta * dk) : 1.0;
        }
        A[i*np+i] += N0;
    }

    /* B = R_dp^T  (np×nd): B[i*nd+k] = R(|data_pos[k] - pilot_pos[i]|) */
    double *B = (double *)malloc((size_t)np * nd * sizeof(double));
    for (int i = 0; i < np; i++)
        for (int k = 0; k < nd; k++) {
            double dk = fabs((double)(data_pos[k] - pilot_pos[i]));
            B[i*nd+k] = (beta > 1e-12) ? exp(-beta * dk) : 1.0;
        }

    /* Solve A * Z = B  →  Z = W^T  (np×nd) */
    gauss_solve(A, B, np, nd);

    /* Store W = Z^T  (nd×np): W[k*np+i] = B[i*nd+k] */
    for (int k = 0; k < nd; k++)
        for (int i = 0; i < np; i++)
            f->W[k*np+i] = B[i*nd+k];

    free(A); free(B);
}

void lmmse_filter_apply(const LMMSEFilter *f, const cx_t *h_ls, cx_t *h_out) {
    for (int k = 0; k < f->nd; k++) {
        cx_t acc = CX_ZERO;
        const double *row = &f->W[k * f->np];
        for (int j = 0; j < f->np; j++)
            acc += row[j] * h_ls[j];
        h_out[k] = acc;
    }
}

void lmmse_filter_free(LMMSEFilter *f) {
    free(f->W); f->W = NULL;
    f->np = f->nd = 0;
}
