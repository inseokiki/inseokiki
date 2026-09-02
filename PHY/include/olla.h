/* ================================================================
 *  olla.h
 *  Outer Loop Link Adaptation — ACK/NACK 기반 폐루프 MCS 오프셋 조정
 *
 *  기존 CQI/MCS 선택은 (근사) SNR→MCS 매핑 하나로 결정되는 개루프
 *  (open-loop) 방식이라, 그 매핑이 실제 채널/수신기 성능과 정확히
 *  맞지 않으면(항상 어느 정도 그렇다 — 실제 코드 성능은 Shannon
 *  한계에 못 미치고, 그 격차(SNR gap)는 코드/복호기별로 다름) 달성
 *  BLER이 목표에서 벗어난다. OLLA는 매 전송의 ACK/NACK(CRC 결과)를
 *  피드백으로 써서 SNR 오프셋을 누적 조정해 실제 BLER을 목표로
 *  수렴시키는 폐루프(closed-loop) 보정 — 3GPP가 알고리즘 자체를
 *  규정하지는 않지만(구현 정의) 업계 표준 관행(예: Dahlman et al.,
 *  *5G NR: The Next Generation Wireless Access Technology* 링크적응
 *  챕터에서 개념적으로 다룸 — CLAUDE.md 참고서적 목록에 이미 포함됨).
 *
 *  수렴 원리: ACK 시 offset += step_up, NACK 시 offset -= step_down.
 *  정상상태에서 ACK 비율 = step_down/(step_up+step_down)이 되므로,
 *  목표 BLER p에 대해 step_up = step_down · p/(1-p)로 두면 정확히
 *  BLER=p에서 평형(구간별 확률보행의 표준 결과 — 이 스텝 비율
 *  선택이 OLLA의 핵심).
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef OLLA_H
#define OLLA_H

#include "mcs_table.h"

typedef struct {
    double offset_db;      /* 누적 SNR 오프셋 [dB], 0에서 시작 */
    double bler_target;    /* 목표 BLER (0,1) */
    double step_up_db;     /* ACK(성공) 시 오프셋 증가량 [dB] */
    double step_down_db;   /* NACK(실패) 시 오프셋 감소량 [dB] */
} OLLAState;

/* step_up_db = step_down_db * bler_target/(1-bler_target)로 자동 계산 —
 * 정상상태 BLER이 정확히 bler_target으로 수렴하도록 하는 표준 OLLA 조건. */
void olla_init(OLLAState *s, double bler_target, double step_down_db);

/* ack=1(CRC 통과)이면 offset += step_up, ack=0(CRC 실패)이면 offset -= step_down. */
void olla_update(OLLAState *s, int ack);

/* Shannon 용량(η=log2(1+SNR)) + 구현 마진(gap_db, 실제 코드가 Shannon
 * 한계에 못 미치는 만큼의 근사 보정 — 구현 정의, 참고: Tse & Viswanath,
 * *Fundamentals of Wireless Communication*의 SNR gap 논의) 기준으로
 * effective_snr_db 이하에서 요구 SNR을 만족하는 가장 높은 인덱스의
 * MCS를 반환(모든 MCS가 초과 요구 시 0으로 폴백). 전 MCS 인덱스를
 * 전수 스캔한다 — TS 38.214 Table 5.1.3.1-1은 변조차수 전환 경계
 * (16QAM→64QAM)에서 스펙트럼 효율이 아주 미세하게 감소하는 지점이
 * 있어(실제 스펙 수치, 2026-09-01 확인) 인덱스 오름차순=효율 오름차순
 * 가정에 기반한 조기 종료는 쓰지 않는다. */
int olla_select_mcs(double effective_snr_db, double gap_db, MCSTableType table);

#endif
