/* ================================================================
 *  ul_power_ctrl.h
 *  UL Closed-Loop Power Control simulation
 *  TS 38.213 §7.2.1 PUSCH Power Control
 *
 *  전력 공식:
 *    P_tx(i) = min(P_CMAX, P_0 + α·PL + f(i))  [dBm]
 *
 *  f(i)는 TPC 누산값:  f(i) = f(i-1) + δ_TPC
 *  δ_TPC ∈ {-1, 0, +1, +3} dB  (TS 38.213 Table 7.2.1-1)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef UL_POWER_CTRL_H
#define UL_POWER_CTRL_H

#include "config_parser.h"

/* UL CLPC 시뮬레이션:
 *   Part 1 — 시계열 (PL_VAR_STD_DB=0이면 고정 PL, 아니면 Gauss-Markov 시변
 *            PL로 채널 페이딩/이동성 근사): P_tx, SINR, f(i) 수렴 과정 출력
 *   Part 2 — PL 스윕 (정상상태): OL vs CL 비교, P_CMAX 클램핑 가시화
 *            (PL 범위는 cfg->snrStart ~ snrEnd 재사용) */
void run_ulpc_simulation(const L1Config *cfg);

#endif
