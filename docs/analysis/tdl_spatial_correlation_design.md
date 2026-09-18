# TDL MIMO 공간상관 설계

2026-09-18. 기존 todo의 "TDL은 ray 기반이어야 한다"는 범위를 정정한다.
TR 38.901 V17.1.0 §7.7.5.2는 TDL에 correlation matrix를 적용하는
MIMO link-level 방식을 명시한다. CDL 각도/레이 확장은 별도 선택지다.
공식 근거: https://www.etsi.org/deliver/etsi_tr/138900_138999/138901/17.01.00_60/tr_138901v170100p.pdf

첫 구현은 UL 4Tx/4Rx, TDL-A/B/C의 각 tap에 H=L_rx G L_tx^T를 적용한다.
R_tx[i,j]=rho_tx^|i-j|, R_rx[i,j]=rho_rx^|i-j|이고 L은 Cholesky factor다.
이 실수 지수상관/포트 배열은 연구용 구현 선택이다. 표준 low/medium/high
preset, 편파 배열 또는 물리 안테나 형상을 재현한다고 주장하지 않는다.
각 tap과 시각에 동일한 두 factor를 적용하므로 tap별 power와 기존
시간상관을 유지하며 tx/rx correlation을 제어한다. rho=0은 정확한 no-op.

전용 설정 TDL_SPATIAL_CORR_TX/RX의 범위는 [0,1)이며 기본은 0이다.
기존 SPATIAL_CORR_TX는 DL 편파/배열별 모델이므로 재해석하지 않는다.
양수 설정은 PUSCH UL_CB_4PORT TDL-A/B/C에서만 허용한다. D/E LOS의
공간 steering/편파 가정은 아직 결정되지 않아 양수 상관과의 조합을
명시적으로 거부한다. flat 및 다른 채널도 지원 범위를 넓히기 전 거부한다.

검증은 no-op, 손계산 단일 원소 impulse, 유한성, per-pair power 및
Tx/Rx/cross-pair covariance를 독립 ensemble로 확인한다. 시간상관과의
결합은 기존 absolute-time sample 뒤에 동일 선형변환을 적용하여 연결한다.
향후 per-tap correlation matrix 입력, 표준 preset, LOS steering, 기타
MIMO 모드 연결 및 CDL 물리 배열 모델을 별도 단계로 확장한다.
