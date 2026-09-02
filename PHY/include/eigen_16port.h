/* ================================================================
 *  eigen_16port.h
 *  Eigen-beamforming (SVD 기반) 프리코딩 — 16 Tx 포트, 비-코드북
 *
 *  CL_4PORT/8PORT/32PORT(codebook.c/codebook_8port.c/codebook_32port.c)는
 *  TS 38.214 Type I SP 코드북(양자화된 유한 후보 집합)에서 최적 PMI를
 *  탐색하는 폐루프(closed-loop) 방식이다. 이 모듈은 그와 대비되는
 *  개루프(open-loop)/reciprocity 기반 방식 — 코드북 양자화 없이 채널
 *  H의 SVD(H=U·Σ·V^H)에서 직접 우특이벡터(V)를 프리코더로 쓰는 고전적
 *  eigen-beamforming(고유빔포밍)이다. 실제 TDD gNB는 SRS 채널 상호성
 *  (reciprocity)으로 H를 직접 추정해 이 방식을 쓸 수 있다 — 이 프로젝트
 *  에서는 다른 모듈과 동일하게 genie-aided CSI(완전한 H 인지)로 가정한다.
 *
 *  참고서적: Tse & Viswanath, *Fundamentals of Wireless Communication*
 *  (SVD 기반 병렬 부채널 분해 및 rank-adaptive 등력 할당 챕터), CLAUDE.md
 *  참고서적 목록에 이미 포함됨. 3GPP 표준이 규정하는 "코드북"이 아니라
 *  신호처리 알고리즘 자체이므로 3GPP 절/표 인용 대상이 아니다.
 *
 *  핵심 성질: H·v_i = σ_i·u_i (SVD 정의) 이므로 프리코더로 V의 처음
 *  r개 열(상위 r개 특이값에 대응)을 쓰면 유효 채널 H·V[:,0:r]는 자동으로
 *  서로 직교(U의 열이 정규직교이므로)한다 — 즉 스트림 간 간섭이 이미
 *  프리코딩 단계에서 제거된다(코드북 방식과 달리 이 직교성이 설계에
 *  의해 보장됨). 검출은 CL_32PORT와 동일한 기존 검출기(mrc_combine_4rx/
 *  mimo_mmse_detect_4rx2/4rx3/4x4)를 그대로 재사용 — 유효 채널이 대각에
 *  가까우므로 특히 잘 조건화됨.
 *
 *  Rx는 4안테나 고정(기존 프로젝트 관례와 동일) → rank(스트림 수) 최대
 *  min(Tx,Rx)=4. 등력 분배(각 활성 스트림에 총 송신전력을 균등 분배,
 *  코드북 방식의 "열당 1/rank 전력" 관례와 동일) 가정 하에 rank
 *  적응 선택 — water-filling(비균등 최적 전력 할당)은 이번 범위에서
 *  제외(참고서적에 나오는 더 정교한 별도 최적화, 후속 과제로 남김).
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef EIGEN_16PORT_H
#define EIGEN_16PORT_H

#include "utils.h"

#define EIG16_TX 16   /* Tx 포트 수 */
#define EIG16_RX 4    /* Rx 안테나 수 (기존 관례 고정) */

/* ── SVD 계산 (경량 Gram-행렬 방식) ──────────────────────────────────────
 *
 * H(4×16)는 Rx<<Tx이므로 표준 4×16 SVD 대신 4×4 Gram 행렬 A=H·H^H의
 * Hermitian 고유분해로 축소해 계산한다:
 *   A = H·H^H = U·Σ²·U^H   (고유값 λ_i=σ_i², 고유벡터 u_i)
 *   v_i = H^H·u_i / σ_i     (σ_i>0인 i에 대해, SVD 정의 H^H u_i = σ_i v_i)
 *
 * 출력은 특이값 내림차순(σ_0≥σ_1≥σ_2≥σ_3≥0)으로 정렬된다.
 *   sigma[4] : 특이값 (내림차순)
 *   U[4][4]  : 좌특이벡터(열이 정규직교, Rx측)
 *   V[16][4] : 우특이벡터(열이 정규직교, Tx측 — 프리코더 후보)
 */
void eigen_bf_16port_svd(const cx_t H[4][16], double sigma[4], cx_t U[4][4], cx_t V[16][4]);

/* ── Rank 적응 선택 (등력 분배 기준) ─────────────────────────────────────
 *
 * r=1..4 중 sum_{i=0}^{r-1} log2(1+σ_i²·(1/r)/N0) (총 송신전력 1을 r개
 * 스트림에 균등 분배했을 때의 추정 용량)를 최대화하는 r을 선택한다.
 * water-filling이 아닌 등력 분배 — codebook_type1_sp_32port_rankN()의
 * "열당 1/rank 전력" 관례와 동일한 가정이라 CL_32PORT와 직접 비교 가능.
 */
void eigen_bf_16port_select_rank(const double sigma[4], double N0, int *sel_rank);

#endif
