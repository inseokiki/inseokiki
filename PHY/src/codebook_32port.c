/* ================================================================
 *  codebook_32port.c
 *  5G NR Type I Single Panel Codebook — 32 CSI-RS ports (N1=4,N2=4)
 *
 *  구성: N1=4, N2=4, Ng=2 (XPOL), O1=4, O2=4, P=32 (2D 배열)
 *  표준: TS 38.214 Sec 5.2.2.2.1, Table 5.2.2.2.1-2/-3, -5/-6/-7/-8
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "codebook_32port.h"
#include <math.h>
#include <stdio.h>

#define N1   CB32_N1
#define N2   CB32_N2
#define O1   CB32_O1
#define O2   CB32_O2
#define P    CB32_P
#define HALF (N1 * N2)          /* 16 — 편파 1개당 포트 수 (rank 1/2 v 길이) */
#define TBLK ((N1 / 2) * N2)    /* 8  — rank 3/4 ṽ 길이(행 블록 하나 크기)    */

/* ── u_m: 수직(N2) 방향 DFT 성분, N2원소 ─────────────────────────────────
 * u_m[k2] = e^{j2π·m·k2/(O2·N2)},  k2 = 0..N2-1
 * ── v_{l,m}: 2D 빔 벡터, N1·N2원소 (index = n1*N2+k2) ───────────────────
 * v_{l,m}[n1,k2] = e^{j2π·l·n1/(O1·N1)} · u_m[k2]
 * ── ṽ_{l,m}: rank 3/4 전용 절반-길이 빔 벡터, (N1/2)·N2원소 ────────────
 * ṽ_{l,m}[n1,k2] = e^{j4π·l·n1/(O1·N1)} · u_m[k2],  n1 = 0..N1/2-1
 *   (l의 DFT 간격이 v의 2배 — Table 5.2.2.2.1-7/-8, P≥16 분기 전용)
 * ────────────────────────────────────────────────────────────────────── */
static void beam_vec(int l, int m, cx_t v[HALF]) {
    for (int n1 = 0; n1 < N1; n1++) {
        double ph1 = 2.0 * PHY_PI * l * n1 / (double)(N1 * O1);
        cx_t e1 = CX_MAKE(cos(ph1), sin(ph1));
        for (int k2 = 0; k2 < N2; k2++) {
            double ph2 = 2.0 * PHY_PI * m * k2 / (double)(N2 * O2);
            cx_t e2 = CX_MAKE(cos(ph2), sin(ph2));
            v[n1 * N2 + k2] = e1 * e2;
        }
    }
}

static void beam_vec_tilde(int l, int m, cx_t vt[TBLK]) {
    for (int n1 = 0; n1 < N1 / 2; n1++) {
        double ph1 = 4.0 * PHY_PI * l * n1 / (double)(N1 * O1);
        cx_t e1 = CX_MAKE(cos(ph1), sin(ph1));
        for (int k2 = 0; k2 < N2; k2++) {
            double ph2 = 2.0 * PHY_PI * m * k2 / (double)(N2 * O2);
            cx_t e2 = CX_MAKE(cos(ph2), sin(ph2));
            vt[n1 * N2 + k2] = e1 * e2;
        }
    }
}

/* φ_n = e^{jπn/2} (co-phasing), θ_p = e^{jπp/4} (rank 3/4 전용) */
static cx_t cophase(int n) { double ph = PHY_PI * n / 2.0; return CX_MAKE(cos(ph), sin(ph)); }
static cx_t thetap(int p)  { double ph = PHY_PI * p / 4.0; return CX_MAKE(cos(ph), sin(ph)); }

/* ════════════════════════════════════════════════════════════════════════
 * Rank-1 프리코더 — Table 5.2.2.2.1-5
 * W = (1/√P)·[v_{l,m}; φ_n·v_{l,m}]
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_32port_rank1(int i1_1, int i1_2, int i2, cx_t W[32]) {
    cx_t v[HALF];
    beam_vec(i1_1, i1_2, v);
    cx_t phi = cophase(i2);
    double norm = 1.0 / sqrt((double)P);

    for (int k = 0; k < HALF; k++) {
        W[k]        = norm * v[k];
        W[HALF + k] = norm * phi * v[k];
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Rank-2 프리코더 — Table 5.2.2.2.1-6
 * W = 1/√(2P)·[v_{l,m}, v_{l',m'}; φ_n·v_{l,m}, −φ_n·v_{l',m'}]
 * (k1,k2) = Table 5.2.2.2.1-3 "N1=N2" 열 (2026-08-31 MCP 확인):
 *   i1,3: 0→(0,0), 1→(O1,0), 2→(0,O2), 3→(O1,O2)
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_32port_rank2(int i1_1, int i1_2, int i1_3, int i2, cx_t W[32][2]) {
    static const int k1_tab[CB32_I13_R2] = {0, O1, 0,  O1};
    static const int k2_tab[CB32_I13_R2] = {0, 0,  O2, O2};

    int l  = i1_1, m = i1_2;
    int lp = (l + k1_tab[i1_3]) % (N1 * O1);
    int mp = (m + k2_tab[i1_3]) % (N2 * O2);

    cx_t v0[HALF], v1[HALF];
    beam_vec(l, m, v0);
    beam_vec(lp, mp, v1);
    cx_t phi = cophase(i2);
    double norm = 1.0 / sqrt(2.0 * (double)P);

    for (int k = 0; k < HALF; k++) {
        W[k][0]        = norm * v0[k];
        W[k][1]        = norm * v1[k];
        W[HALF + k][0] = norm * phi * v0[k];
        W[HALF + k][1] = norm * (-phi) * v1[k];
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Rank-3 프리코더 — Table 5.2.2.2.1-7, codebookMode="1-2", P≥16 분기
 * W = 1/√(3P)·[ṽ,ṽ,ṽ; θṽ,−θṽ,θṽ; φṽ,φṽ,−φṽ; φθṽ,−φθṽ,−φθṽ]
 * i1_1 범위가 rank1/2와 다름 — 0..N1·O1/2-1 (ṽ의 절반 DFT 간격에 대응)
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_32port_rank3(int i1_1, int i1_2, int i1_3, int i2, cx_t W[32][3]) {
    cx_t vt[TBLK];
    beam_vec_tilde(i1_1, i1_2, vt);
    cx_t phi = cophase(i2);
    cx_t th  = thetap(i1_3);
    double norm = 1.0 / sqrt(3.0 * (double)P);

    for (int k = 0; k < TBLK; k++) {
        cx_t x = vt[k];
        W[0 * TBLK + k][0] = norm * x;
        W[0 * TBLK + k][1] = norm * x;
        W[0 * TBLK + k][2] = norm * x;

        W[1 * TBLK + k][0] = norm * th * x;
        W[1 * TBLK + k][1] = norm * (-th) * x;
        W[1 * TBLK + k][2] = norm * th * x;

        W[2 * TBLK + k][0] = norm * phi * x;
        W[2 * TBLK + k][1] = norm * phi * x;
        W[2 * TBLK + k][2] = norm * (-phi) * x;

        W[3 * TBLK + k][0] = norm * phi * th * x;
        W[3 * TBLK + k][1] = norm * (-phi) * th * x;
        W[3 * TBLK + k][2] = norm * (-phi) * th * x;
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Rank-4 프리코더 — Table 5.2.2.2.1-8, codebookMode="1-2", P≥16 분기
 * W = 1/√(4P)·[ṽ,ṽ,ṽ,ṽ; θṽ,−θṽ,θṽ,−θṽ; φṽ,φṽ,−φṽ,−φṽ; φθṽ,−φθṽ,−φθṽ,φθṽ]
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_32port_rank4(int i1_1, int i1_2, int i1_3, int i2, cx_t W[32][4]) {
    cx_t vt[TBLK];
    beam_vec_tilde(i1_1, i1_2, vt);
    cx_t phi = cophase(i2);
    cx_t th  = thetap(i1_3);
    double norm = 1.0 / sqrt(4.0 * (double)P);

    for (int k = 0; k < TBLK; k++) {
        cx_t x = vt[k];
        W[0 * TBLK + k][0] = norm * x;
        W[0 * TBLK + k][1] = norm * x;
        W[0 * TBLK + k][2] = norm * x;
        W[0 * TBLK + k][3] = norm * x;

        W[1 * TBLK + k][0] = norm * th * x;
        W[1 * TBLK + k][1] = norm * (-th) * x;
        W[1 * TBLK + k][2] = norm * th * x;
        W[1 * TBLK + k][3] = norm * (-th) * x;

        W[2 * TBLK + k][0] = norm * phi * x;
        W[2 * TBLK + k][1] = norm * phi * x;
        W[2 * TBLK + k][2] = norm * (-phi) * x;
        W[2 * TBLK + k][3] = norm * (-phi) * x;

        W[3 * TBLK + k][0] = norm * phi * th * x;
        W[3 * TBLK + k][1] = norm * (-phi) * th * x;
        W[3 * TBLK + k][2] = norm * (-phi) * th * x;
        W[3 * TBLK + k][3] = norm * phi * th * x;
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * 코드북 검증용 통계 출력 — rank당 후보 수가 1024~2048개라 4/8-port처럼
 * 코드워드 하나하나를 출력하지 않고, 전 후보에 대한 정규화/직교성 오차의
 * 최댓값만 집계해 출력한다(전수 통과 여부 확인 목적).
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_32port_print(int rank) {
    double max_norm_err = 0.0, max_orth_err = 0.0;
    long count = 0;

    if (rank == 1) {
        for (int l = 0; l < CB32_L_COUNT; l++)
            for (int m = 0; m < CB32_M_COUNT; m++)
                for (int n = 0; n < CB32_I2_R1; n++) {
                    cx_t W[32];
                    codebook_type1_sp_32port_rank1(l, m, n, W);
                    double norm2 = 0.0;
                    for (int p = 0; p < 32; p++) norm2 += CX_NORM(W[p]);
                    double e = fabs(norm2 - 1.0);
                    if (e > max_norm_err) max_norm_err = e;
                    count++;
                }
        printf("Rank-1: %ld개 코드워드, ||W||²=1 최대오차=%.3e\n", count, max_norm_err);

    } else if (rank == 2) {
        for (int l = 0; l < CB32_L_COUNT; l++)
            for (int m = 0; m < CB32_M_COUNT; m++)
                for (int i13 = 0; i13 < CB32_I13_R2; i13++)
                    for (int n = 0; n < CB32_I2_R234; n++) {
                        cx_t W[32][2];
                        codebook_type1_sp_32port_rank2(l, m, i13, n, W);
                        double n0 = 0.0, n1 = 0.0; cx_t dot = 0.0;
                        for (int p = 0; p < 32; p++) {
                            n0  += CX_NORM(W[p][0]);
                            n1  += CX_NORM(W[p][1]);
                            dot += conj(W[p][0]) * W[p][1];
                        }
                        double e = fabs(n0 - 0.5) + fabs(n1 - 0.5);
                        if (e > max_norm_err) max_norm_err = e;
                        if (cabs(dot) > max_orth_err) max_orth_err = cabs(dot);
                        count++;
                    }
        printf("Rank-2: %ld개 코드워드, ||W[:,c]||²=0.5 최대오차=%.3e, 직교성 최대오차=%.3e\n",
               count, max_norm_err, max_orth_err);

    } else if (rank == 3 || rank == 4) {
        for (int l = 0; l < CB32_L34_COUNT; l++)
            for (int m = 0; m < CB32_M_COUNT; m++)
                for (int p13 = 0; p13 < CB32_THETA_R34; p13++)
                    for (int n = 0; n < CB32_I2_R234; n++) {
                        double norms[4] = {0,0,0,0};
                        cx_t dots[4][4];
                        for (int a = 0; a < rank; a++)
                            for (int b = 0; b < rank; b++) dots[a][b] = 0.0;

                        if (rank == 3) {
                            cx_t W[32][3];
                            codebook_type1_sp_32port_rank3(l, m, p13, n, W);
                            for (int c = 0; c < 3; c++)
                                for (int k = 0; k < 32; k++) norms[c] += CX_NORM(W[k][c]);
                            for (int a = 0; a < 3; a++)
                                for (int b = 0; b < 3; b++)
                                    for (int k = 0; k < 32; k++)
                                        dots[a][b] += conj(W[k][a]) * W[k][b];
                        } else {
                            cx_t W[32][4];
                            codebook_type1_sp_32port_rank4(l, m, p13, n, W);
                            for (int c = 0; c < 4; c++)
                                for (int k = 0; k < 32; k++) norms[c] += CX_NORM(W[k][c]);
                            for (int a = 0; a < 4; a++)
                                for (int b = 0; b < 4; b++)
                                    for (int k = 0; k < 32; k++)
                                        dots[a][b] += conj(W[k][a]) * W[k][b];
                        }
                        double target = 1.0 / rank;
                        for (int c = 0; c < rank; c++) {
                            double e = fabs(norms[c] - target);
                            if (e > max_norm_err) max_norm_err = e;
                        }
                        for (int a = 0; a < rank; a++)
                            for (int b = 0; b < rank; b++) {
                                if (a == b) continue;
                                double e = cabs(dots[a][b]);
                                if (e > max_orth_err) max_orth_err = e;
                            }
                        count++;
                    }
        printf("Rank-%d: %ld개 코드워드, ||W[:,c]||²=1/%d 최대오차=%.3e, 직교성 최대오차=%.3e\n",
               rank, count, rank, max_norm_err, max_orth_err);
    } else {
        printf("=== Type I SP 32-port Rank-%d: 미구현 (rank 1~4만 지원) ===\n", rank);
    }
}

/* ── 일반 N×N(N≤4) 복소 Gauss-Jordan 역행렬 (부분 피벗) ─────────────────
 * mimo.c의 inv3x3/inv4x4와 동일 구조를 rank(1~4)에 대해 하나로 통합한
 * 것 — RI+PMI 선택 시의 용량 추정(Gramian 역행렬)에만 사용하고, 실제
 * 데이터 검출은 mimo.c의 전용 검출기(mrc_combine_4rx/mimo_mmse_detect_
 * 4rx2/4rx3/4x4)를 그대로 쓴다. 특이/근사특이 시 -1 반환 — 이 부분 피벗
 * 자체가 4-port/8-port의 2×2 전용 catastrophic-cancellation 방어(det
 * 부호 확인)보다 더 일반적인 수치 안정화이므로 별도 방어 코드가 필요
 * 없다. */
static int inv_generic(int n, const cx_t M[4][4], cx_t inv[4][4]) {
    cx_t aug[4][8];
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) aug[r][c] = M[r][c];
        for (int c = 0; c < n; c++) aug[r][c + n] = (r == c) ? 1.0 : 0.0;
    }
    for (int col = 0; col < n; col++) {
        int    pivot_row = col;
        double max_abs   = cabs(aug[col][col]);
        for (int r = col + 1; r < n; r++) {
            double v = cabs(aug[r][col]);
            if (v > max_abs) { max_abs = v; pivot_row = r; }
        }
        if (max_abs < 1e-14) return -1;
        if (pivot_row != col)
            for (int c = 0; c < 2 * n; c++) {
                cx_t tmp = aug[col][c]; aug[col][c] = aug[pivot_row][c]; aug[pivot_row][c] = tmp;
            }
        cx_t piv_inv = 1.0 / aug[col][col];
        for (int c = 0; c < 2 * n; c++) aug[col][c] *= piv_inv;
        for (int r = 0; r < n; r++) {
            if (r == col) continue;
            cx_t factor = aug[r][col];
            for (int c = 0; c < 2 * n; c++) aug[r][c] -= factor * aug[col][c];
        }
    }
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            inv[r][c] = aug[r][c + n];
    return 0;
}

/* rank개 레이어 유효채널 he[4][rank]에 대한 MMSE 후-검출 SINR 합 기반
 * 추정 용량. rank=1일 때도 동일 공식이 그대로 후-빔포밍 power/N0 공식과
 * 수학적으로 동치(A=N0+|he|², a=|he|²/A, log2(1+a/(1-a))=log2(1+|he|²/N0))
 * 이므로 rank를 특별 취급하지 않고 전 rank에 하나의 경로로 처리한다. */
static double post_detect_capacity(const cx_t he[4][4], int rank, double N0) {
    cx_t A[4][4];
    for (int i = 0; i < rank; i++)
        for (int j = 0; j < rank; j++) {
            A[i][j] = (i == j) ? (cx_t)N0 : 0.0;
            for (int r = 0; r < 4; r++) A[i][j] += conj(he[r][i]) * he[r][j];
        }
    cx_t Ainv[4][4];
    if (inv_generic(rank, A, Ainv) < 0) return -1.0;

    double cap = 0.0;
    for (int t = 0; t < rank; t++) {
        double a = 1.0 - N0 * creal(Ainv[t][t]);
        if (a < 1e-6) a = 1e-6;
        if (a >= 1.0) a = 1.0 - 1e-6;
        cap += log2(1.0 + a / (1.0 - a));
    }
    return cap;
}

/* ════════════════════════════════════════════════════════════════════════
 * RI + PMI 동시 선택 — rank 1~4 전수탐색 (1024+2048+1024+1024=5120 후보)
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_32port_ri_pmi_select(
        const cx_t H[4][32], double N0,
        int *sel_rank, int *sel_i1_1, int *sel_i1_2, int *sel_i1_3, int *sel_i2) {

    double best_cap = -1.0;
    *sel_rank = 1; *sel_i1_1 = 0; *sel_i1_2 = 0; *sel_i1_3 = 0; *sel_i2 = 0;

    /* rank-1: 16×16×4 = 1024 */
    for (int l = 0; l < CB32_L_COUNT; l++)
        for (int m = 0; m < CB32_M_COUNT; m++)
            for (int n = 0; n < CB32_I2_R1; n++) {
                cx_t W[32];
                codebook_type1_sp_32port_rank1(l, m, n, W);
                cx_t he[4][4];
                for (int r = 0; r < 4; r++) {
                    he[r][0] = 0.0;
                    for (int t = 0; t < 32; t++) he[r][0] += H[r][t] * W[t];
                }
                double cap = post_detect_capacity(he, 1, N0);
                if (cap > best_cap) {
                    best_cap = cap;
                    *sel_rank = 1; *sel_i1_1 = l; *sel_i1_2 = m; *sel_i1_3 = 0; *sel_i2 = n;
                }
            }

    /* rank-2: 16×16×4×2 = 2048 */
    for (int l = 0; l < CB32_L_COUNT; l++)
        for (int m = 0; m < CB32_M_COUNT; m++)
            for (int i13 = 0; i13 < CB32_I13_R2; i13++)
                for (int n = 0; n < CB32_I2_R234; n++) {
                    cx_t W[32][2];
                    codebook_type1_sp_32port_rank2(l, m, i13, n, W);
                    cx_t he[4][4];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 2; c++) {
                            he[r][c] = 0.0;
                            for (int t = 0; t < 32; t++) he[r][c] += H[r][t] * W[t][c];
                        }
                    double cap = post_detect_capacity(he, 2, N0);
                    if (cap > best_cap) {
                        best_cap = cap;
                        *sel_rank = 2; *sel_i1_1 = l; *sel_i1_2 = m; *sel_i1_3 = i13; *sel_i2 = n;
                    }
                }

    /* rank-3: 8×16×4×2 = 1024 */
    for (int l = 0; l < CB32_L34_COUNT; l++)
        for (int m = 0; m < CB32_M_COUNT; m++)
            for (int p13 = 0; p13 < CB32_THETA_R34; p13++)
                for (int n = 0; n < CB32_I2_R234; n++) {
                    cx_t W[32][3];
                    codebook_type1_sp_32port_rank3(l, m, p13, n, W);
                    cx_t he[4][4];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 3; c++) {
                            he[r][c] = 0.0;
                            for (int t = 0; t < 32; t++) he[r][c] += H[r][t] * W[t][c];
                        }
                    double cap = post_detect_capacity(he, 3, N0);
                    if (cap > best_cap) {
                        best_cap = cap;
                        *sel_rank = 3; *sel_i1_1 = l; *sel_i1_2 = m; *sel_i1_3 = p13; *sel_i2 = n;
                    }
                }

    /* rank-4: 8×16×4×2 = 1024 */
    for (int l = 0; l < CB32_L34_COUNT; l++)
        for (int m = 0; m < CB32_M_COUNT; m++)
            for (int p13 = 0; p13 < CB32_THETA_R34; p13++)
                for (int n = 0; n < CB32_I2_R234; n++) {
                    cx_t W[32][4];
                    codebook_type1_sp_32port_rank4(l, m, p13, n, W);
                    cx_t he[4][4];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 4; c++) {
                            he[r][c] = 0.0;
                            for (int t = 0; t < 32; t++) he[r][c] += H[r][t] * W[t][c];
                        }
                    double cap = post_detect_capacity(he, 4, N0);
                    if (cap > best_cap) {
                        best_cap = cap;
                        *sel_rank = 4; *sel_i1_1 = l; *sel_i1_2 = m; *sel_i1_3 = p13; *sel_i2 = n;
                    }
                }
}
