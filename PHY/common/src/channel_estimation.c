#include "channel_estimation.h"
#include <math.h>
#include <stdlib.h>

/* LS channel estimate: h_out[k] = rx_pilots[k] / tx_pilots[k] */
void ls_estimate(const cx_t *rx_pilots, const cx_t *tx_pilots, int num_pilots, cx_t *h_out) {
    for (int k = 0; k < num_pilots; k++)
        h_out[k] = rx_pilots[k] / tx_pilots[k];
}

/* Linear interpolation of channel across all active subcarriers.
 * Pilot positions must be sorted in ascending order. */
void interpolate_channel(const cx_t *h_pilots, int num_pilots,
                         const int *pilot_pos, int num_active_sc,
                         cx_t *h_out) {
    if (num_pilots == 0) {
        for (int sc = 0; sc < num_active_sc; sc++) h_out[sc] = CX_ZERO;
        return;
    }

    for (int sc = 0; sc < num_active_sc; sc++) {

        /* Binary search: find the first pilot at or to the right of this subcarrier */
        int lo = 0, hi = num_pilots;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (pilot_pos[mid] < sc) lo = mid + 1;
            else                     hi = mid;
        }
        int right_idx = lo;   /* first pilot with position >= sc */

        if (right_idx == num_pilots) {
            /* Past the last pilot: hold the last value */
            h_out[sc] = h_pilots[num_pilots - 1];

        } else if (pilot_pos[right_idx] == sc) {
            /* Exactly on a pilot */
            h_out[sc] = h_pilots[right_idx];

        } else if (right_idx == 0) {
            /* Before the first pilot: hold the first value */
            h_out[sc] = h_pilots[0];

        } else {
            /* Between two pilots: linearly interpolate */
            int    left_idx  = right_idx - 1;
            int    left_pos  = pilot_pos[left_idx];
            int    right_pos = pilot_pos[right_idx];
            double frac      = (double)(sc - left_pos) / (right_pos - left_pos);
            h_out[sc] = h_pilots[left_idx] * (1.0 - frac) + h_pilots[right_idx] * frac;
        }
    }
}

/* ZF equaliser: eq[k] = rx[k] / h[k]  (skips near-zero channels) */
void zf_equalize(const cx_t *rx, const cx_t *h, int num_sc, cx_t *eq) {
    for (int k = 0; k < num_sc; k++) {
        double ch_power = CX_NORM(h[k]);
        eq[k] = (ch_power < 1e-10) ? rx[k] : rx[k] / h[k];
    }
}

/* MMSE equaliser: w[k] = h*[k] / (|h[k]|² + N0),  eq[k] = w[k] * rx[k]
 * Optionally writes per-SC bias  alpha[k] = |h[k]|² / (|h[k]|² + N0). */
void mmse_equalize(const cx_t *rx, const cx_t *h, int num_sc,
                   double N0, cx_t *eq, double *alpha_out) {
    for (int k = 0; k < num_sc; k++) {
        double ch_power = CX_NORM(h[k]);
        double denom    = ch_power + N0;
        cx_t   weight   = conj(h[k]) / denom;
        eq[k] = weight * rx[k];
        if (alpha_out) alpha_out[k] = ch_power / denom;
    }
}

/* ── LMMSE channel estimation ─────────────────────────────────────────────
 *
 * Precomputed filter:  W = R_dp · (R_pp + N0·I)⁻¹
 *
 * Channel frequency-correlation model (exponential PDP approximation):
 *   R(Δk) = exp(−decay_per_sc · |Δk|)
 *   where decay_per_sc = 2π · τ_rms [s] · Δf [Hz]
 *
 * Usage:
 *   lmmse_filter_build()  — once per SNR point (builds W)
 *   lmmse_filter_apply()  — once per HARQ round per Rx antenna
 *   lmmse_filter_free()   — once per SNR point (frees W)
 * ─────────────────────────────────────────────────────────────────────── */

/* Solve  A · X = B  in-place using Gaussian elimination with partial pivoting.
 * A: n×n (row-major), B: n×nrhs (row-major).
 * On return B is overwritten with the solution X. */
static void gauss_solve(double *A, double *B, int n, int nrhs) {

    /* Forward elimination */
    for (int col = 0; col < n; col++) {

        /* Find the row with the largest absolute value in this column */
        int    pivot_row = col;
        double max_abs   = fabs(A[col*n + col]);
        for (int row = col + 1; row < n; row++) {
            if (fabs(A[row*n + col]) > max_abs) {
                max_abs   = fabs(A[row*n + col]);
                pivot_row = row;
            }
        }

        /* Swap rows col ↔ pivot_row in both A and B */
        if (pivot_row != col) {
            for (int j = 0; j < n; j++) {
                double tmp             = A[col*n      + j];
                A[col*n      + j]      = A[pivot_row*n + j];
                A[pivot_row*n + j]     = tmp;
            }
            for (int j = 0; j < nrhs; j++) {
                double tmp              = B[col*nrhs      + j];
                B[col*nrhs      + j]    = B[pivot_row*nrhs + j];
                B[pivot_row*nrhs + j]   = tmp;
            }
        }

        double pivot_val = A[col*n + col];
        if (fabs(pivot_val) < 1e-15) continue;   /* near-singular column: skip */

        /* Zero out all entries below the pivot */
        for (int row = col + 1; row < n; row++) {
            double factor = A[row*n + col] / pivot_val;
            for (int j = col; j < n;    j++) A[row*n    + j] -= factor * A[col*n    + j];
            for (int j = 0;   j < nrhs; j++) B[row*nrhs + j] -= factor * B[col*nrhs + j];
        }
    }

    /* Back substitution */
    for (int row = n - 1; row >= 0; row--) {
        if (fabs(A[row*n + row]) < 1e-15) continue;
        for (int j = 0; j < nrhs; j++) {
            for (int sub = row + 1; sub < n; sub++)
                B[row*nrhs + j] -= A[row*n + sub] * B[sub*nrhs + j];
            B[row*nrhs + j] /= A[row*n + row];
        }
    }
}

void lmmse_filter_build(LMMSEFilter *f,
                        const int *pilot_pos, int num_pilots,
                        const int *data_pos,  int num_data,
                        double N0, double scs_hz, double tau_rms_ns) {
    f->np = num_pilots;
    f->nd = num_data;
    f->W  = (double *)malloc((size_t)num_data * num_pilots * sizeof(double));

    /* How fast the channel decorrelates across subcarriers.
     * A larger delay spread → faster decorrelation → smaller decay_per_sc → larger filter bandwidth. */
    double tau_rms_s   = tau_rms_ns * 1e-9;
    double decay_per_sc = 2.0 * M_PI * tau_rms_s * scs_hz;

    /* Step 1: Build R_pp + N0·I  (num_pilots × num_pilots)
     *   R_pp[i,j] = correlation between pilot i and pilot j */
    double *R_pp_reg = (double *)malloc((size_t)num_pilots * num_pilots * sizeof(double));
    for (int i = 0; i < num_pilots; i++) {
        for (int j = 0; j < num_pilots; j++) {
            double sc_gap = fabs((double)(pilot_pos[i] - pilot_pos[j]));
            double corr   = (decay_per_sc > 1e-12) ? exp(-decay_per_sc * sc_gap) : 1.0;
            R_pp_reg[i * num_pilots + j] = corr;
        }
        R_pp_reg[i * num_pilots + i] += N0;   /* noise regularisation on diagonal */
    }

    /* Step 2: Build R_dp^T  (num_pilots × num_data)
     *   Entry [pilot_i, data_k] = correlation between data SC data_k and pilot pilot_i.
     *   Stored transposed so it fits directly as the right-hand-side of the linear solve. */
    double *cross_corr_T = (double *)malloc((size_t)num_pilots * num_data * sizeof(double));
    for (int pilot_i = 0; pilot_i < num_pilots; pilot_i++) {
        for (int data_k = 0; data_k < num_data; data_k++) {
            double sc_gap = fabs((double)(data_pos[data_k] - pilot_pos[pilot_i]));
            double corr   = (decay_per_sc > 1e-12) ? exp(-decay_per_sc * sc_gap) : 1.0;
            cross_corr_T[pilot_i * num_data + data_k] = corr;
        }
    }

    /* Step 3: Solve  R_pp_reg · W^T = R_dp^T
     *   After the solve, cross_corr_T holds W^T  (num_pilots × num_data). */
    gauss_solve(R_pp_reg, cross_corr_T, num_pilots, num_data);

    /* Step 4: Transpose to get W  (num_data × num_pilots) stored in f->W */
    for (int data_k = 0; data_k < num_data; data_k++) {
        for (int pilot_i = 0; pilot_i < num_pilots; pilot_i++) {
            f->W[data_k * num_pilots + pilot_i] = cross_corr_T[pilot_i * num_data + data_k];
        }
    }

    free(R_pp_reg);
    free(cross_corr_T);
}

/* Apply the precomputed LMMSE filter: h_out[k] = Σ_j W[k,j] · h_ls[j] */
void lmmse_filter_apply(const LMMSEFilter *f, const cx_t *h_ls, cx_t *h_out) {
    for (int data_k = 0; data_k < f->nd; data_k++) {
        cx_t          estimated_h = CX_ZERO;
        const double *filter_row  = &f->W[data_k * f->np];
        for (int pilot_j = 0; pilot_j < f->np; pilot_j++)
            estimated_h += filter_row[pilot_j] * h_ls[pilot_j];
        h_out[data_k] = estimated_h;
    }
}

void lmmse_filter_free(LMMSEFilter *f) {
    free(f->W);
    f->W  = NULL;
    f->np = 0;
    f->nd = 0;
}
