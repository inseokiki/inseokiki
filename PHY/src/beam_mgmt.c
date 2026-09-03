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

void beam_mgmt_ue_steer(double idx, cx_t w[BM_UE_N]) {
    double scale = 1.0 / sqrt((double)BM_UE_N);
    for (int u = 0; u < BM_UE_N; u++) {
        double ph = 2.0 * M_PI * idx * u / (BM_UE_O * BM_UE_N);
        w[u] = CX_MAKE(cos(ph), sin(ph)) * scale;
    }
}

/* 배열 매니폴드(raw, 원소당 크기 1 -- beam_mgmt_ue_steer()의 결합
 * 가중치용 1/sqrt(N_UE) 정규화와는 다른 물리량): 실제 채널은 각 UE
 * 소자가 (경로손실을 별개로 하면) 독립적으로 같은 신호 전력을 받고
 * 위상만 소자 위치/AoA에 따라 다른 것이지, N_UE소자에 "전력이 나뉘어
 * 실리는" 게 아니다 -- combining 가중치와 물리적 채널을 같은 함수로
 * 만들면 정합 필터링 시 배열이득(최대 N_UE배)이 사라지는 정규화
 * 이중적용 버그가 생긴다(2026-09-03, 첫 구현에서 발견·수정). */
static void ue_array_manifold(double idx, cx_t a[BM_UE_N]) {
    for (int u = 0; u < BM_UE_N; u++) {
        double ph = 2.0 * M_PI * idx * u / (BM_UE_O * BM_UE_N);
        a[u] = CX_MAKE(cos(ph), sin(ph));   /* |a[u]|=1, 결합가중치 정규화 없음 */
    }
}

void beam_mgmt_true_channel_mimo(const cx_t H_true[32], double r_true,
                                  cx_t H_full[BM_UE_N][32]) {
    cx_t a_true[BM_UE_N];
    ue_array_manifold(r_true, a_true);
    for (int u = 0; u < BM_UE_N; u++)
        for (int g = 0; g < 32; g++)
            H_full[u][g] = a_true[u] * H_true[g];
}

/* H_full[u][:]^H · W_tx (32차원 내적, 소자 u) -- inner_prod32()와 동일
 * conj(채널)·프리코더 관례. */
static cx_t inner_prod32_row(const cx_t H_full[BM_UE_N][32], int u, const cx_t W[32]) {
    cx_t s = CX_ZERO;
    for (int g = 0; g < 32; g++) s += conj(H_full[u][g]) * W[g];
    return s;
}

void beam_mgmt_p3_sweep(const cx_t H_full[BM_UE_N][32], const cx_t W_tx[32],
                         double N0, int num_rep, int *sel_ue_idx, double *sel_gain) {
    double sigma = sqrt(N0 / 2.0);
    double best_rsrp = -1.0;
    int best_idx = 0;

    cx_t hg[BM_UE_N];
    for (int u = 0; u < BM_UE_N; u++) hg[u] = inner_prod32_row(H_full, u, W_tx);

    for (int c = 0; c < BM_UE_CAND; c++) {
        cx_t w_ue[BM_UE_N];
        beam_mgmt_ue_steer((double)c, w_ue);

        cx_t h_eff = CX_ZERO;
        for (int u = 0; u < BM_UE_N; u++) h_eff += conj(w_ue[u]) * hg[u];

        double rsrp_sum = 0.0;
        for (int rep = 0; rep < num_rep; rep++) {
            cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
            cx_t y = h_eff + noise;
            rsrp_sum += CX_NORM(y);
        }
        double rsrp_avg = rsrp_sum / num_rep;
        if (rsrp_avg > best_rsrp) { best_rsrp = rsrp_avg; best_idx = c; }
    }

    *sel_ue_idx = best_idx;
    cx_t w_sel[BM_UE_N];
    beam_mgmt_ue_steer((double)best_idx, w_sel);
    cx_t h_eff_sel = CX_ZERO;
    for (int u = 0; u < BM_UE_N; u++) h_eff_sel += conj(w_sel[u]) * hg[u];
    *sel_gain = CX_NORM(h_eff_sel);
}

void beam_mgmt_p2_effective_channel(const cx_t H_full[BM_UE_N][32],
                                     const cx_t w_ue[BM_UE_N], cx_t h_eff[32]) {
    for (int g = 0; g < 32; g++) {
        cx_t s = CX_ZERO;
        for (int u = 0; u < BM_UE_N; u++) s += conj(w_ue[u]) * H_full[u][g];
        h_eff[g] = s;
    }
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
