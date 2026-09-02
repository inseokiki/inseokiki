/* ================================================================
 *  mumimo.c
 *  MU-MIMO 하향링크 — Zero-Forcing Beamforming (ZF-BF)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "mumimo.h"
#include <math.h>

void mumimo_channel_draw(cx_t H[MUMIMO_K][MUMIMO_NT]) {
    double inv_sq2 = 1.0 / sqrt(2.0);
    for (int k = 0; k < MUMIMO_K; k++)
        for (int t = 0; t < MUMIMO_NT; t++)
            H[k][t] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
}

/* 2x2 복소 역행렬 (폐형 공식 — K=2 고정이라 Gauss-Jordan 불필요) */
static void inv2x2_local(cx_t a, cx_t b, cx_t c, cx_t d, cx_t inv[2][2], int *singular) {
    cx_t det = a * d - b * c;
    if (cabs(det) < 1e-12) { *singular = 1; return; }
    *singular = 0;
    cx_t idet = 1.0 / det;
    inv[0][0] =  d * idet; inv[0][1] = -b * idet;
    inv[1][0] = -c * idet; inv[1][1] =  a * idet;
}

void mumimo_zf_precode(const cx_t H[MUMIMO_K][MUMIMO_NT], cx_t W[MUMIMO_NT][MUMIMO_K]) {
    /* A = H H^H (K×K Hermitian Gramian) */
    cx_t A[MUMIMO_K][MUMIMO_K];
    for (int i = 0; i < MUMIMO_K; i++)
        for (int j = 0; j < MUMIMO_K; j++) {
            cx_t s = CX_ZERO;
            for (int t = 0; t < MUMIMO_NT; t++) s += H[i][t] * conj(H[j][t]);
            A[i][j] = s;
        }

    cx_t Ainv[2][2];
    int singular = 0;
    inv2x2_local(A[0][0], A[0][1], A[1][0], A[1][1], Ainv, &singular);
    if (singular) {
        for (int t = 0; t < MUMIMO_NT; t++)
            for (int k = 0; k < MUMIMO_K; k++) W[t][k] = CX_ZERO;
        return;
    }

    /* W_raw = H^H · Ainv  (Nt×K) — 우측 유사역행렬 H^+ = H^H(HH^H)^-1 */
    cx_t Wraw[MUMIMO_NT][MUMIMO_K];
    for (int t = 0; t < MUMIMO_NT; t++)
        for (int k = 0; k < MUMIMO_K; k++) {
            cx_t s = CX_ZERO;
            for (int i = 0; i < MUMIMO_K; i++) s += conj(H[i][t]) * Ainv[i][k];
            Wraw[t][k] = s;
        }

    /* 열별 정규화: ||W[:,k]||² = 1/K (실수 양수 스케일 — 간섭제거 성질 보존) */
    for (int k = 0; k < MUMIMO_K; k++) {
        double norm2 = 0.0;
        for (int t = 0; t < MUMIMO_NT; t++) norm2 += CX_NORM(Wraw[t][k]);
        double norm  = sqrt(norm2);
        double scale = (norm > 1e-9) ? sqrt(1.0 / MUMIMO_K) / norm : 0.0;
        for (int t = 0; t < MUMIMO_NT; t++) W[t][k] = Wraw[t][k] * scale;
    }
}
