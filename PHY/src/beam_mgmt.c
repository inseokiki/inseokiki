/* ================================================================
 *  beam_mgmt.c
 *  빔 관리(Beam Management) — SSB/CSI-RS 기반 P1 절차(Tx 빔 스위핑)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "beam_mgmt.h"
#include "codebook_32port.h"
#include <math.h>

void beam_mgmt_true_channel(double l_true, double m_true, double n_true, cx_t H_true[32]) {
    /* v(l,m)[n1*4+k2] = exp(j2*pi*l*n1/(O1*N1)) * exp(j2*pi*m*k2/(O2*N2))
     * codebook_32port.h 공식을 정수 격자(l,m)가 아닌 실수값으로 일반화
     * (표준 array-manifold steering vector — 격자 제약은 코드북 양자화의
     * 산물이지 물리적 채널 자체의 제약이 아님). */
    cx_t v[16];
    for (int n1 = 0; n1 < 4; n1++) {
        double ph_h = 2.0 * M_PI * l_true * n1 / (CB32_O1 * CB32_N1);
        for (int k2 = 0; k2 < 4; k2++) {
            double ph_v = 2.0 * M_PI * m_true * k2 / (CB32_O2 * CB32_N2);
            v[n1 * 4 + k2] = CX_MAKE(cos(ph_h + ph_v), sin(ph_h + ph_v));
        }
    }
    cx_t phi_n = CX_MAKE(cos(M_PI * n_true / 2.0), sin(M_PI * n_true / 2.0));
    for (int i = 0; i < 16; i++) {
        H_true[i]      = v[i];
        H_true[16 + i] = phi_n * v[i];
    }
    /* |v[i]|=1이므로 ||H_true||²=32 — 단위벡터로 정규화 */
    double norm2 = 0.0;
    for (int i = 0; i < 32; i++) norm2 += CX_NORM(H_true[i]);
    double scale = 1.0 / sqrt(norm2);
    for (int i = 0; i < 32; i++) H_true[i] = H_true[i] * scale;
}

/* H_true^H · W (내적, 32차원) */
static cx_t inner_prod32(const cx_t H_true[32], const cx_t W[32]) {
    cx_t s = CX_ZERO;
    for (int i = 0; i < 32; i++) s += conj(H_true[i]) * W[i];
    return s;
}

void beam_mgmt_p1_sweep(const cx_t H_true[32], double N0, int num_rep,
                         int *sel_i1_1, int *sel_i1_2, int *sel_i2, double *sel_gain) {
    double sigma = sqrt(N0 / 2.0);
    double best_rsrp = -1.0;
    int best_l = 0, best_m = 0, best_n = 0;

    for (int l = 0; l < BM_CAND_L; l++) {
        for (int m = 0; m < BM_CAND_M; m++) {
            for (int n = 0; n < BM_CAND_N2; n++) {
                cx_t W[32];
                codebook_type1_sp_32port_rank1(l, m, n, W);
                cx_t h_eff = inner_prod32(H_true, W);

                double rsrp_sum = 0.0;
                for (int rep = 0; rep < num_rep; rep++) {
                    cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                    cx_t y = h_eff + noise;
                    rsrp_sum += CX_NORM(y);
                }
                double rsrp_avg = rsrp_sum / num_rep;
                if (rsrp_avg > best_rsrp) {
                    best_rsrp = rsrp_avg;
                    best_l = l; best_m = m; best_n = n;
                }
            }
        }
    }

    *sel_i1_1 = best_l;
    *sel_i1_2 = best_m;
    *sel_i2  = best_n;
    cx_t W_sel[32];
    codebook_type1_sp_32port_rank1(best_l, best_m, best_n, W_sel);
    *sel_gain = CX_NORM(inner_prod32(H_true, W_sel));
}

void beam_mgmt_genie_best(const cx_t H_true[32], int *best_i1_1, int *best_i1_2, int *best_i2, double *best_gain) {
    double best = -1.0;
    int bl = 0, bm = 0, bn = 0;
    for (int l = 0; l < BM_CAND_L; l++) {
        for (int m = 0; m < BM_CAND_M; m++) {
            for (int n = 0; n < BM_CAND_N2; n++) {
                cx_t W[32];
                codebook_type1_sp_32port_rank1(l, m, n, W);
                double g = CX_NORM(inner_prod32(H_true, W));
                if (g > best) { best = g; bl = l; bm = m; bn = n; }
            }
        }
    }
    *best_i1_1 = bl;
    *best_i1_2 = bm;
    *best_i2  = bn;
    *best_gain = best;
}
