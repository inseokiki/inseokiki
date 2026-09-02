# 5G PHY LLS — 코드 구조 및 설계 가이드

---

## 1. 디렉토리 구조

```
inseokiki/
├── PHY/                    ← LLS (Link Level Simulator)
│   ├── src/                ← C 소스
│   ├── include/            ← 헤더
│   ├── config/             ← 시뮬 설정 파일
│   ├── build/              ← 오브젝트 파일 (빌드 산출물, git 미추적)
│   ├── results/            ← 시뮬 결과물 (png/txt/fig/iq_dump, 재현 가능, git 미추적)
│   ├── plots/               ← MATLAB 플롯 스크립트 (.m)
│   ├── c_Makefile
│   ├── copy.sh              ← results/plots를 Windows MATLAB 폴더로 복사 (개인 경로 포함, git 미추적)
│   ├── regression_test.sh   ← 74-case 회귀 테스트 (케이스 수는 기능 추가마다 늘어남, 정확한 값은 스크립트 참조)
│   └── lls_sim_c            ← 실행 바이너리 (git 미추적)
│
├── BER/                    ← 독립 BER 툴 (PHY 코드 미사용)
│   ├── src/
│   ├── include/
│   ├── config/
│   ├── Makefile
│   └── ber_sim             ← 실행 바이너리 (git 미추적)
│
└── 3gpp/                   ← 3GPP 표준 문서
```

### 빌드 방법

```bash
# LLS 시뮬레이터
cd PHY && make -f c_Makefile

# BER 툴
cd BER && make
```

---

## 2. PHY/ — LLS 모듈 구성

### 모듈별 역할

| 파일 | 역할 |
|------|------|
| `config_parser.c` | 설정 파일 파싱 + MCS 기반 파라미터 자동 결정 |
| `mcs_table.c` | 3GPP TS 38.214 MCS 테이블 3종 |
| `modulation.c` | QAM 변조 / 복조 / LLR 계산. `qam_soft_symbol()`은 a priori 비트 LLR → 심볼별 소프트 평균/분산(turbo 등화용), `qam_demap_llr`과 같은 PAM 테이블을 재사용해 매핑 방식과 무관하게 정확 |
| `channel.c` | AWGN / Flat Fading 채널 |
| `channel_estimation.c` | LS 추정 + 보간 + ZF / MMSE 등화. `mmse_channel_estimate()`(파일럿별 점추정 LMMSE, 지수 PDP)/`dft_channel_estimate()`(시간영역 잡음절단)로 imperfect CSI 확장, `mmse_build_filter()`+`mmse_apply_filter()`로 SNR당 필터 1회 계산·재사용(성능 분리). `mmse_build_avg_filter()`는 wideband/subband 평균 타깃(target_pos 지정) 자체에 대해 LMMSE를 재유도 — 점추정 필터의 평균이 평균 자체의 MSE 최적은 아니라는 점을 발견해 도입, DFT보다 항상 낮은 NMSE 보장(이론+실측 확인) |
| `mimo.c` | SU-MIMO 2x2/4x4 채널(block-flat Rayleigh) + MRC / ZF / MMSE 검출. 검출 함수(`mimo_zf_detect`/`mimo_mmse_detect`/`mrc_combine`)는 RE 단위 순수 함수라 flat이든 TDL 등 frequency-selective든 그대로 재사용됨. `mimo_apply_tx_correlation_4x4(h, rho)`는 4x4 채널에 Tx 공간상관(Kronecker, XPOL 2x2 블록별 지수상관, 편파 간은 비상관) 적용 — `SPATIAL_CORR_TX`(MIMO_MODE=CL_4PORT 전용)로 제어. 편파 간 완전 독립 가정 때문에 동일편파 상관만으로는 rank-2(교차편파 다이버시티 변형)가 계속 유리해 rank-1 선택률이 ρ→1에서도 낮게 유지됨(실측 확인, 2026-08-02). `mimo_apply_tx_correlation_4x8`/`_4x32`는 CL_8PORT(N1=4 1D)/CL_32PORT(N1=N2=4 2D Kronecker, R_2D=R_horiz⊗R_vert)용 확장. `mimo_channel_draw_4x8`/`_4x16`/`_4x32`는 각 massive-MIMO 포트 수용 i.i.d. Rayleigh 채널 드로우, `mimo_mmse_detect_4rx3`은 3×3 Gramian MMSE(rank-3 검출용) |
| `codebook.c` | Type I SP 4-port 코드북(N1=2,N2=1,O1=4,P=4, TS 38.214 §5.2.2.2.1) — rank-1/2 프리코더 + RI/PMI 동시 적응 선택기(전수탐색). CL_4PORT 계열의 기반 |
| `codebook_8port.c` | Type I SP 8-port 코드북(N1=4,N2=1,O1=4,P=8) — 4-port와 동일 수식 구조를 N1=4로 일반화(rank-1 64 + rank-2 256 = 320 후보). CL_8PORT 계열의 기반 |
| `codebook_32port.c` | Type I SP 32-port 코드북(N1=4,N2=4,O1=4,O2=4,P=32, TS 38.214 Rel-18 Type I SP 스펙상 최댓값) — 순수 2D 배열이라 v_{l,m}에 수직 성분(u_m) 추가, rank 1~4 지원(rank3/4는 P≥16 분기, ṽ 절반길이 빔벡터). RI+PMI 5120후보 전수탐색. CL_32PORT 계열의 기반이자 `beam_mgmt.c`의 후보 빔 집합(rank-1 1024개) 재사용처 |
| `eigen_16port.c` | Eigen-Beamforming(SVD) 16-port, 비-코드북 개루프 방식 — 4×4 Hermitian 고유분해(복소 Cyclic Jacobi)로 4×16 채널 SVD 경량 계산, rank 적응은 등력분배 기준 용량 최대화. 코드북 양자화 손실 없는 상한 성격(CL_32PORT 대비 실측 확인). 고유분해 자체는 `utils.c`의 `herm4x4_eig()`로 추출돼(2026-09-02) `ul_eigen_bf.c`(UL Eigen-BF 4-Tx 확장)와 공유 |
| `olla.c` | OLLA(Outer Loop Link Adaptation) — ACK/NACK 기반 SNR 오프셋 폐루프 보정(`OLLAState`), `olla_select_mcs()`(Shannon 용량+구현마진으로 MCS 선택). 개루프 근사가 실제 코덱과 못 맞는 문제(3GPP 미규정, 구현 정의)를 보정. 고정 SNR 시계열 전용(SNR sweep 무시), `OLLA_ENABLE=1`이 다른 모든 PDSCH dispatch보다 우선. SISO 외 SIMO_MRC(MRC 다이버시티)/SM_2X2(공간다중화, ZF/MMSE)도 지원(2026-09-01) — MCS 예측식은 세 모드 모두 동일(SISO 기준, 다이버시티 이득/MIMO 검출손실 미반영, 의도적 단순화). 검증 중 발견: 저SNR+SIMO_MRC 조합에서 MCS 테이블 최하단에서도 목표 BLER을 못 맞추는 진짜 하한(floor) 확인 — Shannon+gap 근사가 이 프로젝트 LDPC 코덱엔 낙관적이라는 기존 발견이 블록-플랫 페이딩과 결합한 결과(버그 아님) |
| `mumimo.c` | MU-MIMO(Multi-User MIMO) 하향링크 — Zero-Forcing Beamforming(우측 유사역행렬 H^+=H^H(HH^H)^-1)로 서로 다른 사용자 간 간섭을 설계상 제거. Nt=4/K=2 사용자(각 1 Rx, MU-MISO). `mumimo_zf_precode()`는 K=2 폐형 2×2 역행렬이라 RE당 비용이 무시할 만해 TDL에서도 PRG 근사 없이 매 RE 정확히 재설계(2026-09-01), flat/TDL/HARQ 모두 지원. K>2는 이 폐형 역행렬을 일반 K×K로 교체해야 함(후속 과제) |
| `beam_mgmt.c` | 빔 관리(Beam Management) — SSB/CSI-RS 기반 P1 절차(gNB Tx 빔 스위핑, UE Rx 스위핑은 미포함). `codebook_32port.c`의 rank-1 코드북(1024후보)을 후보 빔 집합으로 재사용, 참 채널은 코드북 v_{l,m} 공식을 연속값 방향으로 일반화한 LOS steering vector — 양자화 손실(genie)과 측정잡음 손실(P1, num_rep회 RSRP 평균)을 분리 평가. TDL 확장(2026-09-01)에서는 빔 선택 자체를 wideband로 유지하고 데이터 전송에만 (32안테나 공통) 단일 클러스터 SISO TDL 게인을 곱함 — 이 모델에서는 후보 빔 순위가 TDL 값과 무관하게 wideband와 항상 동일함을 대수적으로 증명(공유 스칼라라 모든 후보에 동일하게 곱해짐) |
| `ul_power_ctrl.c` | UL Closed-Loop Power Control(ULPC, TS 38.213 §7.2.1) — TPC 누산 f(i)={-1,0,+1,+3}dB genie-aided 시계열 수렴 + PL 스윕(OL vs CL 비교, P_CMAX 클램핑). `UL_PC_PL_VAR_*`로 Gauss-Markov(AR1) 시변 PL(이동성/페이딩 근사) 토글 |
| `ul_eigen_bf.c` | UL 수신 빔포밍 — 공간공분산 EVD 기반, 비-코드북(2026-09-01, K=4 확장은 2026-09-02). DL EIGEN_16PORT의 Tx측 SVD 빔포밍과 대칭되는 Rx측 구현. **1-Tx(SIMO)**: 순간 채널이 rank-1이라 고유빔포밍=MRC와 수학적으로 동일 — 대신 M개 잡음 파일럿 관측의 공간공분산에서 지배적 고유벡터를 전력반복법(power iteration)으로 뽑아 순간 MRC보다 잡음에 강건한 빔을 만듦(`ul_eigen_beamform()`, SRS 기반 공분산 추정과 동일 개념). **2-Tx**: 채널 H(16×2)가 더 이상 rank-1이 아니므로 진짜 SVD 필요 — `ul_eigen_svd_genie()`가 2×2 Gram 행렬(H^H H)의 닫힌 형식(반복법 불필요) 고유분해로 좌특이벡터 U와 특이값 sigma[2]를 계산(Genie, sigma는 랭크 적응에 사용), `ul_eigen_orthogonalize2()`(그람-슈미트)로 레이어별 독립 추정 빔 2개를 직교화(Eigen-BF, 결합잡음을 N0·I로 유지해 기존 2×2 검출기 재사용 가능하게 함). **4-Tx(최대)**: 2×2와 달리 4×4 Gram 행렬은 닫힌 형식이 없어 반복법 필요 — `ul_eigen_svd_genie4()`가 `utils.c`의 공용 `herm4x4_eig()`(DL EIGEN_16PORT와 공유하는 Cyclic Jacobi)로 좌특이벡터 U(16×4)와 특이값 sigma[4]를 계산, `ul_eigen_orthogonalize4()`(순차 그람-슈미트, orthogonalize2의 K=4 일반화)로 4개 추정 빔을 직교화. 랭크 적응은 새 함수 없이 `eigen_16port.h`의 `eigen_bf_16port_select_rank()`를 그대로 재사용(인터페이스가 이미 (sigma[4],N0,*rank)로 동일) |
| `rate_matching.c` | Circular buffer rate matching (RV 기반 균등 1/4-버퍼 k0 오프셋, 구현정의 단순화) + HARQ 소프트 컴바이닝. **2026-09-02부로 PUCCH F3 Polar HARQ 전용**(LDPC 경로는 전부 `nr_rate_matching.c`로 이전, P0-3) — Polar는 BG/`Zc`/filler 개념이 없어 이 단순화가 그대로 유지됨 |
| `nr_rate_matching.c` | TS 38.212 §5.4.2.1 표준 LDPC circular-buffer rate matching(2026-09-02 신규, P0-3). `nr_ldpc_k0(bg,Zc,Ncb,rv)`(Table 5.4.2.1-2, 3gpp-server MCP로 원문 이미지 직접 확인 — BG1: rv1/2/3=⌊17·Ncb/(66Zc)⌋·Zc/⌊33·Ncb/(66Zc)⌋·Zc/⌊56·Ncb/(66Zc)⌋·Zc, BG2는 66→50·17/33/56→13/25/43), `nr_ldpc_rate_match_select()`/`_combine()`/`_select_soft()`(§5.4.2.1 bit-selection while-loop — filler 위치(`[info_size, base_info_cols*Zc)`) 건너뜀, `_select_soft()`는 double 버전으로 `run_pusch_tdl_turbo_simulation`의 turbo iteration간 extrinsic 재선별에 사용). `rate_matching.c`(PUCCH F3 공유)와 별개 모듈 — LDPC 전용. Ncb=N(mother codeword 전체 길이, LBRM 미모델링) 가정, C=1(단일 코드블록, P0-1과 동일 범위) |
| `tdl.c` | TDL 주파수 선택적 페이딩 (근사 6탭 NLOS PDP, TS 38.901 표 근사치). `tdl_draw()`를 Tx-Rx 안테나 쌍마다 독립 호출하면 MIMO 공간축으로 그대로 확장 가능 (`pdsch.c`의 TDL+MIMO 조합 함수들 참조) |
| `dft_precode.c` | PUSCH Transform Precoding용 유니터리 M-point DFT/IDFT (O(M²) 직접합산) |
| `pusch.c` | PUSCH(상향) 시뮬레이션 — DFT-s-OFDM/CP-OFDM 토글, PDSCH+DMRS 체인 재사용. **P0-3(2026-09-02)**: LDPC를 쓰는 모든 함수가 `nr_rate_matching.c`의 표준 rate matching을 적용(HARQ 함수는 k0 공식만 표준으로 교체, 비-HARQ 함수는 rate matching 자체를 신규 적용 — 이전엔 mother codeword를 그냥 truncate했음). `run_pusch_tdl_dfe_simulation()`은 MMSE+TDL의 잔여 ISI(`alpha_d` RE별 변동)를 블록 `run_pusch_tdl_dfe_simulation()`은 MMSE+TDL의 잔여 ISI(`alpha_d` RE별 변동)를 블록 병렬간섭제거(DFE)로 실제 제거해보는 연구용 프로토타입(PUSCH_DFE_ENABLE=1) — BLER(noDFE) vs BLER(DFE)를 같은 채널/노이즈 draw로 나란히 비교. 저SNR에서는 오류전파로 악화, 중~고SNR(약 20dB대)에서 소폭 개선, 매우 높은 SNR에서는 둘 다 무오류로 수렴(실측 확인, STRUCTURE 하단 참고). `run_pusch_tdl_turbo_simulation()`(PUSCH_TURBO_ENABLE=1)은 하드 DFE를 소프트 PIC + `ldpc_decode_soft()` extrinsic 반복교환으로 일반화 — BLER(noDFE)/BLER(hardDFE)/BLER(turbo) 3열 비교, 실측상 하드 DFE가 손해보던 SNR 구간까지 포함해 전 구간에서 turbo가 우세함을 확인. `run_pusch_sm2x2_simulation()`/`_tdl_simulation()`(2026-09-01)은 PUSCH 최초의 MIMO — DL `run_pdsch_sm2x2_*`와 동일 구조(`mimo.c`의 RE 단위 순수 검출/채널드로우 함수를 방향 무관하게 재사용), TS 38.211 §6.3.1.4에 따라 Transform Precoding은 다중 레이어를 지원하지 않으므로 CP-OFDM 전용(config_parser.c가 강제). `run_pusch_sm2x2_tdl_harq_simulation()`(2026-09-01)은 DL `run_pdsch_sm2x2_tdl_harq_simulation()`을 1:1 포팅(mother LDPC+circular buffer, DL과 동일하게 TDL 전용·flat+HARQ는 아직 없음). `run_pusch_ul_eigen_bf_simulation()`(2026-09-01)은 `ul_eigen_bf.c`의 Genie MRC vs Eigen-BF를 병렬 비교(빔관리 함수의 Genie/P1 비교와 동일 철학). `run_pusch_ul_eigen_bf_tdl_simulation()`/`_harq_simulation()`(2026-09-01)은 TDL/HARQ 확장 — **배선 전 검증에서 채널 모델 결함 발견·수정**: 처음엔 SM_2X2/MU-MIMO처럼 16개 Rx 안테나 각각 독립 TDL을 줬다가 Eigen-BF가 Genie에 전혀 수렴 못 하는 현상을 발견(파일럿 대역이 coherence bandwidth를 초과해 고정 공간방향 자체가 사라짐), 빔관리 TDL과 동일한 "공유 클러스터 게인 × 고정 공간벡터" 모델로 교체해 해결. HARQ는 빔 추정을 트라이얼당 1회만 수행(빔관리 HARQ와 동일 철학). `run_pusch_ul_eigen_bf_2tx_simulation()`(2026-09-01)은 UE 2 Tx로 확장 — Genie SVD vs Eigen-BF(그람-슈미트 직교화) 2×2 유효채널을 기존 `mimo_zf_detect`/`mimo_mmse_detect`로 검출(신규 검출 코드 없음). **랭크 적응(2026-09-01 추가)**: `ul_eigen_svd_genie()`가 반환하는 특이값 sigma[2]로 등력분배 기준 Shannon 용량을 rank-1/rank-2 각각 추정(cap1 vs cap2)해 트라이얼당(HARQ는 버스트당) 1회 랭크를 결정, Genie/Eigen-BF 양쪽이 동일 랭크 공유. rank=1일 때는 신규 검출 코드 없이 기존 `mrc_combine()`을 유효채널 0번 열에 적용해 재사용. `run_pusch_ul_eigen_bf_2tx_tdl_simulation()`/`_harq_simulation()`(2026-09-01, 랭크 적응 포함)은 1-Tx의 "공유 클러스터 게인 × 고정 공간 시그니처" 교훈을 그대로 적용 — H(16×2)가 트라이얼 내내 고정이라 U/Q와 랭크 결정도 RE 무관 1회 계산, 2×2 유효채널은 고정 베이스 행렬을 g(re)로 스케일하는 것으로 단순화(Genie U/sigma는 H에만 의존해 HARQ에서도 매 attempt 정확). `run_pusch_ul_eigen_bf_4tx_simulation()`/`_tdl_simulation()`/`_harq_simulation()`(2026-09-02)은 2-Tx 설계를 K=4로 일반화 — `ul_eigen_svd_genie4()`/`ul_eigen_orthogonalize4()`로 4×4 확장, 랭크 적응(1~4)은 DL EIGEN_16PORT의 `eigen_bf_16port_select_rank()`를 그대로 재사용. 검출은 `ul_eigen4_detect_re()`(신규 static 헬퍼, 세 함수 공유)가 4×4 투영 유효채널에서 활성 rank개 열만 골라 CL_32PORT와 동일한 검출기 계열(mrc_combine_4rx/mimo_mmse_detect_4rx2/4rx3/4x4)로 분기(신규 검출 코드 없음, CL_32PORT 선례를 따라 MMSE 고정 — ZF는 rank<4 사각계에 미구현). 파일럿 관측 수는 레이어당 `(6*num_rb)/4`(정수 나눗셈), TDL 파일럿 RE는 4-way 인터리브로 레이어별 분리 |
| `pucch_seq.c` | PUCCH F0/F1용 저PAPR base sequence(ZC 근사) + 순환시프트 |
| `pucch.c` | PUCCH(상향 제어) F0~F3 — F0/F1 시퀀스 검출, F2/F3 Polar 대체 코딩. F1/F3는 TDL 변형도 있음(genie-aided CSI — PUCCH는 DMRS/LS 추정 파이프라인이 없어 `tdl_channel_apply()`의 `h_out`을 그대로 완벽 채널로 사용); F0/F2는 AWGN 전용. F1/F3 TDL은 HARQ 결합 버전도 있음(재전송마다 채널 재드로우, PUCCH UCI는 스펙상 CRC가 없어 genie 정답 비트 일치를 종료 판정 기준으로 대체) — F3는 N=64 Polar mother codeword에 `rate_matching.c`의 범용 circular-buffer(원래 LDPC용)를 그대로 재사용해 IR/Chase RV 결합 |
| `ofdm.c` | OFDM 변조 / 복조 (FFT/IFFT) |
| `dmrs.c` | DMRS 파일럿 시퀀스 및 위치 인덱스 |
| `crc.c` | CRC-24A / CRC-24C 생성 및 검사 |
| `ldpc.c` | LDPC 인코더/디코더 공개 API(`ldpc_init`/`ldpc_encode`/`ldpc_decode`/`ldpc_decode_soft`) — **2026-09-02부로 실제 3GPP TS 38.212 BG1/BG2 QC-LDPC로 교체**(이전엔 자체 parity-check `build_H()`, column weight 3 + modulo 배치의 3GPP 비준거 surrogate codec — 상세 검증 이력은 `docs/analysis/phy_development_direction_validation.md` P0-1/Section 10 참조). `ldpc_init()`이 `ldpc_nr.c`의 `nr_select_bg_zc()`/`build_H_nr()`/`ldpc_encode_prepare_nr()`로 위임(BG/`Zc` 선택 → 리프팅된 H 구성 → core 시스템 GF(2) 역행렬 캐싱), `ldpc_encode()`는 `ldpc_encode_nr()`로 위임. **디코더(BP 루프) 자체는 무변경 재사용**(2026-09-02 앞서 완료된 edge-message 교정 그대로) — `H_row_ptr`/`H_col`/`Ht_row_ptr`/`Ht_links`가 커질 뿐 알고리즘은 동일. `ldpc_decode_soft()`에 filler bit 위치(`[info_size, base_info_cols*Zc)`) LLR 강제(확실한 bit-0, `llr[]` 사본에만 적용해 turbo 등화 extrinsic 계산용 원본은 보존) 추가. **범위**: 단일 코드블록만(다중 CB 세그멘테이션 없음 — 모든 호출부가 이미 block_size≤8424로 캡핑돼 있어 이번 범위에서 불필요, 후속 과제 P0-2c). `ldpc_init()`이 `Kb·Zc≥block_size`를 만족하는 조합이 없으면(세그멘테이션이 필요한 경우) 조용히 clamp하지 않고 진단 메시지와 함께 `exit(1)`. **표준 rate matching(P0-3, 2026-09-02 완료)**: `nr_rate_matching.c` 참조 — LDPC를 쓰는 51개 `run_pdsch_*`/`run_pusch_*` 함수 전부가 이제 실제 `E`(레이어당 RE 예산 × 변조차수)개 비트만 표준 순환버퍼 규칙으로 선택/결합하도록 갱신됨(이전엔 mother codeword 앞부분을 그냥 truncate하는 비표준 방식이었음) |
| `ldpc_tables.c` | TS 38.212 Table 5.3.2-1/5.3.2-2/5.3.2-3 데이터(로직 없음, 2026-09-02 신규) — BG1(46×68, nonzero 316개×8 lifting-set=2528 shift 정수)/BG2(42×52, 197개×8=1576) shift-coefficient 표와 8개 lifting-size set(전체 51개 값). **로컬 스펙 원본에서 프로그램적으로 추출**(`3gpp/38212-hc0/38212-hc0.docx`의 `word/document.xml` 표 셀을 zipfile+regex로 직접 파싱, 手전사 없음 — ~4100개 정수의 手전사 오타 위험 제거). 추출 후 자체 검증: 두 표 모두 중복 (row,col) 없음·행/열 범위 전체 커버, parity 열 중 `base_info_cols+4` 이상 인덱스는 전부 차수 1·전 8개 lifting-set에서 shift=0(리프팅된 대각 항등 구조), base-graph 행 0-3은 그 열들을 전혀 참조하지 않음(→ `ldpc_nr.c`의 core 4×4 순환치환 시스템 구조가 실제 표에서 성립함을 확인) |
| `ldpc_nr.c` | NR LDPC 내부 구현(2026-09-02 신규, `ldpc.h` 공개 API 아님 — `ldpc.c`만 이 헤더를 include). `nr_select_bg_zc()`(TS 38.212 §6.2.2 BG 선택: `A≤292` 이거나 `A≤3824∧R≤0.67` 이거나 `R≤0.25`면 BG2, `Kb`는 BG1=22 고정/BG2=block_size 구간별 6·8·9·10, `Kb·Zc≥block_size` 만족 최소 `Zc`). `build_H_nr()`(TS 38.212 §5.3.2 — 단위행렬을 오른쪽으로 `shift mod Zc`번 순환시프트해 리프팅, 즉 `lifted_col=col*Zc+(k+shift)%Zc` — 원문 확인 완료, 기존 `H_row_ptr`/`H_col`/`Ht_*` CSR 필드를 그대로 채워 BP 디코더 무변경 재사용 가능하게 함). `ldpc_encode_prepare_nr()`/`ldpc_encode_nr()`(TS 38.212 §5.3.2의 Richardson-Urbanke류 구조화 인코딩 — 표에서 실측 확인된 성질을 그대로 이용: 처음 4개 parity 열이 서로 얽힌 4×4 순환블록 "core" 시스템을 이루고(GF(2) 역행렬을 `ldpc_init` 1회만 계산해 캐싱, 트라이얼마다 재계산 안 함), 나머지 parity 열은 각각 자기 행에만 shift=0으로 연결되는 순수 대각 구조라 core 해가 나오면 단순 XOR로 직접 풀림 — 신규 검출/최적화 코드 없이 표 구조를 그대로 이용). filler bit(`[block_size, base_info_cols*Zc)`)는 인코딩 시 0 고정, `ldpc.c`의 디코더 쪽에서 LLR 강제 |
| `polar.c` | Polar 인코더 / 디코더 (제어 채널). `polar_init(N,K,E)`가 E를 받아 rate-matching shortening 위치를 강제-frozen 처리 (TS 38.212 5.4.1.1, 2026-07-13 버그 수정 — 이전엔 shortening 위치가 info bit와 겹쳐 파괴될 수 있었음) |
| `polar_rate_match.c` | Polar Rate Matching / Dematching. `polar_interleaver()`는 `polar.c`의 frozen-bit 계산과 공유하기 위해 공개 함수로 노출됨 |
| `pbch.c` | PBCH 시뮬레이션 루프. `run_pbch_fading_simulation()`(CHANNEL_MODEL=FLAT_FADING/TDL)은 PUCCH처럼 DMRS 파이프라인이 없어 genie-aided CSI 사용 — FLAT_FADING은 전체 SSB에 단일 탭, TDL은 심볼 인덱스를 RE로 취급(SCS=SCS_KHZ). `qam_demap_llr_mmse()`는 반드시 `mmse_equalize()` 결과(이미 등화된 신호)를 입력받아야 함(원시 rx 아님) — 이 규약을 지키지 않으면 위상 보정이 안 돼 SNR과 무관한 BLER floor가 생김(2026-08-02 구현 중 발견·수정) |
| `pdcch.c` | PDCCH 시뮬레이션 루프 (Blind Decoding 포함). `run_pdcch_fading_simulation()`은 실제 송신된 후보(TX candidate)만 페이딩 채널을 통과시키고, 나머지 blind-decoding 후보들은 기존과 동일하게 순수 노이즈 드로우 유지(실제 송신과 무관하므로 채널이 의미 없음) — ZF/MMSE 등화 후 TX 후보 전용 스칼라 유효 노이즈분산(`run_pucch_format3_tdl_simulation()`과 같은 평균화 단순화)을 계산해 기존 `qam_demap_llr()` 경로 그대로 재사용 |
| `pdsch.c` | PDSCH 시뮬레이션 루프 — 이 프로젝트에서 가장 큰 파일(6800+줄). **P0-3(2026-09-02)**: LDPC를 쓰는 모든 함수가 `nr_rate_matching.c`의 표준 rate matching을 적용(HARQ 함수는 k0 공식만 표준으로 교체, 비-HARQ 함수는 rate matching 자체를 신규 적용 — `run_pdsch_simulation`처럼 RE 그리드가 없는 함수는 `E=A/cr`를 직접 유도). w/o DMRS 기본형부터 SISO/SIMO_MRC/SM_2X2/SM_4X4(SU-MIMO)/CL_4·8·32PORT(Type I SP 코드북)/EIGEN_16PORT(SVD 비-코드북)/MU_MIMO(ZF-BF)/BEAM_MGMT(P1 빔스위핑)까지 각 flat/TDL/HARQ 조합별 전용 함수(`run_pdsch_*`), OLLA(SISO 시계열) 포함. 정확한 dispatch 라우팅은 아래 "실행 흐름" 참조 |
| `csi_rs.c` | CSI-RS 채널 추정 시뮬레이션 |
| `srs.c` | SRS 채널 사운딩 시뮬레이션 |
| `prach.c` | PRACH(UL 랜덤 접속) — ZC 루트시퀀스(L_RA=839/139, 둘 다 소수라 정확한 공식) + 사이클릭시프트 프리앰블 + 순환상관 스윕으로 프리앰블 검출과 TA(샘플 단위) 동시 추정. SRS/PUCCH F0처럼 OFDM 그리드 없이 시퀀스 도메인 전용, 단일 루트만 지원. TDL 변형(`run_prach_tdl_simulation()`)도 있음 — 같은 ZC 샘플을 PRACH 자체 서브캐리어 간격(Δf_RA, LONG=1.25kHz 고정/SHORT=carrier SCS 재사용)의 RE 값으로 재해석하고 `tdl.c`를 재사용해 다경로 페이딩을 추가; DFT shift 정리에 의해 기존 순환시프트가 정확히 연속시간 지연과 등가임을 이용해 검출 알고리즘 자체는 무변경 |
| `utils.c` | 난수 생성 등 공통 유틸. `rand_uniform_int(n)`은 PRACH의 프리앰블/지연 추첨용으로 추가된 균등정수 RNG (기존 시드 상태 재사용). `herm4x4_eig()`(2026-09-02)는 4×4 복소 Hermitian 고유분해(Cyclic Jacobi) — 원래 `eigen_16port.c`의 static 함수였던 것을 `ul_eigen_bf.c`(UL Eigen-BF 4-Tx 확장)와 공유하기 위해 이곳으로 추출(로직 변경 없음) |
| `main.c` | 진입점 — 채널 타입별 분기 |

### 실행 흐름

```
main()
  └─ config_parser_load()
       └─ calc_derived()        ← MCS 룩업 → modulation, codeRate 자동 결정
                                ← 채널 타입별 코딩 방식 강제
  └─ 채널 타입 분기
       ├─ PDSCH  → (아래는 main.c if/else if 실제 우선순위 순서)
       │           run_pdsch_olla_simulation()              ← OLLA_ENABLE=1, MIMO_MODE=SISO (또는 그 외 미지원 값 — config_parser.c가 SIMO_MRC/SM_2X2 외에는 차단)
       │           run_pdsch_olla_simo_mrc_simulation()     ← OLLA_ENABLE=1, MIMO_MODE=SIMO_MRC (1x2 MRC 다이버시티)
       │           run_pdsch_olla_sm2x2_simulation()        ← OLLA_ENABLE=1, MIMO_MODE=SM_2X2 (2x2 공간다중화, ZF/MMSE, 결합 ACK)
       │           run_pdsch_simulation()                   ← USE_DMRS=0
       │           ── HARQ_ENABLE=1 (USE_DMRS=1) ── config_parser.c가 아래 화이트리스트
       │             밖의 MIMO_MODE+CHANNEL_MODEL 조합을 CFG_ERR로 미리 차단(2026-09-01,
       │             과거엔 조용히 run_pdsch_harq_simulation()(SISO)으로 떨어지는 오배선이 있었음) ──
       │           run_pdsch_sm4x4_harq_simulation()        ← MIMO_MODE=SM_4X4 (4x4 SM+HARQ, flat/TDL 공용, genie-aided CSI)
       │           run_pdsch_cl_4port_harq_simulation()     ← MIMO_MODE=CL_4PORT (flat/TDL 공용, wideband PMI 고정, genie-aided CSI)
       │           run_pdsch_cl_8port_harq_simulation()     ← MIMO_MODE=CL_8PORT (flat/TDL 공용, wideband PMI 고정)
       │           run_pdsch_cl_32port_harq_simulation()    ← MIMO_MODE=CL_32PORT (flat/TDL 공용, rank1~4 PMI 고정)
       │           run_pdsch_mumimo_harq_simulation()       ← MIMO_MODE=MU_MIMO (flat/TDL 공용, 함수 내부 is_tdl 분기)
       │           run_pdsch_beam_mgmt_harq_simulation()    ← MIMO_MODE=BEAM_MGMT (flat/TDL 공용, 함수 내부 is_tdl 분기, 빔 선택은 attempt 0에서 1회만)
       │           run_pdsch_sm2x2_tdl_harq_simulation()    ← MIMO_MODE=SM_2X2, CHANNEL_MODEL=TDL (2x2+TDL+IR/Chase, flat 미지원)
       │           run_pdsch_simo_mrc_tdl_harq_simulation() ← MIMO_MODE=SIMO_MRC, CHANNEL_MODEL=TDL (1x2 MRC+TDL+IR/Chase, flat 미지원)
       │           run_pdsch_harq_simulation()              ← 그 외(=MIMO_MODE=SISO), SISO 전용, flat/TDL 공용
       │           ── HARQ_ENABLE=0 (USE_DMRS=1) ──
       │           run_pdsch_mumimo_simulation()            ← MIMO_MODE=MU_MIMO, CHANNEL_MODEL≠TDL (ZF-BF, Nt=4/K=2 사용자)
       │           run_pdsch_mumimo_tdl_simulation()        ← MIMO_MODE=MU_MIMO, CHANNEL_MODEL=TDL (RE별 프리코더 재설계)
       │           run_pdsch_beam_mgmt_simulation()         ← MIMO_MODE=BEAM_MGMT, CHANNEL_MODEL≠TDL (SSB/CSI-RS P1 빔스위핑)
       │           run_pdsch_beam_mgmt_tdl_simulation()     ← MIMO_MODE=BEAM_MGMT, CHANNEL_MODEL=TDL (빔 선택은 wideband 유지, 데이터 전송만 TDL)
       │           run_pdsch_simo_mrc_simulation()          ← MIMO_MODE=SIMO_MRC, CHANNEL_MODEL≠TDL (1x2, MRC)
       │           run_pdsch_simo_mrc_tdl_simulation()      ← MIMO_MODE=SIMO_MRC, CHANNEL_MODEL=TDL (1x2, MRC, 주파수선택적)
       │           run_pdsch_sm2x2_simulation()             ← MIMO_MODE=SM_2X2, CHANNEL_MODEL≠TDL (2x2, ZF/MMSE)
       │           run_pdsch_sm2x2_tdl_simulation()         ← MIMO_MODE=SM_2X2, CHANNEL_MODEL=TDL (2x2, ZF/MMSE, 주파수선택적)
       │           run_pdsch_sm4x4_simulation()             ← MIMO_MODE=SM_4X4, CHANNEL_MODEL≠TDL (4x4, MMSE, genie-aided CSI)
       │           run_pdsch_sm4x4_tdl_simulation()         ← MIMO_MODE=SM_4X4, CHANNEL_MODEL=TDL (4x4, MMSE, 주파수선택적, genie-aided CSI)
       │           run_pdsch_cl_4port_simulation()          ← MIMO_MODE=CL_4PORT, CHANNEL_MODEL≠TDL (4포트 CL, RI+PMI 적응, genie-aided CSI)
       │           run_pdsch_cl_4port_tdl_simulation()      ← MIMO_MODE=CL_4PORT, CHANNEL_MODEL=TDL (wideband PMI, CHAN_EST_METHOD로 LS/MMSE/DFT 선택 가능)
       │           run_pdsch_cl_8port_simulation()          ← MIMO_MODE=CL_8PORT, CHANNEL_MODEL≠TDL (8포트 CL, RI+PMI 적응)
       │           run_pdsch_cl_8port_tdl_simulation()      ← MIMO_MODE=CL_8PORT, CHANNEL_MODEL=TDL (wideband PMI, CHAN_EST_METHOD)
       │           run_pdsch_cl_32port_simulation()         ← MIMO_MODE=CL_32PORT, CHANNEL_MODEL≠TDL (32포트 CL, rank1~4 RI+PMI 적응)
       │           run_pdsch_cl_32port_tdl_simulation()     ← MIMO_MODE=CL_32PORT, CHANNEL_MODEL=TDL (wideband PMI, CHAN_EST_METHOD)
       │           run_pdsch_eigen_16port_simulation()      ← MIMO_MODE=EIGEN_16PORT, CHANNEL_MODEL≠TDL (SVD 개루프, genie-aided CSI)
       │           run_pdsch_eigen_16port_subband_simulation() ← MIMO_MODE=EIGEN_16PORT, CHANNEL_MODEL=TDL, EIGEN16_PRECODER_GRAN=SUBBAND (PRG별 프리코더, EIGEN16_CHAN_EST)
       │           run_pdsch_eigen_16port_tdl_simulation()  ← MIMO_MODE=EIGEN_16PORT, CHANNEL_MODEL=TDL, 그 외(WIDEBAND) (EIGEN16_CHAN_EST로 LS/MMSE/DFT 선택 가능)
       │           run_pdsch_tdl_simulation()               ← MIMO_MODE=SISO, CHANNEL_MODEL=TDL
       │           run_pdsch_dmrs_simulation()              ← MIMO_MODE=SISO, CHANNEL_MODEL≠TDL (기본)
       ├─ PBCH   → run_pbch_simulation()             ← CHANNEL_MODEL=AWGN
       │           run_pbch_fading_simulation()      ← CHANNEL_MODEL=FLAT_FADING/TDL (genie-aided CSI)
       ├─ PDCCH  → run_pdcch_simulation()             ← CHANNEL_MODEL=AWGN
       │           run_pdcch_fading_simulation()      ← CHANNEL_MODEL=FLAT_FADING/TDL (TX 후보만 genie-aided 페이딩)
       ├─ CSIRS  → run_csirs_simulation()
       ├─ SRS    → run_srs_simulation()
       ├─ PUSCH  → run_pusch_sm2x2_tdl_harq_simulation() ← HARQ_ENABLE=1, MIMO_MODE=SM_2X2, CHANNEL_MODEL=TDL (DL과 동일하게 TDL 전용, flat+HARQ 미지원)
       │           run_pusch_ul_eigen_bf_harq_simulation() ← HARQ_ENABLE=1, MIMO_MODE=UL_EIGEN_BF (flat/TDL 공용, 함수 내부 is_tdl 분기, 빔 추정은 attempt 0에서 1회만)
       │           run_pusch_ul_eigen_bf_2tx_harq_simulation() ← HARQ_ENABLE=1, MIMO_MODE=UL_EIGEN_BF_2TX (flat/TDL 공용, Genie는 매 attempt 정확, Eigen-BF는 attempt 0에서 1회만 추정, 랭크는 버스트당 1회 적응 결정·Genie/Eigen-BF 공유, 2026-09-01)
       │           run_pusch_ul_eigen_bf_4tx_harq_simulation() ← HARQ_ENABLE=1, MIMO_MODE=UL_EIGEN_BF_4TX (flat/TDL 공용, 2-Tx와 동일 설계를 K=4로 일반화, 랭크 1~4 적응, 2026-09-02)
       │           run_pusch_harq_simulation()          ← HARQ_ENABLE=1, 그 외 → SISO 전용 (다른 MIMO_MODE 조합은 config_parser.c가 미리 차단)
       │           run_pusch_sm2x2_simulation()        ← HARQ 비활성, MIMO_MODE=SM_2X2, CHANNEL_MODEL≠TDL (UE 2 레이어, gNB 2 Rx, CP-OFDM 전용)
       │           run_pusch_sm2x2_tdl_simulation()    ← HARQ 비활성, MIMO_MODE=SM_2X2, CHANNEL_MODEL=TDL (4개 독립 안테나쌍 TDL, CP-OFDM 전용)
       │           run_pusch_ul_eigen_bf_simulation() ← HARQ 비활성, MIMO_MODE=UL_EIGEN_BF, CHANNEL_MODEL≠TDL (UE 1 Tx→gNB 16 Rx, Genie MRC vs Eigen-BF 비교)
       │           run_pusch_ul_eigen_bf_tdl_simulation() ← HARQ 비활성, MIMO_MODE=UL_EIGEN_BF, CHANNEL_MODEL=TDL (공유 클러스터 게인 × 고정 공간벡터)
       │           run_pusch_ul_eigen_bf_2tx_simulation() ← HARQ 비활성, MIMO_MODE=UL_EIGEN_BF_2TX, CHANNEL_MODEL≠TDL (UE 2 Tx→gNB 16 Rx, Genie SVD vs Eigen-BF 비교, 랭크는 등력분배 용량 비교로 트라이얼당 적응 결정, 2026-09-01)
       │           run_pusch_ul_eigen_bf_2tx_tdl_simulation() ← HARQ 비활성, MIMO_MODE=UL_EIGEN_BF_2TX, CHANNEL_MODEL=TDL (공유 클러스터 게인 × 고정 공간행렬, 랭크 적응 포함)
       │           run_pusch_ul_eigen_bf_4tx_simulation() ← HARQ 비활성, MIMO_MODE=UL_EIGEN_BF_4TX, CHANNEL_MODEL≠TDL (UE 최대 4 Tx→gNB 16 Rx, Genie SVD vs Eigen-BF 비교, 랭크 1~4 적응, 2026-09-02)
       │           run_pusch_ul_eigen_bf_4tx_tdl_simulation() ← HARQ 비활성, MIMO_MODE=UL_EIGEN_BF_4TX, CHANNEL_MODEL=TDL (공유 클러스터 게인 × 고정 공간행렬, 랭크 적응 포함)
       │           run_pusch_simulation()              ← HARQ 비활성, MIMO_MODE=SISO, CHANNEL_MODEL≠TDL, TRANSFORM_PRECODING로 DFT-s-OFDM/CP-OFDM 토글
       │           run_pusch_tdl_simulation()          ← HARQ 비활성, MIMO_MODE=SISO, CHANNEL_MODEL=TDL, TURBO/DFE 둘 다 비활성 (ZF/MMSE 둘 다 지원)
       │           run_pusch_tdl_dfe_simulation()      ← HARQ 비활성, MIMO_MODE=SISO, CHANNEL_MODEL=TDL, PUSCH_DFE_ENABLE=1 (MMSE+DFE 연구 프로토타입)
       │           run_pusch_tdl_turbo_simulation()    ← HARQ 비활성, MIMO_MODE=SISO, CHANNEL_MODEL=TDL, PUSCH_TURBO_ENABLE=1 (DFE_ENABLE보다 우선, MMSE+turbo 연구 프로토타입)
       ├─ PUCCH  → run_pucch_format0_simulation()      ← PUCCH_FORMAT=0 (시퀀스 검출, AWGN 전용)
       │           run_pucch_format1_simulation()      ← PUCCH_FORMAT=1, CHANNEL_MODEL≠TDL (시퀀스+반복결합)
       │           run_pucch_format1_tdl_simulation()  ← PUCCH_FORMAT=1, CHANNEL_MODEL=TDL (반복마다 독립 채널, genie-aided MRC)
       │           run_pucch_format2_simulation()      ← PUCCH_FORMAT=2 (Polar, 짧음, AWGN 전용)
       │           run_pucch_format3_simulation()      ← PUCCH_FORMAT=3, CHANNEL_MODEL≠TDL (Polar+DFT-s-OFDM, 김)
       │           run_pucch_format3_tdl_simulation()  ← PUCCH_FORMAT=3, CHANNEL_MODEL=TDL (genie-aided ZF/MMSE)
       │           run_pucch_format1_tdl_harq_simulation() ← PUCCH_FORMAT=1, CHANNEL_MODEL=TDL, HARQ_ENABLE=1 (재전송마다 반복 옥카전 재드로우 후 누적, 무코딩이라 IR/Chase 무의미, genie bit-match로 종료 판정)
       │           run_pucch_format3_tdl_harq_simulation() ← PUCCH_FORMAT=3, CHANNEL_MODEL=TDL, HARQ_ENABLE=1 (N=64 Polar mother codeword + rate_matching.c 범용 circular buffer IR/Chase, genie bit-match로 종료 판정)
       ├─ PRACH  → run_prach_simulation()               ← PRACH_FORMAT=LONG/SHORT, CHANNEL_MODEL≠TDL (프리앰블 검출+TA 추정)
       │           run_prach_tdl_simulation()           ← PRACH_FORMAT=LONG/SHORT, CHANNEL_MODEL=TDL (RE grid + 다경로 페이딩)
       ├─ ULPC   → run_ulpc_simulation()                ← UL 폐루프 전력제어 시계열+PL 스윕, UL_PC_PL_VAR_*로 시변 PL 토글
       ├─ BER    → run_ber_sim()                        ← main.c 내부 정의, uncoded/no-OFDM 순수 BER 곡선(codec/OFDM 없는 경량 sanity check)
       └─ NONE   → run_legacy_sim()                     ← main.c 내부 정의, OFDM 기반 legacy 경로(LDPC/Polar 선택 가능)
```

PUSCH/PUCCH 분기는 2026-07-14까지 `main.c`에 `#include`조차 없어 이 함수들
전부가 config로 도달 불가능했다 (PDSCH에서 있었던 것과 같은 종류의 배선 누락,
같은 날 함께 수정).

PDSCH 분기는 `main.c`에서 `OLLA_ENABLE`/`USE_DMRS`/`HARQ_ENABLE`/`MIMO_MODE`/
`CHANNEL_MODEL` 값으로 실제 라우팅된다 (2026-07-13까지는 이 배선이 빠져 있어
`run_pdsch_dmrs_simulation()` 외의 PDSCH 함수들이 config로 도달 불가능한 상태였음).
if/else if 우선순위(위에서 먼저 매치되는 조건이 이김):

1. `OLLA_ENABLE=1` → 다른 모든 조건보다 최우선, `MIMO_MODE`로 세분화:
   SIMO_MRC → `run_pdsch_olla_simo_mrc_simulation()`, SM_2X2 →
   `run_pdsch_olla_sm2x2_simulation()`, 그 외 → `run_pdsch_olla_simulation()`
   (SISO). SISO/SIMO_MRC/SM_2X2 외 값은 `config_parser.c`가 CFG_ERR로
   차단(전용 함수 없어 SISO로 조용히 떨어지는 오배선 방지, 2026-09-01)
2. `USE_DMRS=0` → `run_pdsch_simulation()`
3. `HARQ_ENABLE=1` — 전용 함수가 있는 조합만 개별 라우팅, 그 외는 전부
   `run_pdsch_harq_simulation()`(SISO 전용)로 떨어짐:
   SM_4X4/CL_4PORT/CL_8PORT/CL_32PORT/MU_MIMO/BEAM_MGMT → 채널 모델
   무관(MU_MIMO/BEAM_MGMT는 함수 내부 `is_tdl` 분기, 나머지는 별도 TDL
   전용 함수), SM_2X2+TDL/SIMO_MRC+TDL → TDL 한정(flat 미지원).
   **`EIGEN_16PORT`, 그리고 `SM_2X2`/`SIMO_MRC`의 flat 버전은 HARQ
   전용 함수가 없다** — `config_parser.c`가 이 화이트리스트 밖의
   조합을 `HARQ_ENABLE=1`과 함께 쓰면 `CFG_ERR`로 명시적으로 막는다
   (2026-09-01 추가 — 이전엔 이 조합들이 조용히 `run_pdsch_harq_simulation()`
   (SISO, `MIMO_MODE` 완전 무시)으로 떨어지면서도 헤더에는 원래 `MIMO_MODE`가
   찍히는 오배선이 있었음, `docs/analysis/history.md` 2026-09-01 항목 참조.
   `MU_MIMO`/`BEAM_MGMT`는 같은 날 전용 HARQ 함수가 생기면서 화이트리스트에
   순차 편입됨).
   `main.c`에 새 `MIMO_MODE`×`HARQ_ENABLE` 조합을 추가하면 `config_parser.c`의
   같은 화이트리스트도 반드시 함께 갱신할 것.
4. `HARQ_ENABLE=0`인 상태에서 `MIMO_MODE`별 분기 — 모든 MIMO 모드
   (`MU_MIMO`/`BEAM_MGMT`/`SIMO_MRC`/`SM_2X2`/`SM_4X4`/`CL_4PORT`/
   `CL_8PORT`/`CL_32PORT`/`EIGEN_16PORT`)가 각각
   flat/TDL 두 갈래.

PUSCH는 `HARQ_ENABLE=1`일 때 `MIMO_MODE=SM_2X2`+`CHANNEL_MODEL=TDL`만
`run_pusch_sm2x2_tdl_harq_simulation()`으로 라우팅되고(2026-09-01 추가, DL
`run_pdsch_sm2x2_tdl_harq_simulation()`을 1:1 포팅 — DL도 flat+HARQ가 없는
비대칭이라 UL도 동일하게 TDL 전용으로 유지), `MIMO_MODE=UL_EIGEN_BF`/
`UL_EIGEN_BF_2TX`/`UL_EIGEN_BF_4TX`는 각각 `run_pusch_ul_eigen_bf_harq_simulation()`/
`run_pusch_ul_eigen_bf_2tx_harq_simulation()`/`run_pusch_ul_eigen_bf_4tx_harq_simulation()`
으로 채널 모델 무관하게 라우팅(flat/TDL 둘 다 함수 내부 분기, SM_2X2와 달리 TDL 제한 없음),
그 외는 전부 `run_pusch_harq_simulation()`(SISO 전용)로 떨어짐.
`MIMO_MODE=SM_2X2`+`CHANNEL_MODEL=FLAT_FADING`+`HARQ_ENABLE=1` 등
전용 함수가 없는 조합은 `config_parser.c`가 `CFG_ERR`로 차단한다
(PDSCH의 HARQ 화이트리스트와 같은 원리).
`HARQ_ENABLE=0`일 때 `MIMO_MODE=SM_2X2`이면 UL SU-MIMO(2026-09-01 추가, PUSCH 최초의
MIMO — 이전까지는 전부 단일안테나였던 DL과의 구조적 비대칭을 일부 해소, `tasks/todo.md`
참조)로 라우팅되고, 그 외(SISO)는 기존 파형 토글 로직을 그대로 탄다. UL SM_2X2는
TS 38.211 §6.3.1.4(Transform Precoding은 1개 레이어 초과 전송 미지원)에 따라
CP-OFDM(`TRANSFORM_PRECODING=0`) 전용이며, 이 조합도 `config_parser.c`가 강제한다.

---

## 3. 설정 파라미터 설계

### 핵심 원칙: MCS가 단일 진입점

`MODULATION`, `CODE_RATE`를 config에 직접 쓰지 않는다.  
`MCS_INDEX` + `MCS_TABLE`만 지정하면 나머지는 자동 결정된다.

```
config 파일
  MCS_INDEX = 10
  MCS_TABLE = TABLE1
       │
       ▼ (calc_derived 내부)
  get_mcs_entry(10, TABLE1)
       │
       ├─ cfg->modulation = "16QAM"
       ├─ cfg->codeRate   = 0.3320
       └─ spectralEff     = 1.328 bits/RE
```

### 채널 타입별 코딩 방식 자동 강제

| 채널 | 코딩 | 변조 | 근거 |
|------|------|------|------|
| PBCH | POLAR (강제) | QPSK (강제) | TS 38.212 §7.3.3 |
| PDCCH | POLAR (강제) | QPSK (강제) | TS 38.212 §7.3.2 |
| PDSCH | LDPC (강제) | MCS 테이블 | TS 38.212 §7.2 |
| NONE / 기타 | config 유지 | MCS 테이블 | — |

### MCS 테이블 3종 (TS 38.214)

| 테이블 | 표준 참조 | 변조 범위 | 용도 |
|--------|-----------|-----------|------|
| TABLE1 | Table 5.1.3.1-1 | QPSK ~ 64QAM (MCS 0~28) | 기본 |
| TABLE2 | Table 5.1.3.1-2 | QPSK ~ 256QAM (MCS 0~27) | 고효율 |
| TABLE3 | Table 5.1.3.1-3 | QPSK ~ 64QAM (MCS 0~28) | 저SE (RedCap 등) |

---

## 4. 데이터 전달 구조

### 4-1. 설정 데이터 — 구조체 값 복사 후 const 포인터 전달

```c
/* config_parser.h */
typedef struct {
    int    bandwidthMHz;
    int    mcsIndex;
    char   modulation[64];
    double codeRate;
    char   physicalChannel[64];
    ...
} L1Config;

/* 사용 패턴 */
ConfigParser parser;
config_parser_load(&parser, "sim_config.txt"); // &parser: 쓰기 포인터
L1Config cfg = config_parser_get(&parser);     // 값 복사 (이후 독립)

run_pdsch_simulation(&cfg);   // const L1Config * → 읽기 전용 전달
```

**설계 의도:**
- 시뮬 함수는 설정을 바꾸면 안 되므로 `const` 강제
- 값 복사로 parser 변경이 cfg에 영향 없음

### 4-2. 심볼 버퍼 — in / out 포인터 쌍 분리

```c
typedef struct { double re, im; } cx_t;

/* 호출자가 버퍼 소유, 함수는 채우기만 */
void qam_modulate(const int  *bits,   // 읽기 전용 입력
                  int         nbits,
                  const char *mod,
                  cx_t       *syms);  // 쓰기 출력 (호출자 버퍼)

void awgn_add_noise(const AWGNChannel *ch,
                    const cx_t *in,   // 읽기 전용
                    int n, int nfft,
                    cx_t       *out); // 쓰기 출력
```

**버퍼 소유권:**
```
run_pdsch_simulation()      ← malloc / free 책임
    │
    ├─ qam_modulate   (... syms)      // syms  빌려줌
    ├─ awgn_add_noise (... rxsyms)    // rxsyms 빌려줌
    └─ ldpc_decode    (... decoded)   // decoded 빌려줌
```

### 4-3. 리소스 그리드 — 1D 평탄 배열 + 인덱스 배열

```c
cx_t *tx_grid  = malloc(active * sizeof(cx_t)); // active = num_rb * 12
int  *pilot_pos = malloc(num_pilots * sizeof(int));
int  *data_pos  = malloc(num_data   * sizeof(int));

dmrs_pilot_indices(num_rb, pilot_pos); // 파일럿 RE 위치 인덱스
dmrs_data_indices (num_rb, data_pos);  // 데이터 RE 위치 인덱스

// 그리드 배치
for (int p = 0; p < num_pilots; p++)
    tx_grid[pilot_pos[p]] = dmrs_sym[p];

// 채널 통과 후 파일럿 RE만 추출
for (int p = 0; p < num_pilots; p++)
    rx_pilots[p] = rx_grid[pilot_pos[p]];
```

**설계 의도:**
- 2D(RE × 심볼)를 1D로 펼쳐 인덱스 계산 단순화
- 위치 정보(인덱스 배열)와 데이터(심볼 배열) 분리 → 재사용 가능

### 4-4. 코덱 상태 — 구조체 + init / free 패턴

```c
typedef struct {
    int    info_size;
    int    coded_size;
    double codeRate;
    /* 내부 패리티 행렬 (동적 할당) */
} LDPCCodec;

/* 생성 → 사용 → 해제 */
LDPCCodec ldpc;
ldpc_init  (&ldpc, K, codeRate);      // 내부 행렬 malloc
ldpc_encode(&ldpc, input, output);    // ldpc 읽기 전용 참조
ldpc_decode(&ldpc, llr, 25, decoded);
ldpc_free  (&ldpc);                   // 내부 malloc 해제
```

동일 패턴: `PolarCodec`, `OFDMCtx`, `AWGNChannel`, `FlatFadingChannel`

**C에서 객체지향 흉내:**
```
C++ class  →  C struct + init() + free()
생성자     →  ldpc_init()
소멸자     →  ldpc_free()
멤버 함수  →  ldpc_encode(&ldpc, ...)
```

---

## 5. 변수 스코프 원칙

### 전역 변수 없음

| 데이터 종류 | 위치 | 이유 |
|-------------|------|------|
| 시뮬 버퍼 (bits, syms, llr) | 함수 로컬 `malloc` | 함수마다 크기 다름, 독립성 필요 |
| 설정값 (L1Config) | 값 복사 후 `const *` 전달 | 읽기 전용, 여러 함수 공유 |
| 코덱 상태 (LDPCCodec 등) | 로컬 구조체 | 초기화 비용 크지만 함수별 독립 |
| 노이즈 상태 (`randn` spare) | `static` 로컬 | 호출 간 상태 필요, 외부 노출 불필요 |
| **전역 변수** | **없음** | 데이터 오염, 확장성 저하 |

### 버퍼 생애주기 패턴

```c
void run_xxx_simulation(const L1Config *cfg) {

    /* ① 시뮬 전체에서 한 번만 malloc */
    cx_t *syms = malloc(nsym * sizeof(cx_t));
    ...

    /* ② SNR 루프 — 버퍼 재사용, 내용만 덮어씀 */
    for (double snr = cfg->snrStart; ...) {
        for (int trial = 0; trial < cfg->numTrials; trial++) {
            qam_modulate(..., syms);   // syms 덮어씀
            ...
        }
    }

    /* ③ 함수 끝에서 반드시 해제 */
    free(syms);
}
```

**trial마다 malloc/free 하지 않는 이유:**
- 동적 할당은 OS 시스템 콜 → 수만 번 반복 시 병목
- 버퍼 크기가 trial마다 같으므로 재사용이 안전

---

## 6. BER/ — 독립 툴 설계

### 모듈 구성

```
BER/
├── src/main.c    ← 시뮬 루프 + 경량 config 파서 (인라인)
├── src/qam.c     ← Gray-coded QAM 변조/복조 + 이론 BER
└── src/awgn.c    ← Box-Muller AWGN 생성
```

PHY/ 코드에 의존하지 않고 3개 파일로 자급자족.

### QAM 정규화 (단위 평균 심볼 전력)

```
k비트 PAM 정규화 계수 = sqrt(2 * (4^k - 1) / 3)

  k=1 (QPSK)   : sqrt(2)    심볼 ±1/√2 per component
  k=2 (16QAM)  : sqrt(10)   ±1/√10, ±3/√10
  k=3 (64QAM)  : sqrt(42)   ±1/√42 ... ±7/√42
  k=4 (256QAM) : sqrt(170)  ±1/√170 ... ±15/√170
```

### Eb/N0 → 잡음 분산 변환

```
Es/N0 = Qm × Eb/N0                    (선형)
sigma = sqrt(1 / (2 × Qm × Eb/N0))   (성분당 표준편차)
```

단위 전력 심볼 기준이므로 Es = 1.

### 이론 BER (Proakis & Salehi 5th Ed. Eq.5-2-79)

```
BER ≈ (4/Qm) × (1 - 1/√M) × Q(√(3·Qm/(M-1) · Eb/N0))
Q(x) = 0.5 × erfc(x / √2)
```

QPSK(M=4): 정확한 값 / 16·64·256QAM: tight 근사

---

## 7. 전체 TX→RX 데이터 흐름

```
[설정]
  L1Config (구조체 — const 포인터로 전달)

[TX]
  int[]   bits
    └─ qam_modulate()  →  cx_t[]  syms
    └─ 리소스 그리드 배치  →  cx_t[]  tx_grid

[채널]
  cx_t[]  tx_grid
    └─ awgn_add_noise()  →  cx_t[]  rx_grid

[RX]
  cx_t[]  rx_grid
    └─ ls_estimate()      →  cx_t[]  h_est     (채널 추정)
    └─ interpolate()      →  cx_t[]  h_full
    └─ mmse_equalize()    →  cx_t[]  eq_data   (등화)
    └─ qam_demap_llr()    →  double[] llr      (소프트 비트)
    └─ ldpc_decode()      →  int[]   decoded   (복호)

[집계]
  BER = bit_errors / total_bits
  BLER = block_errors / num_trials
```

각 단계는 `const in*` → `out*` 포인터 쌍으로 연결.  
버퍼는 SNR 루프 밖에서 한 번만 `malloc`, trial 반복마다 재사용.

---

*이 문서는 코드 리뷰 및 구조 파악을 위한 참조 문서입니다.*
