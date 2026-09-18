# TDL 시간상관 구현 설계

상태: 2026-09-18, 단계 1/2 구현·검증 완료. 기존 `tdl_draw()` 경로를 보존한다.

## 근거와 범위

TR 38.901 V17.1.0 §7.7.2는 diffuse tap의 classical/Jakes Doppler
spectrum과 최대 Doppler f_D를 정의하며 D/E의 LOS peak는 0.7 f_D다.
공식 원문: https://www.etsi.org/deliver/etsi_tr/138900_138999/138901/17.01.00_60/tr_138901v170100p.pdf

프로파일 delay/power는 기존 TDLChannel을 재사용한다. 시간축만 확장하며
실제 OFDM waveform/ICI, 공간상관, 채널추정 추적 알고리즘은 별도다.

## 구현 선택

`tdl_time_init(state, channel, max_doppler_hz)`로 한 realization을 만들고
`tdl_time_sample(state, time_s, taps)`로 절대 시각의 tap을 평가한다.
샘플 조회는 난수를 소비하지 않아 조회 순서와 호출 간격에 독립적이다.
초기화만 기존 프로젝트 RNG를 사용하므로 기존 `rng_seed()`로 재현된다.

각 diffuse tap은 32개 복소 Gaussian 계수와 각도 방향별 Doppler 성분의
합으로 구현한다. 각도는 균일 간격 격자에 tap별 무작위 회전을 적용한다.
계수 분산은 tap power/32로 설정하여 ensemble power를 유지한다.
이는 Jakes spectrum의 유한 주파수 근사이며 정확한 연속 spectrum이나
모든 realization의 시간평균=ensemble평균을 보장하지 않는다. 32는
구현 선택이며 표준 상수가 아니다. 상관 시간차가 커지면 유한합 오차가
커질 수 있어 covariance 검증 범위를 명시해야 한다.

LOS phasor는 realization 초기 위상을 고정하고 0.7 f_D로 회전한다.
f_D=0은 전체 시간에서 동일한 tap을 반환한다. 음수/비유한 Doppler,
음수/비유한 시간은 오류로 처리한다. 기존 independent draw와 f_D=0의
static realization은 다른 모드이므로 독립 redraw의 기본 동작을 바꾸지 않는다.

## 단계와 검증

1. 독립 모듈: 재현성·동일 시각·조회 순서·zero Doppler·입력 검증,
   ensemble power, diffuse covariance 및 LOS 회전 검증.
2. PUSCH UL 4포트의 HARQ attempt 간 상태 유지에 선택적으로 연결.
   처음에는 한 attempt 내 RE-domain block channel을 유지한다. 따라서
   symbol 내 시간변화/ICI를 모델링했다고 주장하지 않는다.
3. 타 채널은 실제 시간 간격 정의를 검토한 뒤 단계적으로 연결.
   53개 호출부를 일괄 교체하거나 trial을 임의의 초 단위로 해석하지 않는다.

## 적용 설정

- TDL_TIME_CORRELATION=0(기본) 또는 1.
- TDL_MAX_DOPPLER_HZ>=0, TDL_HARQ_INTERVAL_MS>0(기본 1 ms).
- 활성화는 PUSCH/UL_CB_4PORT/TDL/HARQ 조합에서만 허용.
- TB마다 16개 안테나쌍 상태를 새로 초기화하고 attempt a에서
  t=a*interval_ms/1000으로 조회. RI/TPMI는 기존처럼 첫 attempt에서 고정.
- 4,000 realization/profile에서 tap power 오차 8% 이내,
  fD=100 Hz, lag=1/5 ms의 diffuse normalized covariance를
  J0(0.2π)/J0(π)와 복소 절대오차 0.07 이내 비교했다.
- 무한 시간차·연속 Doppler spectrum 정확성은 검증하지 않았다.
