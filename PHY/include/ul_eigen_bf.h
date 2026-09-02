/* ================================================================
 *  ul_eigen_bf.h
 *  UL SIMO 수신 빔포밍 — 공간 공분산 EVD(전력반복법) 기반, 비-코드북
 *
 *  UE 1 Tx 안테나 → gNB UL_EIGEN_NRX(=16) Rx 안테나. DL EIGEN_16PORT의
 *  Tx측 SVD 빔포밍과 대칭되는 Rx측 구현 — "코드북(PMI 양자화) 대신
 *  실제 채널로 빔을 만든다"는 같은 철학을 수신 방향에 적용(사용자
 *  지적: UL Rx는 코드북이 아니라 빔포머를 써야 함).
 *
 *  UE가 1개 안테나뿐이라 채널 h(16×1)는 항상 rank-1이므로, 잡음 없는
 *  순간 채널 하나만 놓고 보면 "고유빔포밍"은 수학적으로 MRC와 완전히
 *  동일하다(h h^H의 유일한 0이 아닌 고유벡터가 h 자신이므로). 그래서
 *  이 모듈은 순간 채널이 아니라 "여러 파일럿 관측(각각 잡음 포함)의
 *  공간 공분산 R=(1/M)Σ h_est_m h_est_m^H"에서 지배적 고유벡터를
 *  뽑는다 — 잡음은 16차원 전체에 고르게 퍼지는 반면 신호는 h 방향에만
 *  실리므로, M개 관측을 평균한 공분산의 지배적 고유벡터는 파일럿 하나만
 *  쓴 순간 채널 추정치보다 잡음에 더 강건한 수신 빔이 된다 — 이 지점이
 *  순간 MRC와 실질적으로 달라지는 부분(실무의 SRS 기반 공간 공분산
 *  추정과 같은 개념, Björnson et al. *Massive MIMO Networks* 근거,
 *  3GPP가 수신기 알고리즘을 규정하지 않으므로 구현 정의).
 *
 *  16×16 Hermitian 행렬의 전체 고유분해 대신, 필요한 건 지배적
 *  고유벡터 1개뿐이므로 전력반복법(power iteration)을 사용 — 전체
 *  스펙트럼이 필요했던 EIGEN_16PORT의 4×4 Jacobi EVD와 다른 접근.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef UL_EIGEN_BF_H
#define UL_EIGEN_BF_H

#include "utils.h"

#define UL_EIGEN_NRX 16   /* gNB Rx 안테나 수 (DL EIGEN_16PORT 포트 수와 대칭) */

/* UE 1 Tx -> gNB UL_EIGEN_NRX Rx, i.i.d. Rayleigh SIMO 채널 */
void ul_eigen_channel_draw(cx_t h[UL_EIGEN_NRX]);

/* num_obs개의 잡음 섞인 파일럿 채널 관측(각 UL_EIGEN_NRX차원, LS 추정치
 * h_est_obs[m] = h + noise_m, 이미 파일럿 심볼로 역회전된 상태)에서
 * 표본 공간공분산 R=(1/num_obs)Σ h_est_obs[m]·h_est_obs[m]^H을 만들고
 * 전력반복법으로 지배적 고유벡터를 뽑아 단위벡터 수신 빔 w_out에 저장. */
void ul_eigen_beamform(const cx_t h_est_obs[][UL_EIGEN_NRX], int num_obs, cx_t w_out[UL_EIGEN_NRX]);

/* ── UE 2 Tx(레이어) 확장 ────────────────────────────────────────────────
 *
 * UE가 2개 안테나(레이어)로 동시 전송하면 채널 H는 UL_EIGEN_NRX×2
 * 행렬이 되어 더 이상 rank-1이 아니다 — 진짜 SVD가 필요해지는 지점.
 *
 *  - Genie 경로: `ul_eigen_svd_genie()`가 완전한 채널 지식으로 H의
 *    좌특이벡터 U(UL_EIGEN_NRX×2, 직교정규)를 정확히 계산 — H^H H
 *    (2×2 Hermitian Gram 행렬)를 닫힌 형식으로 고유분해(2×2라 반복법
 *    불필요, 직접 공식)한 뒤 U_k = H·v_k/σ_k. U는 H의 열공간(신호
 *    부분공간)을 정확히 张 span하므로, U로 16차원 수신신호를 2차원에
 *    투영해도 신호 에너지 손실이 전혀 없다(잡음만 있는 14차원을
 *    버리는 것과 동일) — 이후 2×2 ZF/MMSE(`mimo_zf_detect`/
 *    `mimo_mmse_detect` 재사용)와 결합하면 완전한 채널지식 기준
 *    최적 선형 수신기와 동치.
 *  - Eigen-BF(현실) 경로: 레이어마다 독립적으로 FDM 파일럿 관측 →
 *    `ul_eigen_beamform()`을 레이어별로 2회 호출해 w_A, w_B(각각
 *    잡음 섞인 공분산 기반 추정치)를 얻은 뒤, `ul_eigen_orthogonalize2()`
 *    로 그람-슈미트 직교화해 Q=[w_A, w_B_perp](직교정규 기저)를
 *    구성 — 직교화해야 결합 후 잡음이 정확히 N0·I가 되어 기존
 *    2×2 검출기를 수정 없이 재사용할 수 있다(w_A, w_B가 직교하지
 *    않으면 결합 잡음의 공분산이 N0·I가 아니게 되어 표준 ZF/MMSE
 *    가정이 깨짐).
 */

/* H(UL_EIGEN_NRX×2)의 좌특이벡터 U(UL_EIGEN_NRX×2, 직교정규)와 특이값
 * sigma[0]>=sigma[1]>=0을 완전한 채널 지식으로 정확히 계산(2×2 Gram
 * 행렬의 닫힌 형식 고유분해) — sigma는 랭크 적응(등력분배 기준 추정
 * 용량 비교)에 사용. */
void ul_eigen_svd_genie(const cx_t H[UL_EIGEN_NRX][2], cx_t U[UL_EIGEN_NRX][2], double sigma[2]);

/* w1(단위노름 가정)에 대해 w2_inout을 그람-슈미트 직교화하고 재정규화
 * (w1은 그대로 둠) — in-place 수정. */
void ul_eigen_orthogonalize2(const cx_t w1[UL_EIGEN_NRX], cx_t w2_inout[UL_EIGEN_NRX]);

/* ── UE K Tx(레이어, K≤4) 확장(2026-09-02) ──────────────────────────────
 *
 * 2-Tx 설계를 4×4로 일반화 — UE가 최대 4개 레이어로 동시 전송하면
 * 채널 H는 UL_EIGEN_NRX×4가 되고, 4×4 Gram 행렬 H^H H의 완전한
 * 스펙트럼이 필요해진다(2×2와 달리 닫힌 형식이 없어 반복법 필요) —
 * DL EIGEN_16PORT가 이미 검증한 Cyclic Jacobi 4×4 Hermitian
 * 고유분해(`herm4x4_eig()`, utils.h로 추출해 두 모듈이 공유)를
 * 그대로 재사용한다.
 *
 *  - Genie 경로: `ul_eigen_svd_genie4()`가 좌특이벡터 U(UL_EIGEN_NRX×4,
 *    직교정규)와 특이값 sigma[4](내림차순)를 계산 — 2-Tx
 *    `ul_eigen_svd_genie()`와 동일한 원리(U_k = H v_k / sigma_k),
 *    Gram/고유분해만 4×4로 확장.
 *  - Eigen-BF(현실) 경로: 레이어마다 독립적으로 `ul_eigen_beamform()`을
 *    4회 호출해 얻은 w0..w3를 `ul_eigen_orthogonalize4()`로 순차
 *    그람-슈미트 직교화(2-Tx `ul_eigen_orthogonalize2()`의 K=4 일반화 —
 *    w[k]를 이미 직교화된 w[0..k-1]에 대해 차례로 투영 제거).
 *  - 랭크 적응: DL EIGEN_16PORT의 `eigen_bf_16port_select_rank()`를
 *    그대로 재사용(등력분배 기준 추정 용량, r=1..4) — 신호가 이미
 *    (sigma[4], N0, *sel_rank)로 동일해 새 함수가 필요 없다.
 *  - 검출: 4×rank 유효채널이 항상 4차원 투영 수신공간을 가지므로
 *    CL_32PORT와 동일한 검출기 계열(mrc_combine_4rx/mimo_mmse_detect_
 *    4rx2/4rx3/4x4)을 재사용(신규 검출 코드 없음) — CL_32PORT 선례를
 *    따라 등화기 선택 없이 MMSE 고정(ZF는 rank<4 사각 시스템에 대해
 *    이 프로젝트에 미구현).
 */

/* H(UL_EIGEN_NRX×4)의 좌특이벡터 U(UL_EIGEN_NRX×4, 직교정규)와 특이값
 * sigma[0]>=...>=sigma[3]>=0을 완전한 채널 지식으로 계산 — 4×4 Gram
 * 행렬(H^H H)을 `herm4x4_eig()`로 고유분해한 뒤 U_k = H·v_k/sigma_k
 * (sigma_k>0인 k에 대해). sigma는 랭크 적응(`eigen_bf_16port_select_
 * rank()`)에 사용. */
void ul_eigen_svd_genie4(const cx_t H[UL_EIGEN_NRX][4], cx_t U[UL_EIGEN_NRX][4], double sigma[4]);

/* w[0..3](각 UL_EIGEN_NRX차원)를 순차 그람-슈미트 직교화 — w[0]은
 * 그대로 두고, w[k](k=1..3)는 이미 처리된 w[0..k-1]에 대한 투영을
 * 차례로 제거한 뒤 재정규화한다(in-place). `ul_eigen_orthogonalize2()`의
 * K=4 일반화. */
void ul_eigen_orthogonalize4(cx_t w[4][UL_EIGEN_NRX]);

#endif
