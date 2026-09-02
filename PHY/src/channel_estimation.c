/* ================================================================
 *  channel_estimation.c
 *  LS channel estimation, interpolation, ZF/MMSE equalization
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "channel_estimation.h"
#include "dft_precode.h"
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

/* ── 임의 크기 N×N 복소 Gauss-Jordan 역행렬 (부분 피벗) ──────────────────
 * mimo.c/codebook_32port.c의 고정크기(3x3/4x4/N≤4) 버전과 동일 구조를
 * malloc 기반으로 일반화 — MMSE 채널추정의 파일럿개수(M) 역행렬에 사용.
 * 특이/근사특이 시 -1 반환. */
static int inv_dynamic(int n, const cx_t *A, cx_t *Ainv) {
    cx_t *aug = (cx_t *)malloc(n * 2 * n * sizeof(cx_t));
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) aug[r * 2 * n + c] = A[r * n + c];
        for (int c = 0; c < n; c++) aug[r * 2 * n + n + c] = (r == c) ? 1.0 : 0.0;
    }
    for (int col = 0; col < n; col++) {
        int    pivot_row = col;
        double max_abs   = cabs(aug[col * 2 * n + col]);
        for (int r = col + 1; r < n; r++) {
            double v = cabs(aug[r * 2 * n + col]);
            if (v > max_abs) { max_abs = v; pivot_row = r; }
        }
        if (max_abs < 1e-14) { free(aug); return -1; }
        if (pivot_row != col)
            for (int c = 0; c < 2 * n; c++) {
                cx_t tmp = aug[col * 2 * n + c];
                aug[col * 2 * n + c] = aug[pivot_row * 2 * n + c];
                aug[pivot_row * 2 * n + c] = tmp;
            }
        cx_t piv_inv = 1.0 / aug[col * 2 * n + col];
        for (int c = 0; c < 2 * n; c++) aug[col * 2 * n + c] *= piv_inv;
        for (int r = 0; r < n; r++) {
            if (r == col) continue;
            cx_t factor = aug[r * 2 * n + col];
            for (int c = 0; c < 2 * n; c++) aug[r * 2 * n + c] -= factor * aug[col * 2 * n + c];
        }
    }
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            Ainv[r * n + c] = aug[r * 2 * n + n + c];
    free(aug);
    return 0;
}

/* R·(R+N0·I)⁻¹를 W_out(M×M, row-major)에 채운다 — mmse_build_filter()와
 * mmse_channel_estimate() 둘 다 이 헬퍼로 공유. */
static void mmse_build_filter_ex(const int *pilot_pos, int M,
                                  double delay_spread_rms_ns, double scs_hz, double N0,
                                  cx_t *W_out, int *singular_out) {
    double tau_rms = delay_spread_rms_ns * 1e-9;

    cx_t *R = (cx_t *)malloc((size_t)M * M * sizeof(cx_t));
    cx_t *A = (cx_t *)malloc((size_t)M * M * sizeof(cx_t));
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < M; j++) {
            double df = (double)(pilot_pos[i] - pilot_pos[j]) * scs_hz;
            double x  = 2.0 * PHY_PI * df * tau_rms;
            double mag2 = 1.0 + x * x;
            cx_t rho = CX_MAKE(1.0 / mag2, -x / mag2);   /* 1/(1+jx) */
            R[i * M + j] = rho;
            A[i * M + j] = rho + ((i == j) ? (cx_t)N0 : 0.0);
        }
    }

    cx_t *Ainv = (cx_t *)malloc((size_t)M * M * sizeof(cx_t));
    if (inv_dynamic(M, A, Ainv) < 0) {
        *singular_out = 1;
        free(R); free(A); free(Ainv);
        return;
    }
    *singular_out = 0;

    /* W = R * Ainv */
    for (int i = 0; i < M; i++)
        for (int j = 0; j < M; j++) {
            cx_t s = 0.0;
            for (int k = 0; k < M; k++) s += R[i * M + k] * Ainv[k * M + j];
            W_out[i * M + j] = s;
        }

    free(R); free(A); free(Ainv);
}

void mmse_build_filter(const int *pilot_pos, int num_pilots,
                        double delay_spread_rms_ns, double scs_hz, double N0,
                        cx_t *W_out) {
    if (num_pilots <= 0) return;
    int singular = 0;
    mmse_build_filter_ex(pilot_pos, num_pilots, delay_spread_rms_ns, scs_hz, N0, W_out, &singular);
    if (singular) {
        /* 특이 행렬 방어: 항등행렬(LS 그대로 통과) */
        for (int i = 0; i < num_pilots; i++)
            for (int j = 0; j < num_pilots; j++)
                W_out[i * num_pilots + j] = (i == j) ? 1.0 : 0.0;
    }
}

void mmse_apply_filter(const cx_t *W, int num_pilots, const cx_t *h_ls, cx_t *h_mmse_out) {
    for (int i = 0; i < num_pilots; i++) {
        cx_t s = 0.0;
        for (int j = 0; j < num_pilots; j++) s += W[i * num_pilots + j] * h_ls[j];
        h_mmse_out[i] = s;
    }
}

void mmse_build_avg_filter(const int *pilot_pos, int num_pilots,
                            const int *target_pos, int num_target,
                            double delay_spread_rms_ns, double scs_hz, double N0,
                            cx_t *w_avg_out) {
    int M = num_pilots;
    if (M <= 0 || num_target <= 0) return;
    double tau_rms = delay_spread_rms_ns * 1e-9;

    /* A = R_pilot,pilot + N0·I (파일럿-파일럿 상관, mmse_build_filter와 동일) */
    cx_t *A = (cx_t *)malloc((size_t)M * M * sizeof(cx_t));
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < M; j++) {
            double df = (double)(pilot_pos[i] - pilot_pos[j]) * scs_hz;
            double x  = 2.0 * PHY_PI * df * tau_rms;
            double mag2 = 1.0 + x * x;
            cx_t rho = CX_MAKE(1.0 / mag2, -x / mag2);
            A[i * M + j] = rho + ((i == j) ? (cx_t)N0 : 0.0);
        }
    }
    cx_t *Ainv = (cx_t *)malloc((size_t)M * M * sizeof(cx_t));
    if (inv_dynamic(M, A, Ainv) < 0) {
        /* 특이 행렬 방어: 단순평균(스무딩 없음) */
        for (int i = 0; i < M; i++) w_avg_out[i] = 1.0 / (double)M;
        free(A); free(Ainv);
        return;
    }

    /* r_yz[p] = E[Y·H*(f_p)] = (1/D)·Σ_d ρ(f_target[d] − f_pilot[p])
     * Y = (1/D)·Σ_d H(f_target[d])가 타깃(예: 서브밴드 data RE 평균) */
    cx_t *r_yz = (cx_t *)malloc((size_t)M * sizeof(cx_t));
    for (int p = 0; p < M; p++) {
        cx_t s = CX_ZERO;
        for (int d = 0; d < num_target; d++) {
            double df = (double)(target_pos[d] - pilot_pos[p]) * scs_hz;
            double x  = 2.0 * PHY_PI * df * tau_rms;
            double mag2 = 1.0 + x * x;
            s += CX_MAKE(1.0 / mag2, -x / mag2);
        }
        r_yz[p] = s / (double)num_target;
    }

    /* w_avg = r_yz^T · Ainv  (1×M 행벡터), 이후 Ŷ = w_avg · h_ls (내적 한 번) */
    for (int j = 0; j < M; j++) {
        cx_t s = CX_ZERO;
        for (int p = 0; p < M; p++) s += r_yz[p] * Ainv[p * M + j];
        w_avg_out[j] = s;
    }

    free(A); free(Ainv); free(r_yz);
}

void mmse_channel_estimate(const cx_t *h_ls, const int *pilot_pos, int num_pilots,
                            double delay_spread_rms_ns, double scs_hz, double N0,
                            cx_t *h_mmse_out) {
    int M = num_pilots;
    if (M <= 0) return;

    cx_t *W = (cx_t *)malloc((size_t)M * M * sizeof(cx_t));
    mmse_build_filter(pilot_pos, M, delay_spread_rms_ns, scs_hz, N0, W);
    mmse_apply_filter(W, M, h_ls, h_mmse_out);
    free(W);
}

void dft_channel_estimate(const cx_t *h_freq_in, int num_sc, int num_taps,
                           cx_t *h_freq_out) {
    if (num_taps > num_sc) num_taps = num_sc;
    if (num_taps < 1) num_taps = 1;

    cx_t *h_time = (cx_t *)malloc((size_t)num_sc * sizeof(cx_t));
    idft_precode(h_freq_in, num_sc, h_time);
    for (int i = num_taps; i < num_sc; i++) h_time[i] = CX_ZERO;
    dft_precode(h_time, num_sc, h_freq_out);
    free(h_time);
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
