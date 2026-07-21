/* ================================================================
 *  codebook.h
 *  5G NR Type I Single Panel Codebook — 4 CSI-RS ports
 *
 *  구성: N1=2, N2=1, Ng=2 (XPOL), O1=4, O2=1, P=4
 *  표준: TS 38.214 Sec 5.2.2.2.1
 *
 *  포트 순서: [pol1_ant0, pol1_ant1, pol2_ant0, pol2_ant1]
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef CODEBOOK_H
#define CODEBOOK_H

#include "utils.h"

/* ── PMI 인덱스 범위 (N1=2, O1=4 기준) ────────────────────────────────── */
#define CB_N1          2    /* 1차원 안테나 수                           */
#define CB_O1          4    /* 1차원 오버샘플링 인수                     */
#define CB_BEAMS       8    /* 총 빔 수 = N1 * O1                        */
#define CB_COPHASE     4    /* 코피에이징 수 = 4 (φ_n = e^{jπn/2})      */
#define CB_RANK1_TOTAL 32   /* rank-1 코드워드 총 수 = 8 × 4             */

/* ── Rank-1 프리코더 ─────────────────────────────────────────────────────
 *
 * W = (1/2) · [v_{i1_1}; φ_{i2} · v_{i1_1}]   (4×1 복소 벡터)
 *
 * v_l = [1, e^{j·2π·l/(N1·O1)}]^T = [1, e^{jπl/4}]^T  (미정규화, 각 원소 단위 크기)
 * φ_n = e^{jπn/2}
 *
 * 파라미터:
 *   i1_1 : 빔 인덱스,      0 ≤ i1_1 ≤ CB_BEAMS-1 = 7
 *   i2   : 코피에이징 인덱스, 0 ≤ i2   ≤ CB_COPHASE-1 = 3
 *   W    : 출력 4×1 열 벡터 (에너지 정규화: ||W||² = 1)
 */
void codebook_type1_sp_4port_rank1(int i1_1, int i2, cx_t W[4]);

/* ── Rank-2 프리코더 ─────────────────────────────────────────────────────
 *
 * i1_3 = 0  — 빔쌍 다이버시티 (직교 빔 l과 l' = l+4):
 *   W = (1/2) · [v_l, v_{l'}; φ_{n1}·v_l, φ_{n2}·v_{l'}]   (4×2)
 *   i1_1 ∈ {0,...,3},  l' = (l+4) mod 8
 *   i2 → (φ_{n1}, φ_{n2}): 0→(1,1), 1→(1,j), 2→(1,−1), 3→(1,−j)
 *
 * i1_3 = 1  — 동일빔 교차편파 다이버시티 (같은 빔, 반대 코피에이징):
 *   W = (1/2) · [v_l, v_l; φ_n·v_l, −φ_n·v_l]   (4×2)
 *   i1_1 ∈ {0,...,7}
 *   i2 → φ_n = e^{jπi2/2}
 *
 * 직교성: W[:,0]^H W[:,1] = 0  (두 변형 모두 수학적으로 보장됨)
 * 정규화: ||W[:,0]||² = ||W[:,1]||² = 1
 *
 * 파라미터:
 *   i1_1 : 빔 인덱스 (i1_3=0: 0~3, i1_3=1: 0~7)
 *   i1_3 : 빔 쌍 변형 선택 (0 또는 1)
 *   i2   : 코피에이징 인덱스 0~3
 *   W    : 출력 W[port][layer], 4×2 복소 행렬
 */
void codebook_type1_sp_4port_rank2(int i1_1, int i1_3, int i2, cx_t W[4][2]);

/* ── 코드북 전체 출력 (검증용) ───────────────────────────────────────────
 *
 * rank=1: 모든 (i1_1, i2) 조합 32개 출력, ||W||² 및 빔각도 표시
 * rank=2: 모든 (i1_1, i1_3, i2) 조합 출력, ||W[:,j]||² 및 직교성 검증
 */
void codebook_type1_sp_4port_print(int rank);

/* ── 최적 PMI 탐색 (exhaustive search, rank 1) ──────────────────────────
 *
 * 입력 채널 행렬 H[rx][tx] (rx × 4) 에 대해 수신 SNR을 최대화하는
 * (i1_1, i2) 쌍을 탐색.
 *
 * 기준: 최대 후-빔포밍 수신 신호 파워 Σ_r |h_r · W|² (rank-1 MRT 기준)
 *
 * 파라미터:
 *   H        : 채널 행렬, H[rx][4], rx 안테나 수
 *   num_rx   : Rx 안테나 수
 *   best_i1  : 출력 — 최적 i1_1
 *   best_i2  : 출력 — 최적 i2
 *   max_power: 출력 — 최적 수신 파워
 */
void codebook_type1_sp_4port_pmi_search(const cx_t H[][4], int num_rx,
                                         int *best_i1, int *best_i2,
                                         double *max_power);

/* ── RI + PMI 동시 선택 (Rank Adaptation) ──────────────────────────────
 *
 * H[4][4] 채널과 N0 노이즈 분산으로 추정 Shannon 용량을 최대화하는
 * (rank, PMI) 조합을 전체 탐색(80 후보).
 *
 * 선택 기준:
 *   Rank-1: C₁(l,n) = log₂(1 + ||H·W||²/N₀)
 *   Rank-2: C₂(l,k,n) = Σ_{j} log₂(1 + α_j/(1−α_j))
 *           α_j = 1 − N₀·Re{(A⁻¹)_jj},  A = H_eff^H H_eff + N₀·I
 *
 * 출력 (선택된 결과 + rank-1·rank-2 각각의 최적도 함께 반환):
 *   sel_rank  : 선택된 rank (1 또는 2)
 *   sel_i1_1  : 선택된 i1_1 (빔 인덱스)
 *   sel_i1_3  : 선택된 i1_3 (rank-1이면 항상 0)
 *   sel_i2    : 선택된 i2  (코피에이징)
 *   r1_i1_1   : rank-1 범위 내 최적 i1_1
 *   r1_i2     : rank-1 범위 내 최적 i2
 *   r2_i1_1   : rank-2 범위 내 최적 i1_1
 *   r2_i1_3   : rank-2 범위 내 최적 i1_3
 *   r2_i2     : rank-2 범위 내 최적 i2
 */
void codebook_type1_sp_4port_ri_pmi_select(
    const cx_t H[4][4], double N0,
    int *sel_rank, int *sel_i1_1, int *sel_i1_3, int *sel_i2,
    int *r1_i1_1, int *r1_i2,
    int *r2_i1_1, int *r2_i1_3, int *r2_i2);

#endif
