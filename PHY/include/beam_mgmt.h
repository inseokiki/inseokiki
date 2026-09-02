/* ================================================================
 *  beam_mgmt.h
 *  빔 관리(Beam Management) — SSB/CSI-RS 기반 P1 절차(Tx 빔 스위핑)
 *
 *  TS 38.213 §8.5 / TS 38.214 §5.1.6.1의 P1 절차(gNB Tx 빔 스위핑 +
 *  UE Rx 빔 스위핑으로 최적 빔 쌍 탐색)를 단순화해 구현: UE Rx는
 *  단일 안테나(빔 스위핑 없음)로 스코프를 좁히고, gNB Tx 빔 스위핑만
 *  모델링한다(구현 정의 단순화 — 실제 UE Rx 빔 스위핑까지 포함하려면
 *  UE 측 배열/빔 코드북이 추가로 필요, 후속 과제로 분리).
 *
 *  후보 빔 집합: 이미 구현된 CL_32PORT Type I SP rank-1 코드북
 *  (`codebook_type1_sp_32port_rank1()`, TS 38.214 §5.2.2.2.1)의
 *  i1_1×i1_2×i2 전체 격자(16×16×4=1024, CB32_RANK1_TOTAL)를 그대로
 *  재사용한다 — i2(편파 위상)를 후보에서 빼면 참 채널의 편파 위상에
 *  따라 격자 위에서도 최대 이득이 1.0에 못 미치는 인위적 손실이
 *  생기므로(교차편파 위상이 안 맞으면 두 편파 그룹이 상쇄) 전체 3차원을
 *  탐색한다. gNB가 SSB/CSI-RS로 이 1024개 빔을 순차 송신하고, UE가 매
 *  빔마다 num_rep회 반복 관측한 L1-RSRP(수신전력)를 평균해 최댓값 빔을
 *  선택 — SSB 버스트 반복을 통한 측정 잡음 평균화를 모사한다.
 *
 *  "참 채널"(true channel) 모델: 단일 지배 경로(LOS) 가정, TS 38.214
 *  코드북과 동일한 array-manifold(steering vector) 형식을 격자점이
 *  아닌 연속값 l_true/m_true/n_true로 일반화해 생성(표준 안테나 어레이
 *  이론 — Tse & Viswanath; Björnson et al., *Massive MIMO Networks*,
 *  둘 다 프로젝트 참고서적. 코드북 자체의 DFT 격자 수식은 스펙 그대로,
 *  연속값 일반화만 구현 정의) — 이렇게 하면 참 방향이 코드북 격자에서
 *  벗어난 경우까지 포함해 빔 선택의 양자화 손실과 측정잡음 손실을
 *  분리해 평가할 수 있다.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef BEAM_MGMT_H
#define BEAM_MGMT_H

#include "utils.h"

#define BM_CAND_L    16   /* i1_1 격자 크기 (CB32_L_COUNT 재사용) */
#define BM_CAND_M    16   /* i1_2 격자 크기 (CB32_M_COUNT 재사용) */
#define BM_CAND_N2   4    /* i2 격자 크기 (CB32_I2_R1 재사용) */

/* 연속값 (l_true, m_true, n_true) 방향의 32소자 참 채널(LOS steering
 * vector, 단일 Rx 안테나) 생성 — codebook_32port.h의 v_{l,m} 공식을
 * 정수 격자가 아닌 실수값으로 일반화. ||H_true||²=1로 정규화. */
void beam_mgmt_true_channel(double l_true, double m_true, double n_true, cx_t H_true[32]);

/* P1 절차: 1024개 후보 빔(i1_1=0..15, i1_2=0..15, i2=0..3, rank-1
 * 코드북 전체)을 순차 송신, 빔마다 num_rep회 독립 잡음으로
 * RSRP=|H_true^H·W_c|²·P_tx+n 관측 후 평균해 최댓값 빔 선택.
 * P_tx=1(정규화), N0=잡음분산. 출력: 선택된 (i1_1,i1_2,i2)와 그 순간의
 * 노이즈 없는 진짜 빔포밍 이득 |H_true^H·W_sel|² (하향 데이터 전송에
 * 사용할 값). */
void beam_mgmt_p1_sweep(const cx_t H_true[32], double N0, int num_rep,
                         int *sel_i1_1, int *sel_i1_2, int *sel_i2, double *sel_gain);

/* Genie 기준선: 측정 잡음 없이 1024개 후보 전체를 노이즈 없는 진짜
 * 채널 이득으로 전수탐색해 격자 위에서의 진짜 최적 빔을 찾는다
 * (양자화 손실만 반영, 측정잡음 손실은 없음 — P1 결과와 비교해
 * 두 손실 성분을 분리하는 기준선). */
void beam_mgmt_genie_best(const cx_t H_true[32], int *best_i1_1, int *best_i1_2, int *best_i2, double *best_gain);

#endif
