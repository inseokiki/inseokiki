/* ================================================================
 *  codebook.c
 *  5G NR Type I Single Panel Codebook — 4 CSI-RS ports
 *
 *  구성: N1=2, N2=1, Ng=2 (XPOL), O1=4, O2=1, P=4
 *  표준: TS 38.214 Sec 5.2.2.2.1
 *
 *  포트 순서: [pol1_ant0, pol1_ant1, pol2_ant0, pol2_ant1]
 *   → 수평 편파 그룹(pol1): 안테나 0,1  /  수직 편파 그룹(pol2): 안테나 2,3
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "codebook.h"
#include <math.h>
#include <stdio.h>

/* ── 내부 상수 ────────────────────────────────────────────────────────── */
#define N1 2           /* 수평 안테나 수                                  */
#define O1 4           /* 오버샘플링 인수 (TS 38.214 기본값)              */
#define P  4           /* 총 CSI-RS 포트 수 = N1 * N2 * Ng = 2*1*2       */
/* PHY_PI는 utils.h에서 정의됨 */

/* ── DFT 빔 벡터 ─────────────────────────────────────────────────────────
 *
 * v_l = [1, e^{j·2π·l/(N1·O1)}]^T = [1, e^{jπl/4}]^T
 *
 * 미정규화(각 원소 단위 크기). l = 0, 1, ..., N1*O1-1 = 0..7
 * 공간 방향 θ_l ∝ l/(N1·O1): l=0이 broadside, l=4가 endfire 반대편
 *
 * 핵심 직교성: v_l^H v_{l+N1*O1/2} = 1 + e^{jπ} = 0
 *   → 빔 쌍 (l, l+4)는 DFT 직교 관계 ★ rank-2 빔쌍 다이버시티의 근거
 */
static void beam_vec(int l, cx_t v[2]) {
    v[0] = CX_MAKE(1.0, 0.0);
    double phase = PHY_PI * l / (double)(N1 * O1 / 2);   /* = 2π*l/(N1*O1) = πl/4 */
    v[1] = CX_MAKE(cos(phase), sin(phase));
}

/* ── 코피에이징 스칼라 ────────────────────────────────────────────────────
 *
 * φ_n = e^{jπn/2}  →  φ_0=1, φ_1=j, φ_2=−1, φ_3=−j
 * 두 편파 그룹 간의 위상 관계를 조정해 빔 조향 방향성 확보
 */
static cx_t cophase(int n) {
    double phase = PHY_PI * n / 2.0;
    return CX_MAKE(cos(phase), sin(phase));
}

/* ════════════════════════════════════════════════════════════════════════
 * Rank-1 프리코더
 *
 * W = (1/2) · [v_l; φ_n · v_l]   (4×1)
 *
 * 구조 해석:
 *   상위 2원소 [v_l[0]; v_l[1]] : pol1 그룹 (ant 0, 1) 신호
 *   하위 2원소 [φ_n·v_l[0]; φ_n·v_l[1]] : pol2 그룹 (ant 2, 3) 신호
 *   → 같은 DFT 빔을 두 편파 그룹에 φ_n 위상차를 두고 동시 송신
 *
 * 정규화 증명:
 *   ||W||² = (1/4)(||v_l||² + |φ_n|²·||v_l||²) = (1/4)·2·2 = 1 ✓
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_4port_rank1(int i1_1, int i2, cx_t W[4]) {
    cx_t v[2];
    beam_vec(i1_1, v);
    cx_t phi = cophase(i2);

    double norm = 1.0 / (double)P * 2.0;   /* = 1/2 : 1/sqrt(P) = 1/2 */
    norm = 0.5;

    W[0] = norm * v[0];
    W[1] = norm * v[1];
    W[2] = norm * phi * v[0];
    W[3] = norm * phi * v[1];
}

/* ════════════════════════════════════════════════════════════════════════
 * Rank-2 프리코더
 *
 * ──────────────────────────────────────────────────────────────────────
 * 변형 A (i1_3 = 0): 빔쌍 다이버시티 — 직교 빔 l과 l' = l+4
 *
 * W = (1/2) · [v_l,    v_{l'};
 *              φ_{n1}·v_l, φ_{n2}·v_{l'}]
 *
 * 두 열(레이어)이 다른 DFT 빔을 사용 → 공간 다중화에 적합
 *
 * 직교성 증명 (W[:,0]^H W[:,1) = 0):
 *   W[:,0]^H W[:,1] = (1/4)(v_l^H v_{l'} + φ_{n1}^* φ_{n2} v_l^H v_{l'})
 *                   = (1/4)(1 + e^{jπ})(1 + φ_{n1}^* φ_{n2}) ... 아니면
 *   v_l^H v_{l'} = 1 + e^{j·π(l'-l)/4} = 1 + e^{jπ} = 0  ★ (l'-l=4)
 *   → v_l ⊥ v_{l'} 이므로 코피에이징 무관하게 직교 ✓
 *
 * i2 → (φ_{n1}, φ_{n2}) 대응:
 *   i2=0 → (1, 1)   i2=1 → (1, j)   i2=2 → (1, -1)   i2=3 → (1, -j)
 * ──────────────────────────────────────────────────────────────────────
 * 변형 B (i1_3 = 1): 동일빔 교차편파 다이버시티
 *
 * W = (1/2) · [v_l,    v_l;
 *               φ_n·v_l, −φ_n·v_l]
 *
 * 두 열이 같은 DFT 빔, 편파 그룹 위상을 반전 → 교차편파 채널에 적합
 *
 * 직교성 증명:
 *   W[:,0]^H W[:,1] = (1/4)(||v_l||² + (φ_n)^*(−φ_n)||v_l||²)
 *                   = (1/4)(2 − 2) = 0 ✓
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_4port_rank2(int i1_1, int i1_3, int i2, cx_t W[4][2]) {
    double norm = 0.5;

    if (i1_3 == 0) {
        /* 변형 A: 빔쌍 다이버시티 */
        int l  = i1_1;                            /* 기준 빔: 0~3          */
        int lp = (l + N1 * O1 / 2) % (N1 * O1);  /* 직교 빔: l+4 (mod 8)  */

        cx_t v0[2], v1[2];
        beam_vec(l,  v0);
        beam_vec(lp, v1);

        /* i2 → 코피에이징 쌍 (φ_{n1}=1 고정, φ_{n2} 가변) */
        static const int n2_table[4] = { 0, 1, 2, 3 };  /* φ=1,j,-1,-j */
        cx_t phi0 = cophase(0);           /* φ_{n1} = 1 (i2 무관) */
        cx_t phi1 = cophase(n2_table[i2]);/* φ_{n2} varies by i2  */

        /* 열 0: [v_l; φ_{n1}·v_l]   열 1: [v_{l'}; φ_{n2}·v_{l'}] */
        W[0][0] = norm * v0[0];          W[0][1] = norm * v1[0];
        W[1][0] = norm * v0[1];          W[1][1] = norm * v1[1];
        W[2][0] = norm * phi0 * v0[0];   W[2][1] = norm * phi1 * v1[0];
        W[3][0] = norm * phi0 * v0[1];   W[3][1] = norm * phi1 * v1[1];
    } else {
        /* 변형 B: 동일빔 교차편파 다이버시티 */
        cx_t v[2];
        beam_vec(i1_1, v);
        cx_t phi = cophase(i2);

        /* 열 0: [v_l; φ_n·v_l]   열 1: [v_l; -φ_n·v_l] */
        W[0][0] = norm * v[0];          W[0][1] = norm * v[0];
        W[1][0] = norm * v[1];          W[1][1] = norm * v[1];
        W[2][0] = norm * phi  * v[0];   W[2][1] = norm * (-phi) * v[0];
        W[3][0] = norm * phi  * v[1];   W[3][1] = norm * (-phi) * v[1];
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * 코드북 전체 출력 (검증용)
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_4port_print(int rank) {
    if (rank == 1) {
        printf("=== Type I SP 4-port Rank-1 Codebook (N1=%d, O1=%d) ===\n", N1, O1);
        printf("%-6s %-4s   %-40s  %-8s  %s\n",
               "i1_1", "i2", "W[0..3] (Re+jIm)", "||W||²", "빔각(°)");
        printf("%s\n", "----------------------------------------------------------------------");

        for (int i1 = 0; i1 < N1 * O1; i1++) {
            for (int i2 = 0; i2 < 4; i2++) {
                cx_t W[4];
                codebook_type1_sp_4port_rank1(i1, i2, W);

                double norm2 = 0.0;
                for (int p = 0; p < P; p++) norm2 += CX_NORM(W[p]);

                /* 빔 방향: θ = arcsin(l/(N1·O1) * λ/d) 의 정규화 각도
                 * d=λ/2 가정 시: θ_l [deg] = arcsin(l / (N1·O1)) * 180/π
                 * l=0: broadside (0°), l=4: 반대방향                    */
                double sin_theta = (double)i1 / (double)(N1 * O1);
                double deg = (sin_theta <= 1.0) ? asin(sin_theta) * 180.0 / PHY_PI : 90.0;

                printf("i1=%2d  i2=%d  [%+.3f%+.3fj, %+.3f%+.3fj, %+.3f%+.3fj, %+.3f%+.3fj]"
                       "  %.4f  %.1f°\n",
                       i1, i2,
                       creal(W[0]), cimag(W[0]),
                       creal(W[1]), cimag(W[1]),
                       creal(W[2]), cimag(W[2]),
                       creal(W[3]), cimag(W[3]),
                       norm2, deg);
            }
        }
        printf("총 rank-1 코드워드: %d\n\n", N1 * O1 * 4);

    } else if (rank == 2) {
        printf("=== Type I SP 4-port Rank-2 Codebook (N1=%d, O1=%d) ===\n", N1, O1);

        /* 변형 A: i1_3=0, i1_1=0..3, i2=0..3 */
        printf("\n[변형 A: i1_3=0, 빔쌍 다이버시티 (l, l+4)]\n");
        printf("%-6s %-6s %-4s   %-8s  %-8s  %s\n",
               "i1_1", "i1_3", "i2", "||W[:,0]||²", "||W[:,1]||²", "직교성|W0^HW1|");
        printf("%s\n", "--------------------------------------------------------------");

        for (int i1 = 0; i1 < N1 * O1 / 2; i1++) {
            for (int i2 = 0; i2 < 4; i2++) {
                cx_t W[4][2];
                codebook_type1_sp_4port_rank2(i1, 0, i2, W);

                double n0 = 0.0, n1 = 0.0;
                cx_t dot = 0.0;
                for (int p = 0; p < P; p++) {
                    n0  += CX_NORM(W[p][0]);
                    n1  += CX_NORM(W[p][1]);
                    dot += conj(W[p][0]) * W[p][1];
                }
                printf("i1=%d  i1_3=0  i2=%d   %.4f      %.4f      %.2e\n",
                       i1, i2, n0, n1, cabs(dot));
            }
        }

        /* 변형 B: i1_3=1, i1_1=0..7, i2=0..3 */
        printf("\n[변형 B: i1_3=1, 동일빔 교차편파 (l, l)]\n");
        printf("%-6s %-6s %-4s   %-8s  %-8s  %s\n",
               "i1_1", "i1_3", "i2", "||W[:,0]||²", "||W[:,1]||²", "직교성|W0^HW1|");
        printf("%s\n", "--------------------------------------------------------------");

        for (int i1 = 0; i1 < N1 * O1; i1++) {
            for (int i2 = 0; i2 < 4; i2++) {
                cx_t W[4][2];
                codebook_type1_sp_4port_rank2(i1, 1, i2, W);

                double n0 = 0.0, n1 = 0.0;
                cx_t dot = 0.0;
                for (int p = 0; p < P; p++) {
                    n0  += CX_NORM(W[p][0]);
                    n1  += CX_NORM(W[p][1]);
                    dot += conj(W[p][0]) * W[p][1];
                }
                printf("i1=%d  i1_3=1  i2=%d   %.4f      %.4f      %.2e\n",
                       i1, i2, n0, n1, cabs(dot));
            }
        }

        int total = (N1 * O1 / 2 * 4) + (N1 * O1 * 4);
        printf("\n총 rank-2 코드워드: %d (변형A: %d + 변형B: %d)\n\n",
               total, N1 * O1 / 2 * 4, N1 * O1 * 4);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * RI + PMI 동시 선택 (Rank Adaptation)
 *
 * Rank-1 탐색: 32 후보 (8빔 × 4코피에이징)
 *   기준  C₁ = log₂(1 + ||H·W||²/N₀)
 *
 * Rank-2 탐색: 48 후보 (변형A 16 + 변형B 32)
 *   H_eff = H·W (4×2) → A = H_eff^H H_eff + N₀·I (2×2)
 *   α_j = 1 − N₀·Re{(A⁻¹)_jj}
 *   C₂ = Σ_j log₂(1 + α_j/(1−α_j))   (MMSE 후-검출 SINR 기반 용량)
 *
 * 총 탐색 후보: 80개  (수 μs 수준, 블록-평탄 채널 1회 드로우 당 1회 실행)
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_4port_ri_pmi_select(
        const cx_t H[4][4], double N0,
        int *sel_rank, int *sel_i1_1, int *sel_i1_3, int *sel_i2,
        int *r1_i1_1, int *r1_i2,
        int *r2_i1_1, int *r2_i1_3, int *r2_i2) {

    double best_cap_r1 = -1.0, best_cap_r2 = -1.0, best_cap = -1.0;

    /* 초기화 */
    *sel_rank = 1; *sel_i1_1 = 0; *sel_i1_3 = 0; *sel_i2 = 0;
    *r1_i1_1 = 0; *r1_i2 = 0;
    *r2_i1_1 = 0; *r2_i1_3 = 0; *r2_i2 = 0;

    /* ── Rank-1 탐색 (32 후보) ─────────────────────────────────────────── */
    for (int i1 = 0; i1 < N1 * O1; i1++) {
        for (int i2 = 0; i2 < 4; i2++) {
            cx_t W[4];
            codebook_type1_sp_4port_rank1(i1, i2, W);

            /* H_eff[r] = Σ_t H[r][t]·W[t]  (4×1) → 수신 파워 */
            double pw = 0.0;
            for (int r = 0; r < 4; r++) {
                cx_t he = 0.0;
                for (int t = 0; t < P; t++) he += H[r][t] * W[t];
                pw += CX_NORM(he);
            }
            double cap = log2(1.0 + pw / N0);

            if (cap > best_cap_r1) {
                best_cap_r1 = cap;
                *r1_i1_1 = i1;
                *r1_i2   = i2;
            }
        }
    }

    /* ── Rank-2 탐색 (변형A 16 + 변형B 32 = 48 후보) ─────────────────── */
    for (int i1_3 = 0; i1_3 <= 1; i1_3++) {
        int i1_max = (i1_3 == 0) ? (N1 * O1 / 2) : (N1 * O1);
        for (int i1 = 0; i1 < i1_max; i1++) {
            for (int i2 = 0; i2 < 4; i2++) {
                cx_t W2[4][2];
                codebook_type1_sp_4port_rank2(i1, i1_3, i2, W2);

                /* H_eff[r][l] = Σ_t H[r][t]·W2[t][l]  (4×2) */
                cx_t he[4][2];
                for (int r = 0; r < 4; r++)
                    for (int l = 0; l < 2; l++) {
                        he[r][l] = 0.0;
                        for (int t = 0; t < P; t++) he[r][l] += H[r][t] * W2[t][l];
                    }

                /* A = H_eff^H H_eff + N₀·I  (2×2 Gramian) */
                cx_t A00 = (cx_t)N0, A01 = 0.0, A10 = 0.0, A11 = (cx_t)N0;
                for (int r = 0; r < 4; r++) {
                    A00 += conj(he[r][0]) * he[r][0];
                    A01 += conj(he[r][0]) * he[r][1];
                    A10 += conj(he[r][1]) * he[r][0];
                    A11 += conj(he[r][1]) * he[r][1];
                }

                /* A⁻¹ (2×2 직접 역산) */
                cx_t det = A00 * A11 - A01 * A10;
                if (cabs(det) < 1e-14) continue;   /* 특이 행렬 → 스킵 */
                cx_t id = 1.0 / det;
                double inv00 = creal( A11 * id);
                double inv11 = creal( A00 * id);

                /* α_j = 1 − N₀·Re{(A⁻¹)_jj} */
                double a0 = 1.0 - N0 * inv00;
                double a1 = 1.0 - N0 * inv11;
                if (a0 < 1e-6) a0 = 1e-6;
                if (a1 < 1e-6) a1 = 1e-6;
                if (a0 >= 1.0) a0 = 1.0 - 1e-6;
                if (a1 >= 1.0) a1 = 1.0 - 1e-6;

                /* C₂ = log₂(1+SINR₀) + log₂(1+SINR₁),  SINR_j = α_j/(1-α_j) */
                double cap = log2(1.0 + a0 / (1.0 - a0)) + log2(1.0 + a1 / (1.0 - a1));

                if (cap > best_cap_r2) {
                    best_cap_r2 = cap;
                    *r2_i1_1  = i1;
                    *r2_i1_3  = i1_3;
                    *r2_i2    = i2;
                }
            }
        }
    }

    /* ── 최적 rank 선택 ────────────────────────────────────────────────── */
    if (best_cap_r1 >= best_cap_r2) {
        best_cap  = best_cap_r1;
        *sel_rank = 1;
        *sel_i1_1 = *r1_i1_1;
        *sel_i1_3 = 0;
        *sel_i2   = *r1_i2;
    } else {
        best_cap  = best_cap_r2;
        *sel_rank = 2;
        *sel_i1_1 = *r2_i1_1;
        *sel_i1_3 = *r2_i1_3;
        *sel_i2   = *r2_i2;
    }
    (void)best_cap;  /* 상위 호출자가 용량 값 자체는 사용 안 함 */
}

/* ════════════════════════════════════════════════════════════════════════
 * Rank-1 PMI Exhaustive Search
 *
 * 기준: 최대 후-빔포밍 수신 파워  P_rx(l,n) = Σ_r |Σ_t H[r][t]·W[t]|²
 *
 * 채널 행렬 H[rx][4], 수신 안테나 수 num_rx
 *
 * 실제 gNB에서는 UE가 CSI-RS로 측정한 H를 바탕으로 이 탐색을 수행하고
 * (i1_1, i2) = (i1,1, i2) 를 PMI로 피드백함.
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_4port_pmi_search(const cx_t H[][4], int num_rx,
                                         int *best_i1, int *best_i2,
                                         double *max_power) {
    *max_power = -1.0;
    *best_i1   = 0;
    *best_i2   = 0;

    for (int i1 = 0; i1 < N1 * O1; i1++) {
        for (int i2 = 0; i2 < 4; i2++) {
            cx_t W[4];
            codebook_type1_sp_4port_rank1(i1, i2, W);

            double power = 0.0;
            for (int r = 0; r < num_rx; r++) {
                /* y_r = Σ_t H[r][t] · W[t] */
                cx_t y = 0.0;
                for (int t = 0; t < P; t++) y += H[r][t] * W[t];
                power += CX_NORM(y);
            }

            if (power > *max_power) {
                *max_power = power;
                *best_i1   = i1;
                *best_i2   = i2;
            }
        }
    }
}
