/* ================================================================
 *  ul_eigen_bf.c
 *  UL SIMO 수신 빔포밍 — 공간 공분산 EVD(전력반복법) 기반, 비-코드북
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "ul_eigen_bf.h"
#include <math.h>

void ul_eigen_channel_draw(cx_t h[UL_EIGEN_NRX]) {
    double inv_sq2 = 1.0 / sqrt(2.0);
    for (int r = 0; r < UL_EIGEN_NRX; r++)
        h[r] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
}

void ul_eigen_beamform(const cx_t h_est_obs[][UL_EIGEN_NRX], int num_obs, cx_t w_out[UL_EIGEN_NRX]) {
    /* 표본 공간공분산 R = (1/num_obs) * sum_m h_m h_m^H (Hermitian PSD) */
    cx_t R[UL_EIGEN_NRX][UL_EIGEN_NRX];
    for (int i = 0; i < UL_EIGEN_NRX; i++)
        for (int j = 0; j < UL_EIGEN_NRX; j++) R[i][j] = CX_ZERO;
    for (int m = 0; m < num_obs; m++) {
        for (int i = 0; i < UL_EIGEN_NRX; i++)
            for (int j = 0; j < UL_EIGEN_NRX; j++)
                R[i][j] += h_est_obs[m][i] * conj(h_est_obs[m][j]);
    }
    double inv_m = 1.0 / num_obs;
    for (int i = 0; i < UL_EIGEN_NRX; i++)
        for (int j = 0; j < UL_EIGEN_NRX; j++) R[i][j] *= inv_m;

    /* 전력반복법: w <- R w / ||R w|| 를 반복해 지배적 고유벡터로 수렴 */
    cx_t w[UL_EIGEN_NRX];
    for (int i = 0; i < UL_EIGEN_NRX; i++)
        w[i] = CX_MAKE(randn(), randn());
    double norm0 = 0.0;
    for (int i = 0; i < UL_EIGEN_NRX; i++) norm0 += CX_NORM(w[i]);
    double scale0 = 1.0 / sqrt(norm0);
    for (int i = 0; i < UL_EIGEN_NRX; i++) w[i] = w[i] * scale0;

    const int ITERS = 50;
    for (int it = 0; it < ITERS; it++) {
        cx_t wn[UL_EIGEN_NRX];
        for (int i = 0; i < UL_EIGEN_NRX; i++) {
            cx_t s = CX_ZERO;
            for (int j = 0; j < UL_EIGEN_NRX; j++) s += R[i][j] * w[j];
            wn[i] = s;
        }
        double norm2 = 0.0;
        for (int i = 0; i < UL_EIGEN_NRX; i++) norm2 += CX_NORM(wn[i]);
        double norm = sqrt(norm2);
        if (norm < 1e-12) break;   /* R이 사실상 0(관측 없음 등) — 방어적 종료 */
        double scale = 1.0 / norm;
        for (int i = 0; i < UL_EIGEN_NRX; i++) w[i] = wn[i] * scale;
    }

    for (int i = 0; i < UL_EIGEN_NRX; i++) w_out[i] = w[i];
}

/* 2×2 Hermitian 행렬 [[a00,a01];[conj(a01),a11]](a00,a11 실수)의 닫힌
 * 형식 고유분해 — 2×2라 반복법 없이 직접 공식으로 풀 수 있다.
 * lam1>=lam2, v1/v2는 직교정규 고유벡터. */
static void herm2x2_eig(double a00, cx_t a01, double a11,
                         double *lam1, double *lam2, cx_t v1[2], cx_t v2[2]) {
    double bm = cabs(a01);
    double half_tr = (a00 + a11) / 2.0;
    double half_diff = (a00 - a11) / 2.0;
    double disc = sqrt(half_diff * half_diff + bm * bm);
    double l1 = half_tr + disc, l2 = half_tr - disc;

    if (bm > 1e-12) {
        cx_t u1[2] = { a01, CX_MAKE(l1 - a00, 0.0) };
        double n1 = sqrt(CX_NORM(u1[0]) + CX_NORM(u1[1]));
        v1[0] = u1[0] / n1; v1[1] = u1[1] / n1;
        cx_t u2[2] = { a01, CX_MAKE(l2 - a00, 0.0) };
        double n2 = sqrt(CX_NORM(u2[0]) + CX_NORM(u2[1]));
        v2[0] = u2[0] / n2; v2[1] = u2[1] / n2;
    } else {
        /* 이미 대각(비대각 원소 0에 근접) — 표준기저가 고유벡터 */
        if (a00 >= a11) { v1[0]=CX_MAKE(1,0); v1[1]=CX_ZERO; v2[0]=CX_ZERO; v2[1]=CX_MAKE(1,0); }
        else            { v1[0]=CX_ZERO; v1[1]=CX_MAKE(1,0); v2[0]=CX_MAKE(1,0); v2[1]=CX_ZERO; }
    }
    *lam1 = l1; *lam2 = l2;
}

void ul_eigen_svd_genie(const cx_t H[UL_EIGEN_NRX][2], cx_t U[UL_EIGEN_NRX][2], double sigma[2]) {
    /* Gram 행렬 G = H^H H (2x2 Hermitian) */
    cx_t g01 = CX_ZERO;
    double g00 = 0.0, g11 = 0.0;
    for (int r = 0; r < UL_EIGEN_NRX; r++) {
        g00 += CX_NORM(H[r][0]);
        g11 += CX_NORM(H[r][1]);
        g01 += conj(H[r][0]) * H[r][1];
    }
    double lam1, lam2; cx_t v1[2], v2[2];
    herm2x2_eig(g00, g01, g11, &lam1, &lam2, v1, v2);
    double sigma1 = sqrt(lam1 > 0.0 ? lam1 : 0.0);
    double sigma2 = sqrt(lam2 > 0.0 ? lam2 : 0.0);
    sigma[0] = sigma1; sigma[1] = sigma2;

    /* 좌특이벡터: U_k = H v_k / sigma_k */
    for (int r = 0; r < UL_EIGEN_NRX; r++) {
        cx_t hv1 = H[r][0] * v1[0] + H[r][1] * v1[1];
        cx_t hv2 = H[r][0] * v2[0] + H[r][1] * v2[1];
        U[r][0] = (sigma1 > 1e-9) ? hv1 / sigma1 : CX_ZERO;
        U[r][1] = (sigma2 > 1e-9) ? hv2 / sigma2 : CX_ZERO;
    }
}

void ul_eigen_orthogonalize2(const cx_t w1[UL_EIGEN_NRX], cx_t w2_inout[UL_EIGEN_NRX]) {
    cx_t proj = CX_ZERO;
    for (int r = 0; r < UL_EIGEN_NRX; r++) proj += conj(w1[r]) * w2_inout[r];
    for (int r = 0; r < UL_EIGEN_NRX; r++) w2_inout[r] -= proj * w1[r];
    double n2 = 0.0;
    for (int r = 0; r < UL_EIGEN_NRX; r++) n2 += CX_NORM(w2_inout[r]);
    double n = sqrt(n2);
    if (n > 1e-9) {
        double inv = 1.0 / n;
        for (int r = 0; r < UL_EIGEN_NRX; r++) w2_inout[r] *= inv;
    }
}

void ul_eigen_svd_genie4(const cx_t H[UL_EIGEN_NRX][4], cx_t U[UL_EIGEN_NRX][4], double sigma[4]) {
    /* Gram 행렬 G = H^H H (4x4 Hermitian) */
    cx_t G[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            cx_t s = CX_ZERO;
            for (int r = 0; r < UL_EIGEN_NRX; r++) s += conj(H[r][i]) * H[r][j];
            G[i][j] = s;
        }
    double eigval[4]; cx_t eigvec[4][4];
    herm4x4_eig(G, eigval, eigvec);

    for (int k = 0; k < 4; k++) {
        double lam = eigval[k] > 0.0 ? eigval[k] : 0.0;
        double sg = sqrt(lam);
        sigma[k] = sg;
        for (int r = 0; r < UL_EIGEN_NRX; r++) {
            cx_t hv = CX_ZERO;
            for (int c = 0; c < 4; c++) hv += H[r][c] * eigvec[c][k];
            U[r][k] = (sg > 1e-9) ? hv / sg : CX_ZERO;
        }
    }
}

void ul_eigen_orthogonalize4(cx_t w[4][UL_EIGEN_NRX]) {
    for (int k = 1; k < 4; k++) {
        for (int j = 0; j < k; j++) {
            cx_t proj = CX_ZERO;
            for (int r = 0; r < UL_EIGEN_NRX; r++) proj += conj(w[j][r]) * w[k][r];
            for (int r = 0; r < UL_EIGEN_NRX; r++) w[k][r] -= proj * w[j][r];
        }
        double n2 = 0.0;
        for (int r = 0; r < UL_EIGEN_NRX; r++) n2 += CX_NORM(w[k][r]);
        double n = sqrt(n2);
        if (n > 1e-9) {
            double inv = 1.0 / n;
            for (int r = 0; r < UL_EIGEN_NRX; r++) w[k][r] *= inv;
        }
    }
}
