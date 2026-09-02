/* ================================================================
 *  codebook_8port.c
 *  5G NR Type I Single Panel Codebook — 8 CSI-RS ports (N1=4,N2=1)
 *
 *  구성: N1=4, N2=1, Ng=2 (XPOL), O1=4, O2=1, P=8
 *  표준: TS 38.214 Sec 5.2.2.2.1, Table 5.2.2.2.1-2
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "codebook_8port.h"
#include <math.h>
#include <stdio.h>

/* ── 내부 상수 ────────────────────────────────────────────────────────── */
#define N1 4           /* 수평 안테나 수                                  */
#define O1 4           /* 오버샘플링 인수 (TS 38.214 표 5.2.2.2.1-2, P=8) */
#define P  8           /* 총 CSI-RS 포트 수 = N1 * N2 * Ng = 4*1*2       */

/* ── DFT 빔 벡터 ─────────────────────────────────────────────────────────
 *
 * v_l[k] = e^{j·2π·l·k/(N1·O1)},  k = 0..N1-1
 *
 * 4-port(N1=2) 코드의 beam_vec()과 동일한 정의를 N1=4로 일반화한 것 —
 * 정의 자체가 N1에 의존하는 특수 케이스를 두지 않으므로 확장에 별도
 * 가정이 필요 없다. 미정규화(각 원소 단위 크기).
 * l = 0, 1, ..., N1*O1-1 = 0..15
 */
static void beam_vec(int l, cx_t v[N1]) {
    for (int k = 0; k < N1; k++) {
        double phase = 2.0 * PHY_PI * l * k / (double)(N1 * O1);
        v[k] = CX_MAKE(cos(phase), sin(phase));
    }
}

/* ── 코피에이징 스칼라 (4-port와 동일 정의, N1 무관) ────────────────────
 * φ_n = e^{jπn/2}  →  φ_0=1, φ_1=j, φ_2=−1, φ_3=−j
 */
static cx_t cophase(int n) {
    double phase = PHY_PI * n / 2.0;
    return CX_MAKE(cos(phase), sin(phase));
}

/* ════════════════════════════════════════════════════════════════════════
 * Rank-1 프리코더
 *
 * W = (1/√P) · [v_l; φ_n · v_l]   (8×1)
 *
 * 정규화 증명: ||W||² = (1/P)(||v_l||² + |φ_n|²·||v_l||²)
 *                     = (1/8)(4 + 4) = 1 ✓  (||v_l||²=N1=4, |φ_n|=1)
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_8port_rank1(int i1_1, int i2, cx_t W[8]) {
    cx_t v[N1];
    beam_vec(i1_1, v);
    cx_t phi = cophase(i2);

    double norm = 1.0 / sqrt((double)P);   /* 1/√8 */

    for (int k = 0; k < N1; k++) {
        W[k]      = norm * v[k];
        W[N1 + k] = norm * phi * v[k];
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Rank-2 프리코더
 *
 * TS 38.214 Table 5.2.2.2.1-6 (codebookMode=1) 원식을 N1=4에 그대로 적용:
 *
 * W = (1/√(2P)) · [v_l,    v_l';
 *                  φ_n·v_l, −φ_n·v_l']            l' = (l+k1) mod (N1·O1)
 *
 * k1은 Table 5.2.2.2.1-3의 "N1>2, N2=1" 열에서 확정한 값 — k1 = i1_3·O1
 * (2026-08-31, 3GPP MCP 서버로 OLE 이미지를 직접 확인해 N1·O1/2 가설을
 * 기각함. codebook_8port.h 상단 주석 참조).
 *
 * 직교성 증명 (W[:,0]^H W[:,1] = 0):
 *   = (1/(2P))·(v_l^H v_l' + φ_n^*·(−φ_n)·v_l^H v_l')
 *   = (1/(2P))·(1 − 1)·v_l^H v_l' = 0  ✓ (φ_n 항의 부호가 서로 상쇄 —
 *   v_l ⊥ v_l' 여부와 무관하게 항상 성립, i1_3=0인 l'=l 케이스 포함)
 *
 * 정규화 증명 (열당 0.5, 총 1 — rank-1과 동일 총 전력):
 *   ||W[:,0]||² = (1/(2P))·(||v_l||²+||v_l||²) = (1/(2P))·2N1 = N1/P = 0.5
 *   (N1=4, P=8) — 동일하게 ||W[:,1]||²=0.5, 합계 1 ✓
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_8port_rank2(int i1_1, int i1_3, int i2, cx_t W[8][2]) {
    int l  = i1_1;
    int k1 = i1_3 * O1;                       /* Table 5.2.2.2.1-3, N1>2,N2=1 */
    int lp = (l + k1) % (N1 * O1);

    cx_t v0[N1], v1[N1];
    beam_vec(l,  v0);
    beam_vec(lp, v1);
    cx_t phi = cophase(i2);

    double norm = 1.0 / sqrt(2.0 * (double)P);   /* 1/√(2·8) = 1/4 */

    for (int k = 0; k < N1; k++) {
        W[k]     [0] = norm * v0[k];
        W[k]     [1] = norm * v1[k];
        W[N1 + k][0] = norm * phi * v0[k];
        W[N1 + k][1] = norm * (-phi) * v1[k];
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * 코드북 전체 출력 (검증용)
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_8port_print(int rank) {
    if (rank == 1) {
        printf("=== Type I SP 8-port Rank-1 Codebook (N1=%d, O1=%d) ===\n", N1, O1);
        printf("%-6s %-4s   %-8s  %s\n", "i1_1", "i2", "||W||²", "빔각(°)");
        printf("%s\n", "----------------------------------------------------------------");

        for (int i1 = 0; i1 < N1 * O1; i1++) {
            for (int i2 = 0; i2 < 4; i2++) {
                cx_t W[P];
                codebook_type1_sp_8port_rank1(i1, i2, W);

                double norm2 = 0.0;
                for (int p = 0; p < P; p++) norm2 += CX_NORM(W[p]);

                double sin_theta = (double)i1 / (double)(N1 * O1);
                double deg = (sin_theta <= 1.0) ? asin(sin_theta) * 180.0 / PHY_PI : 90.0;

                printf("i1=%2d  i2=%d  ||W||²=%.4f  %.1f°\n", i1, i2, norm2, deg);
            }
        }
        printf("총 rank-1 코드워드: %d\n\n", N1 * O1 * 4);
    } else if (rank == 2) {
        printf("=== Type I SP 8-port Rank-2 Codebook (N1=%d, O1=%d) ===\n", N1, O1);
        printf("%-6s %-6s %-4s   %-8s  %-8s  %s\n",
               "i1_1", "i1_3", "i2", "||W[:,0]||²", "||W[:,1]||²", "직교성|W0^HW1|");
        printf("%s\n", "--------------------------------------------------------------");

        for (int i1 = 0; i1 < N1 * O1; i1++) {
            for (int i13 = 0; i13 < CB8_I13_COUNT; i13++) {
                for (int i2 = 0; i2 < 4; i2++) {
                    cx_t W[P][2];
                    codebook_type1_sp_8port_rank2(i1, i13, i2, W);

                    double n0 = 0.0, n1 = 0.0;
                    cx_t dot = 0.0;
                    for (int p = 0; p < P; p++) {
                        n0  += CX_NORM(W[p][0]);
                        n1  += CX_NORM(W[p][1]);
                        dot += conj(W[p][0]) * W[p][1];
                    }
                    printf("i1=%2d  i1_3=%d  i2=%d   %.4f      %.4f      %.2e\n",
                           i1, i13, i2, n0, n1, cabs(dot));
                }
            }
        }
        printf("\n총 rank-2 코드워드: %d\n\n", N1 * O1 * CB8_I13_COUNT * 4);
    } else {
        printf("=== Type I SP 8-port Rank-%d: 미구현 (rank 1,2만 지원) ===\n\n", rank);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Rank-1 PMI Exhaustive Search
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_8port_pmi_search(const cx_t H[][8], int num_rx,
                                         int *best_i1, int *best_i2,
                                         double *max_power) {
    *max_power = -1.0;
    *best_i1   = 0;
    *best_i2   = 0;

    for (int i1 = 0; i1 < N1 * O1; i1++) {
        for (int i2 = 0; i2 < 4; i2++) {
            cx_t W[P];
            codebook_type1_sp_8port_rank1(i1, i2, W);

            double power = 0.0;
            for (int r = 0; r < num_rx; r++) {
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

/* ════════════════════════════════════════════════════════════════════════
 * RI + PMI 동시 선택 (Rank Adaptation)
 *
 * 4-port codebook_type1_sp_4port_ri_pmi_select()(codebook.c)와 동일한
 * 방식 — rank-1: 후-빔포밍 수신 파워 기반 용량, rank-2: MMSE 후-검출
 * SINR 합 기반 용량 — 을 8-port 인덱스 범위로 확장한 것.
 *
 * Rank-1 탐색: 64 후보 (16빔 × 4코피에이징)
 * Rank-2 탐색: 256 후보 (16빔 × 4(i1_3) × 4코피에이징)
 * 총 탐색 후보: 320개
 * ════════════════════════════════════════════════════════════════════════ */
void codebook_type1_sp_8port_ri_pmi_select(
        const cx_t H[4][8], double N0,
        int *sel_rank, int *sel_i1_1, int *sel_i1_3, int *sel_i2,
        int *r1_i1_1, int *r1_i2,
        int *r2_i1_1, int *r2_i1_3, int *r2_i2) {

    double best_cap_r1 = -1.0, best_cap_r2 = -1.0, best_cap = -1.0;

    *sel_rank = 1; *sel_i1_1 = 0; *sel_i1_3 = 0; *sel_i2 = 0;
    *r1_i1_1 = 0; *r1_i2 = 0;
    *r2_i1_1 = 0; *r2_i1_3 = 0; *r2_i2 = 0;

    /* ── Rank-1 탐색 (16 × 4 = 64 후보) ───────────────────────────────── */
    for (int i1 = 0; i1 < N1 * O1; i1++) {
        for (int i2 = 0; i2 < 4; i2++) {
            cx_t W[8];
            codebook_type1_sp_8port_rank1(i1, i2, W);

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

    /* ── Rank-2 탐색 (16 × 4 × 4 = 256 후보) ─────────────────────────── */
    for (int i1_3 = 0; i1_3 < CB8_I13_COUNT; i1_3++) {
        for (int i1 = 0; i1 < N1 * O1; i1++) {
            for (int i2 = 0; i2 < 4; i2++) {
                cx_t W2[8][2];
                codebook_type1_sp_8port_rank2(i1, i1_3, i2, W2);

                cx_t he[4][2];
                for (int r = 0; r < 4; r++)
                    for (int l = 0; l < 2; l++) {
                        he[r][l] = 0.0;
                        for (int t = 0; t < P; t++) he[r][l] += H[r][t] * W2[t][l];
                    }

                /* A = H_eff^H H_eff + N0·I (2×2 Gramian) — catastrophic
                 * cancellation 방어는 4-port codebook.c와 동일 이유로 필요
                 * (codebook.c의 해당 주석 참조, 2026-08-27). */
                cx_t A00 = (cx_t)N0, A01 = 0.0, A10 = 0.0, A11 = (cx_t)N0;
                for (int r = 0; r < 4; r++) {
                    A00 += conj(he[r][0]) * he[r][0];
                    A01 += conj(he[r][0]) * he[r][1];
                    A10 += conj(he[r][1]) * he[r][0];
                    A11 += conj(he[r][1]) * he[r][1];
                }

                double det_re = creal(A00 * A11 - A01 * A10);
                if (det_re < 1e-6 * N0 * N0) continue;
                cx_t det = det_re;
                cx_t id = 1.0 / det;
                double inv00 = creal( A11 * id);
                double inv11 = creal( A00 * id);

                double a0 = 1.0 - N0 * inv00;
                double a1 = 1.0 - N0 * inv11;
                if (a0 < 1e-6) a0 = 1e-6;
                if (a1 < 1e-6) a1 = 1e-6;
                if (a0 >= 1.0) a0 = 1.0 - 1e-6;
                if (a1 >= 1.0) a1 = 1.0 - 1e-6;

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
    (void)best_cap;
}
