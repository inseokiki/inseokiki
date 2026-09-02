/* ================================================================
 *  codebook_32port.h
 *  5G NR Type I Single Panel Codebook — 32 CSI-RS ports (N1=4,N2=4)
 *
 *  구성: N1=4, N2=4, Ng=2 (XPOL), O1=4, O2=4, P=32
 *  표준: TS 38.214 Sec 5.2.2.2.1, Table 5.2.2.2.1-2
 *
 *  이 구성은 순수 2D 안테나 배열(N1>1 AND N2>1)로, 지금까지의 4-port
 *  (N1=2,N2=1)/8-port(N1=4,N2=1) 1D 배열 코드북과는 빔 벡터 정의부터
 *  다르다 — u_m(수직 방향 DFT 성분)이 추가로 곱해진다.
 *
 *  "Massive MIMO(64안테나)" 요청에 대한 스펙 정합 대응: TS 38.214 Rel-18은
 *  Type I SP에서 64 CSI-RS 포트를 정의하지 않는다(§5.2.2.2.1 서두가
 *  4/8/12/16/24/32 포트만 나열, TS 38.211 Table 7.4.1.5.3-1도 최대 X=32).
 *  32포트가 스펙상 최댓값이며, (N1,N2)=(4,4)는 그중 유일하게 "정사각형"
 *  2D 배열인 조합(다른 하나는 (8,2)) — 편파당 16소자 × 2편파 = 32소자,
 *  총 32 CSI-RS 포트가 "64 물리 안테나 소자"에 가장 가깝게 대응한다.
 *  2026-08-31, 3gpp-server MCP로 Table 5.2.2.2.1-2/-3, 그리고 rank
 *  1~4 공식(Table 5.2.2.2.1-5/-6/-7/-8)을 이미지로 직접 확인해 아래
 *  구현에 반영. 상세 유도는 docs/analysis/history.md 참조.
 *
 *  빔 벡터 (§5.2.2.2.1 공식):
 *    u_m[k2] = e^{j2π·m·k2/(O2·N2)},  k2=0..N2-1        (N2원소 행벡터)
 *    v_{l,m}[n1·N2+k2] = e^{j2π·l·n1/(O1·N1)} · u_m[k2]  (N1·N2원소, n1=0..N1-1)
 *    ṽ_{l,m}[n1·N2+k2] = e^{j4π·l·n1/(O1·N1)} · u_m[k2]  ((N1/2)·N2원소, n1=0..N1/2-1)
 *      — rank 3/4(P≥16 분기)에서만 사용, l의 DFT 간격이 2배(빔 절반 개수)
 *    φ_n = e^{jπn/2},  θ_p = e^{jπp/4}
 *
 *  물리 포트 순서: v_{l,m}/ṽ_{l,m} 내부는 n1(수평, N1 방향)이 바깥,
 *  k2(수직, N2 방향)가 안쪽 — index = n1*N2+k2. 전체 P=32포트는
 *  [편파1: v_{l,m}(16개), 편파2: φ_n·v_{l,m}(16개)] (rank-1 기준).
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef CODEBOOK_32PORT_H
#define CODEBOOK_32PORT_H

#include "utils.h"

/* ── 배열/포트 파라미터 ──────────────────────────────────────────────── */
#define CB32_N1        4    /* 수평 안테나 수                              */
#define CB32_N2        4    /* 수직 안테나 수                              */
#define CB32_O1        4    /* 수평 오버샘플링 인수                        */
#define CB32_O2        4    /* 수직 오버샘플링 인수                        */
#define CB32_P         32   /* 총 CSI-RS 포트 수 = 2·N1·N2                 */

/* ── PMI 인덱스 범위 ──────────────────────────────────────────────────── */
#define CB32_L_COUNT    16  /* i1,1 범위 (rank 1/2) = N1·O1                */
#define CB32_M_COUNT    16  /* i1,2 범위 (전 rank)  = N2·O2                */
#define CB32_L34_COUNT  8   /* i1,1 범위 (rank 3/4) = N1·O1/2 (P≥16 분기)  */
#define CB32_I13_R2     4   /* rank-2 i1,3 범위 (Table 5.2.2.2.1-3, N1=N2) */
#define CB32_THETA_R34  4   /* rank 3/4 i1,3(=p) 범위, θ_p = e^{jπp/4}     */
#define CB32_I2_R1      4   /* rank-1 i2 범위, φ_n = e^{jπn/2}             */
#define CB32_I2_R234    2   /* rank 2/3/4 i2 범위 (Table -6/-7/-8: 0,1)    */

#define CB32_RANK1_TOTAL (CB32_L_COUNT   * CB32_M_COUNT * CB32_I2_R1)                  /* 1024 */
#define CB32_RANK2_TOTAL (CB32_L_COUNT   * CB32_M_COUNT * CB32_I13_R2  * CB32_I2_R234) /* 2048 */
#define CB32_RANK3_TOTAL (CB32_L34_COUNT * CB32_M_COUNT * CB32_THETA_R34 * CB32_I2_R234) /* 1024 */
#define CB32_RANK4_TOTAL CB32_RANK3_TOTAL                                              /* 1024 */

/* ── Rank-1 프리코더 (Table 5.2.2.2.1-5, codebookMode=1) ────────────────
 *
 * W = (1/√P) · [v_{l,m}; φ_n·v_{l,m}]   (32×1)
 *
 * 파라미터: i1_1=l (0..15), i1_2=m (0..15), i2=n (0..3)
 * 정규화: ||W||²=1
 */
void codebook_type1_sp_32port_rank1(int i1_1, int i1_2, int i2, cx_t W[32]);

/* ── Rank-2 프리코더 (Table 5.2.2.2.1-6, codebookMode=1) ────────────────
 *
 * W = 1/√(2P) · [v_{l,m},   v_{l',m'};
 *                φ_n·v_{l,m}, −φ_n·v_{l',m'}]   (32×2)
 *   l'=(l+k1) mod(N1·O1), m'=(m+k2) mod(N2·O2)
 *   (k1,k2) = Table 5.2.2.2.1-3의 "N1=N2" 열: i1_3=0→(0,0), 1→(O1,0),
 *             2→(0,O2), 3→(O1,O2)
 *
 * 파라미터: i1_1=l (0..15), i1_2=m (0..15), i1_3 (0..3), i2=n (0..1)
 * 정규화: 열당 ||W[:,c]||²=0.5, 총 1 (rank-1과 동일 총 송신전력)
 */
void codebook_type1_sp_32port_rank2(int i1_1, int i1_2, int i1_3, int i2, cx_t W[32][2]);

/* ── Rank-3/4 프리코더 (Table 5.2.2.2.1-7/-8, codebookMode="1-2", P≥16 분기) ──
 *
 * i1_1 범위가 rank1/2와 다름(0..7, N1·O1/2) — ṽ_{l,m}(길이 (N1/2)·N2=8)를
 * 사용해 4개 행 블록(총 4·8=32=P)으로 구성. i1_3=p는 θ_p 선택.
 *
 * Rank-3: W = 1/√(3P)·[ṽ,    ṽ,    ṽ;
 *                       θ_pṽ, −θ_pṽ, θ_pṽ;
 *                       φ_nṽ, φ_nṽ, −φ_nṽ;
 *                       φ_nθ_pṽ, −φ_nθ_pṽ, −φ_nθ_pṽ]           (32×3)
 * Rank-4: W = 1/√(4P)·[ṽ,    ṽ,    ṽ,    ṽ;
 *                       θ_pṽ, −θ_pṽ, θ_pṽ, −θ_pṽ;
 *                       φ_nṽ, φ_nṽ, −φ_nṽ, −φ_nṽ;
 *                       φ_nθ_pṽ, −φ_nθ_pṽ, −φ_nθ_pṽ, φ_nθ_pṽ]  (32×4)
 *
 * 파라미터: i1_1=l (0..7), i1_2=m (0..15), i1_3=p (0..3), i2=n (0..1)
 * 정규화: 열당 ||W[:,c]||²=1/rank, 총 1
 */
void codebook_type1_sp_32port_rank3(int i1_1, int i1_2, int i1_3, int i2, cx_t W[32][3]);
void codebook_type1_sp_32port_rank4(int i1_1, int i1_2, int i1_3, int i2, cx_t W[32][4]);

/* ── 코드북 전체 출력 (검증용: 정규화/직교성) ────────────────────────── */
void codebook_type1_sp_32port_print(int rank);

/* ── RI + PMI 동시 선택 (Rank Adaptation, rank 1~4 전수탐색) ────────────
 *
 * H[4][32] 채널과 N0 노이즈 분산으로 추정 Shannon 용량을 최대화하는
 * (rank, PMI) 조합을 rank 1~4 전 범위에서 전수탐색
 * (1024+2048+1024+1024 = 5120 후보). rank-1: 후-빔포밍 파워 기반,
 * rank≥2: MMSE 후-검출 SINR 합 기반(rank×rank Gramian 역행렬, 4-port/
 * 8-port와 동일 방식을 일반 N×N으로 확장).
 *
 * 출력: sel_rank(1~4)와 해당 rank의 PMI 인덱스. i1_3/i2는 rank에 따라
 * 의미가 다름(rank1: i1_3 미사용=0 고정, i2 범위 0~3 / rank2~4: i1_3
 * 사용, i2 범위 0~1) — codebook_type1_sp_32port_rankN() 각각의 파라미터
 * 규약을 그대로 따른다.
 */
void codebook_type1_sp_32port_ri_pmi_select(
    const cx_t H[4][32], double N0,
    int *sel_rank, int *sel_i1_1, int *sel_i1_2, int *sel_i1_3, int *sel_i2);

#endif
