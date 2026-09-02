/* ================================================================
 *  eigen_16port.c
 *  Eigen-beamforming (SVD 기반) 프리코딩 — 16 Tx 포트, 비-코드북
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "eigen_16port.h"
#include <math.h>

void eigen_bf_16port_svd(const cx_t H[4][16], double sigma[4], cx_t U[4][4], cx_t V[16][4]) {
    cx_t A[4][4];   /* A = H H^H (4x4 Hermitian PSD) */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            A[i][j] = 0.0;
            for (int t = 0; t < 16; t++) A[i][j] += H[i][t] * conj(H[j][t]);
        }

    double eigval[4];
    cx_t eigvec[4][4];
    herm4x4_eig(A, eigval, eigvec);   /* 2026-09-02: utils.h로 추출된 공용 4x4 Hermitian EVD
                                        * (ul_eigen_bf.c의 UL Eigen-BF 4-Tx 확장과 공유) */

    for (int i = 0; i < 4; i++) {
        double lam = eigval[i];
        if (lam < 0.0) lam = 0.0;   /* PSD 이론상 항상 >=0, 부동소수점 잡음 방어 */
        sigma[i] = sqrt(lam);

        for (int r = 0; r < 4; r++) U[r][i] = eigvec[r][i];

        if (sigma[i] > 1e-9) {
            for (int t = 0; t < 16; t++) {
                cx_t s = 0.0;
                for (int r = 0; r < 4; r++) s += conj(H[r][t]) * U[r][i];
                V[t][i] = s / sigma[i];
            }
        } else {
            for (int t = 0; t < 16; t++) V[t][i] = 0.0;
        }
    }
}

void eigen_bf_16port_select_rank(const double sigma[4], double N0, int *sel_rank) {
    double best_cap = -1.0;
    int best_r = 1;
    for (int r = 1; r <= 4; r++) {
        double cap = 0.0;
        for (int i = 0; i < r; i++) {
            double snr_i = sigma[i] * sigma[i] * (1.0 / r) / N0;
            cap += log2(1.0 + snr_i);
        }
        if (cap > best_cap) { best_cap = cap; best_r = r; }
    }
    *sel_rank = best_r;
}
