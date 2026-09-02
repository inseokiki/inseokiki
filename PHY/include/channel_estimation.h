/* ================================================================
 *  channel_estimation.h
 *  LS/MMSE/DFT-based channel estimation, interpolation, ZF/MMSE equalization
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef CHANNEL_ESTIMATION_H
#define CHANNEL_ESTIMATION_H

#include "utils.h"

/* LS estimate: h_out[n] = rx_pilots[n] / tx_pilots[n] */
void ls_estimate(const cx_t *rx, const cx_t *tx, int n, cx_t *h_out);

/* Linear freq-domain interpolation.  pilot_pos[] sorted ascending.
   h_out[num_active_sc] must be pre-allocated. */
void interpolate_channel(const cx_t *h_pilots, int num_pilots,
                         const int *pilot_pos,
                         int num_active_sc, cx_t *h_out);

/* ── MMSE(Wiener) 채널추정 — 파일럿 도메인 LMMSE 스무딩 ──────────────────
 *
 * LS 추정치(잡음 포함)를 파일럿 위치들의 주파수 상관행렬 R_hh로 웨너
 * 필터링해 잡음을 줄인다: H_mmse = R_hh · (R_hh + N0·I)⁻¹ · H_ls
 *
 * R_hh 모델: 지수 PDP(power-delay-profile)의 주파수 상관 — 표준 결과
 *   ρ(Δf) = 1 / (1 + j·2π·Δf·τ_rms)   (τ_rms = RMS 지연확산)
 * (mimo.c의 Kronecker 공간상관, tdl.c의 PDP와 동일 철학의 "구현 정의"
 * 근사 — 3GPP가 채널추정기 내부를 규정하지 않으므로 표준 절 인용 대상이
 * 아님. 참고: van de Beek et al., "On Channel Estimation in OFDM
 * Systems", IEEE VTC 1995; Molisch, *Wireless Communications* 지수 PDP
 * 주파수 상관 결과와 동일)
 *
 * 파일럿 개수(num_pilots)만큼의 M×M 복소 역행렬이 필요해 O(M³) — 슬롯당
 * 1회(SNR/지연확산이 같으면 트라이얼마다 재계산할 필요 없음, 호출자가
 * 캐싱해 재사용 권장).
 *
 *   h_ls              : 파일럿 위치에서의 LS 추정치 (num_pilots개)
 *   pilot_pos          : 파일럿 부반송파 인덱스 (오름차순, RE 단위)
 *   delay_spread_rms_ns: 가정하는 RMS 지연확산 [ns]
 *   scs_hz             : 부반송파 간격 [Hz] (pilot_pos를 Δf로 환산)
 *   N0                 : 파일럿 심볼당 잡음 분산
 *   h_mmse_out          : 출력, num_pilots개 (파일럿 위치에서의 스무딩된
 *                          추정치 — 전 부반송파로 확장하려면 이후
 *                          interpolate_channel()에 h_pilots 대신 이걸 전달)
 */
void mmse_channel_estimate(const cx_t *h_ls, const int *pilot_pos, int num_pilots,
                            double delay_spread_rms_ns, double scs_hz, double N0,
                            cx_t *h_mmse_out);

/* mmse_channel_estimate()를 "필터 행렬 계산"과 "적용"으로 분리한 버전.
 * W=R·(R+N0·I)⁻¹는 pilot_pos/delay_spread/scs/N0에만 의존하고 실제 채널
 * 값(h_ls)과는 무관 — 같은 SNR·파일럿 패턴에서 여러 안테나 쌍/트라이얼에
 * 걸쳐 반복 적용할 때는 O(M³) 역행렬을 한 번만 계산(mmse_build_filter)하고
 * 이후 매번 O(M²) 행렬-벡터 곱(mmse_apply_filter)만 하는 것이 훨씬 빠르다
 * (예: massive MIMO에서 Tx×Rx 안테나 쌍마다 반복 적용). W_out/W는
 * num_pilots×num_pilots(row-major) 버퍼를 호출자가 할당. */
void mmse_build_filter(const int *pilot_pos, int num_pilots,
                        double delay_spread_rms_ns, double scs_hz, double N0,
                        cx_t *W_out);
void mmse_apply_filter(const cx_t *W, int num_pilots, const cx_t *h_ls, cx_t *h_mmse_out);

/* ── PRG/wideband 평균 타깃 전용 MMSE(Wiener) 필터 ───────────────────────
 *
 * mmse_build_filter()는 "파일럿 지점 각각의 개별 MSE"를 최소화하도록
 * 설계된 필터라, 그 출력을 나중에 num_target개 위치(예: 서브밴드나
 * wideband의 data RE들)에 대해 평균 내면 "그 평균값 자체의 MSE"에는
 * 최적이 아니다(2026-08-31, EIGEN_16PORT subband 프리코딩 검증 중 발견 —
 * 이 불일치 때문에 DFT 기반 추정이 오히려 MMSE보다 낮은 NMSE를 보인 사례
 * 있음, docs/analysis/history.md 참조). 이 함수는 타깃 자체를
 * Y=(1/D)Σ_d H(f_target[d])로 두고 LMMSE를 다시 유도한다:
 *   Ŷ = r_yz · (R_pilot,pilot+N0·I)⁻¹ · h_ls           (스칼라)
 *   r_yz[p] = E[Y·H*(f_pilot[p])] = (1/D)Σ_d ρ(f_target[d]−f_pilot[p])
 * (ρ는 mmse_build_filter와 동일한 지수 PDP 주파수상관 모델). 출력
 * w_avg_out(길이 num_pilots)은 M×1 가중치 "행벡터" — 이후
 * Ŷ = Σ_p w_avg_out[p]·h_ls[p] (내적 한 번)로 바로 스칼라 추정치를 얻는다
 * (mmse_build_filter+mmse_apply_filter+평균의 2단계보다 한 단계 적고,
 * LMMSE 이론상 이 타깃에 대해 달성 가능한 최소 MSE 선형 추정 — DFT
 * 기반 추정을 포함한 어떤 선형 추정기보다도 NMSE가 같거나 낮아야 한다).
 *
 *   pilot_pos/num_pilots   : 파일럿 위치(M개)
 *   target_pos/num_target  : 평균 낼 목표 위치(D개, 예: 서브밴드의 data RE)
 *   나머지 파라미터는 mmse_build_filter와 동일
 */
void mmse_build_avg_filter(const int *pilot_pos, int num_pilots,
                            const int *target_pos, int num_target,
                            double delay_spread_rms_ns, double scs_hz, double N0,
                            cx_t *w_avg_out);

/* ── DFT 기반 채널추정 (시간영역 노이즈 절단) ────────────────────────────
 *
 * 주파수영역 LS/보간 추정치를 IDFT로 시간영역(채널 임펄스 응답 추정)으로
 * 바꾼 뒤, 실제 채널 에너지가 집중되는 처음 num_taps개 탭만 남기고
 * 나머지(잡음이 지배적인 구간)를 0으로 자른 다음 다시 DFT로 주파수영역에
 * 되돌린다 — 채널의 시간영역 서포트가 전체 심볼 길이보다 훨씬 짧다는
 * 사실을 이용한 표준 잡음 저감 기법(구현 정의, 3GPP 비규정 — 위 MMSE와
 * 동일한 "van de Beek et al." 계열 참고). num_taps는 CP 길이 또는
 * 가정 지연확산에 대응하는 샘플 수로 호출자가 정한다. dft_precode.h의
 * 임의 M-포인트 유니터리 DFT/IDFT를 그대로 재사용 — FFT 크기 제약 없음.
 *
 *   h_freq_in  : 주파수영역 추정치 (num_sc개, 연속 인덱스 0..num_sc-1)
 *   num_taps   : 유지할 시간영역 탭 수 (0 < num_taps <= num_sc)
 *   h_freq_out : 출력, num_sc개 (잡음 저감된 주파수영역 추정치)
 */
void dft_channel_estimate(const cx_t *h_freq_in, int num_sc, int num_taps,
                           cx_t *h_freq_out);

/* ZF equalization: eq[n] = rx[n] / h[n] */
void zf_equalize(const cx_t *rx, const cx_t *h, int n, cx_t *eq);

/* MMSE equalization: w[k]=h*[k]/(|h|^2+N0), eq[n]=w[n]*rx[n].
   If alpha_out != NULL, writes per-SC bias alpha[n]=|h|^2/(|h|^2+N0). */
void mmse_equalize(const cx_t *rx, const cx_t *h, int n,
                   double N0, cx_t *eq, double *alpha_out);

#endif
