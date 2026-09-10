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

/* K×K 복소 역행렬 — Gauss-Jordan 소거, 부분 피벗팅(열마다 절댓값 최대
 * 원소를 피벗으로 선택, 수치 안정성 확보). MUMIMO_K가 2 고정이던 이전
 * 버전의 폐형 2×2 공식을 일반 K로 대체(tasks/todo.md MU-MIMO K>2 확장). */
static void invKxK_local(const cx_t A[MUMIMO_K][MUMIMO_K], cx_t inv[MUMIMO_K][MUMIMO_K], int *singular) {
    cx_t M[MUMIMO_K][2 * MUMIMO_K];
    for (int i = 0; i < MUMIMO_K; i++) {
        for (int j = 0; j < MUMIMO_K; j++) M[i][j] = A[i][j];
        for (int j = 0; j < MUMIMO_K; j++) M[i][MUMIMO_K + j] = (i == j) ? CX_MAKE(1.0, 0.0) : CX_ZERO;
    }
    *singular = 0;
    for (int col = 0; col < MUMIMO_K; col++) {
        int piv = col;
        double best = cabs(M[col][col]);
        for (int r = col + 1; r < MUMIMO_K; r++) {
            double m = cabs(M[r][col]);
            if (m > best) { best = m; piv = r; }
        }
        if (best < 1e-12) { *singular = 1; return; }
        if (piv != col)
            for (int j = 0; j < 2 * MUMIMO_K; j++) {
                cx_t t = M[col][j]; M[col][j] = M[piv][j]; M[piv][j] = t;
            }
        cx_t pivval = M[col][col];
        for (int j = 0; j < 2 * MUMIMO_K; j++) M[col][j] /= pivval;
        for (int r = 0; r < MUMIMO_K; r++) {
            if (r == col) continue;
            cx_t factor = M[r][col];
            if (cabs(factor) < 1e-15) continue;
            for (int j = 0; j < 2 * MUMIMO_K; j++) M[r][j] -= factor * M[col][j];
        }
    }
    for (int i = 0; i < MUMIMO_K; i++)
        for (int j = 0; j < MUMIMO_K; j++) inv[i][j] = M[i][MUMIMO_K + j];
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

    cx_t Ainv[MUMIMO_K][MUMIMO_K];
    int singular = 0;
    invKxK_local(A, Ainv, &singular);
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
