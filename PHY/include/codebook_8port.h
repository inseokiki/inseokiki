/* ================================================================
 *  codebook_8port.h
 *  5G NR Type I Single Panel Codebook — 8 CSI-RS ports (N1=4,N2=1)
 *
 *  구성: N1=4, N2=1, Ng=2 (XPOL), O1=4, O2=1, P=8
 *  표준: TS 38.214 Sec 5.2.2.2.1, Table 5.2.2.2.1-2 (P=8 → (N1,N2)=(4,1),
 *        (O1,O2)=(4,1) 확인됨 — 같은 P=8에 (N1,N2)=(2,2) 대안도 있으나
 *        1D 선형 배열(N2=1) 가정인 이 프로젝트에는 (4,1) 쪽 적용)
 *
 *  기존 4-port(codebook.c/.h, N1=2)의 rank-1 구조를 N1=4로 그대로
 *  일반화한 것 — 빔 벡터/코피에이징 정의가 N1에 의존하지 않는 형태라
 *  일반화에 별도 가정이 필요 없음(rank1_derivation.md 참조).
 *
 *  rank-2: TS 38.214 Table 5.2.2.2.1-3(i1,3 → k1,k2 매핑)의 실제 k1 값이
 *    로컬 docx 문서에서는 Equation Editor 3.0 OLE 개체로 막혀 있었으나,
 *    3GPP MCP 서버(공식 스펙 렌더링본, 수식이 뷰어블 PNG로 제공됨)로
 *    2026-08-31 확정: N1>2, N2=1 그룹에서 k1 = i1,3 · O1, k2 = 0
 *    (i1,3 = 0,1,2,3 → k1 = 0, O1, 2·O1, 3·O1). N1·O1/2 가설은 틀렸음
 *    (4-port(N1=2)는 i1,3∈{0,1}만 유효해 두 가설이 우연히 같은 값을 줬을
 *    뿐). Table 5.2.2.2.1-6 codebookMode=1 원식도 함께 확인:
 *    W = 1/√(2·P) · [v_l, v_l'; φ_n·v_l, −φ_n·v_l'],  l'=l+k1, n=i2.
 *    상세는 docs/analysis/history.md 참조.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef CODEBOOK_8PORT_H
#define CODEBOOK_8PORT_H

#include "utils.h"

/* ── PMI 인덱스 범위 (N1=4, O1=4 기준) ────────────────────────────────── */
#define CB8_N1          4    /* 1차원 안테나 수                           */
#define CB8_O1          4    /* 1차원 오버샘플링 인수                     */
#define CB8_BEAMS       16   /* 총 빔 수 = N1 * O1                        */
#define CB8_COPHASE     4    /* 코피에이징 수 = 4 (φ_n = e^{jπn/2})      */
#define CB8_RANK1_TOTAL 64   /* rank-1 코드워드 총 수 = 16 × 4            */
#define CB8_I13_COUNT   4    /* i1,3 유효값 수 (N1>2,N2=1: k1=i1,3·O1)    */
#define CB8_RANK2_TOTAL 256  /* rank-2 코드워드 총 수 = 16(i1_1) × 4(i1_3) × 4(i2) */

/* ── Rank-1 프리코더 ─────────────────────────────────────────────────────
 *
 * W = (1/√8) · [v_{i1_1}; φ_{i2} · v_{i1_1}]   (8×1 복소 벡터)
 *
 * v_l = [1, e^{j2πl/16}, e^{j4πl/16}, e^{j6πl/16}]^T  (4원소, 미정규화)
 * φ_n = e^{jπn/2}
 *
 * 파라미터:
 *   i1_1 : 빔 인덱스,      0 ≤ i1_1 ≤ CB8_BEAMS-1 = 15
 *   i2   : 코피에이징 인덱스, 0 ≤ i2   ≤ CB8_COPHASE-1 = 3
 *   W    : 출력 8×1 열 벡터 (에너지 정규화: ||W||² = 1)
 */
void codebook_type1_sp_8port_rank1(int i1_1, int i2, cx_t W[8]);

/* ── Rank-2 프리코더 ─────────────────────────────────────────────────────
 *
 * TS 38.214 Table 5.2.2.2.1-6 (codebookMode=1):
 *   W = 1/√(2·P) · [v_l, v_l'; φ_n·v_l, −φ_n·v_l']   (8×2 복소 행렬)
 *   l  = i1_1                       (0 ≤ i1_1 ≤ CB8_BEAMS-1 = 15)
 *   l' = (l + k1) mod (N1·O1),  k1 = i1_3 · O1  (Table 5.2.2.2.1-3, N1>2,N2=1)
 *   n  = i2                         (0 ≤ i2 ≤ CB8_COPHASE-1 = 3)
 *
 * 파라미터:
 *   i1_1 : 기준 빔 인덱스, 0 ≤ i1_1 ≤ 15
 *   i1_3 : 빔 오프셋 선택,  0 ≤ i1_3 ≤ CB8_I13_COUNT-1 = 3
 *   i2   : 코피에이징 인덱스, 0 ≤ i2 ≤ 3
 *   W    : 출력 8×2 복소 행렬 (열 정규화: ||W[:,0]||²=||W[:,1]||²=0.5,
 *          총 ||W||²_F = 1, rank-1과 동일 총 송신전력)
 */
void codebook_type1_sp_8port_rank2(int i1_1, int i1_3, int i2, cx_t W[8][2]);

/* ── 코드북 전체 출력 (검증용) ────────────────────────────────────────── */
void codebook_type1_sp_8port_print(int rank);

/* ── Rank-1 PMI Exhaustive Search ────────────────────────────────────── */
void codebook_type1_sp_8port_pmi_search(const cx_t H[][8], int num_rx,
                                         int *best_i1, int *best_i2,
                                         double *max_power);

/* ── RI + PMI 동시 선택 (Rank Adaptation) ──────────────────────────────
 *
 * H[4][8] 채널과 N0 노이즈 분산으로 추정 Shannon 용량을 최대화하는
 * (rank, PMI) 조합을 전체 탐색(rank-1 64 + rank-2 256 = 320 후보).
 * 4-port codebook_type1_sp_4port_ri_pmi_select()와 동일한 방식(rank-1:
 * 후-빔포밍 파워 기반, rank-2: MMSE 후-검출 SINR 합 기반), 8-port 인덱스
 * 범위(i1_1: 0~15, i1_3: 0~3)로 확장.
 *
 * 출력 (선택된 결과 + rank-1·rank-2 각각의 최적도 함께 반환):
 *   sel_rank/sel_i1_1/sel_i1_3/sel_i2 : 최종 선택
 *   r1_i1_1/r1_i2                     : rank-1 범위 내 최적
 *   r2_i1_1/r2_i1_3/r2_i2             : rank-2 범위 내 최적
 */
void codebook_type1_sp_8port_ri_pmi_select(
    const cx_t H[4][8], double N0,
    int *sel_rank, int *sel_i1_1, int *sel_i1_3, int *sel_i2,
    int *r1_i1_1, int *r1_i2,
    int *r2_i1_1, int *r2_i1_3, int *r2_i2);

#endif
