/* ================================================================
 *  mumimo.h
 *  MU-MIMO 하향링크 — Zero-Forcing Beamforming (ZF-BF)
 *
 *  기존 SM_2X2/SM_4X4/CL_XPORT는 전부 SU-MIMO(단일 사용자가 여러
 *  스트림/레이어를 받는 공간 다중화)다. 이 모듈은 그와 대비되는
 *  MU-MIMO(Multi-User MIMO) — gNB가 같은 시간-주파수 자원에서
 *  서로 다른 여러 사용자(UE)에게 공간적으로 분리된 스트림을
 *  동시에 서비스한다.
 *
 *  스코프(1차 구현): gNB Nt=4 안테나, K=2 사용자, 각 사용자 1개
 *  Rx 안테나(MU-MISO 다운링크 — 실무·교과서에서 가장 흔한 MU-MIMO
 *  기본형). 사용자별 다중 스트림/랭크 적응은 후속 과제로 분리
 *  (tasks/todo.md 참조).
 *
 *  프리코딩: Zero-Forcing Beamforming — H(K×Nt)의 우측 유사역행렬
 *  H^+ = H^H(HH^H)^-1을 프리코더로 써서 사용자 간 간섭을 설계상
 *  정확히 제거한다(잡음 없는 이상적 경우 H·H^+=I_K). 열마다
 *  ||W[:,k]||²=1/K로 정규화(등력 분배, 이 프로젝트의 다른 다중
 *  스트림 프리코더들과 동일한 관례) — 이 스케일링은 실수 양수라
 *  간섭 제거 성질을 그대로 보존한다(H[j]·W[:,k]=0, j≠k는 변하지
 *  않음, k=j인 유효 채널만 실수 양수 스칼라로 스케일됨).
 *  표준 교과서 기법(Tse & Viswanath; Björnson et al., *Massive
 *  MIMO Networks* — 둘 다 프로젝트 참고서적) — 3GPP가 프리코더
 *  알고리즘 자체를 규정하지 않으므로 구현 정의.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef MUMIMO_H
#define MUMIMO_H

#include "utils.h"

#define MUMIMO_NT 4   /* gNB Tx 안테나 수 */
#define MUMIMO_K  2   /* 동시 서빙 사용자 수 (각 1 Rx 안테나) */

/* i.i.d. Rayleigh 채널: H[k][t], k=0..K-1(사용자), t=0..Nt-1(gNB 안테나) */
void mumimo_channel_draw(cx_t H[MUMIMO_K][MUMIMO_NT]);

/* ZF-BF 프리코더: W[t][k] (Nt×K). H[k]·W[:,k] = 실수 양수 유효채널
 * 이득(스칼라), H[j]·W[:,k]≈0 (j≠k, 노이즈 없는 이상적 경우) — 자세한
 * 유도는 위 파일 헤더 주석 참조. K×K Gramian이 특이(사용자 채널이
 * 거의 같은 방향)에 가까우면 유효채널 이득이 매우 작아질 수 있음
 * (물리적으로 타당한 ZF의 한계 — 방어적으로 특이시 0벡터 반환). */
void mumimo_zf_precode(const cx_t H[MUMIMO_K][MUMIMO_NT], cx_t W[MUMIMO_NT][MUMIMO_K]);

#endif
