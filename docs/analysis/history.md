# 개발 이력 아카이브 (2026-05-24 ~ 2026-07-22)

> 이 문서는 2026-08-02 `CLAUDE.md` 분리 작업에서 이관된 완료 개발 이력이다.
> 원본 전체는 `docs/analysis/CLAUDE.md.original-20260802.bak` 참조.
> 상시 컨텍스트(프로필/답변원칙 등)는 `CLAUDE.md`, 현재/향후 작업은 `tasks/todo.md`, 교훈은 `tasks/lessons.md` 참조.

---

## ✅ 완료된 작업 (시간순)

### Massive MIMO UL Rx Beamforming — Eigen 기반 구현 완료
업링크 수신 빔포밍에서 채널 공간 공분산 행렬 추정 → EVD(고유값 분해) → Eigenbeam 추출 → UL 수신 결합. 관련 표준: 3GPP TS 38.214 (UL MIMO), Rel-17 UL 공간 다중화.

### PHY LLS — DL/UL 기능 대거 확장 (2026-07-13)
- **MIMO**: PDSCH에 SU-MIMO 2x2 추가 — SIMO_MRC(1x2 수신 다이버시티), SM_2X2(2계층 공간다중화, ZF/MMSE). 다이버시티/검출기 이득 수치 검증 완료
- **HARQ**: Circular buffer rate matching + IR/Chase 소프트 컴바이닝(`rate_matching.c`). IR이 Chase보다 항상 우세함을 실측 확인
- **TDL 주파수선택적 페이딩**: 근사 6탭 NLOS PDP(`tdl.c`, TS 38.901 표 근사치 — 정확한 표 아님, 문서화됨). 주파수 선택성 실증 확인
- **PUSCH**: Transform Precoding(DFT-s-OFDM, `dft_precode.c`) — PAPR 저감 실증(8.02dB→0.00dB). PUSCH+TDL+ZF/MMSE 등화 조합에서 "DFT precoding은 선형 등화기와 결합 시 주파수선택적 채널에서 오히려 손해"라는 결론을 수식적으로 유도·검증(SC-FDMA 문헌과 일치)
- **PUCCH Format 0/1/2/3**: 시퀀스 기반 검출(F0), 반복/코히런트 결합(F1, 이득 이론과 정확히 일치 확인), 코딩된 UCI(F2/F3, Reed-Muller 대신 기존 Polar 재사용)
- **버그 수정**: `polar.c`의 frozen-bit 선택이 rate-matching shortening 위치를 모르고 있어 info bit가 파괴되는 기존 버그 발견·수정(PDCCH도 영향받던 버그, PUCCH F2 K별 스윕 테스트 중 발견). 수정 후 N=32/64 전 K 범위·전 rate-matching 영역에서 무손실 라운드트립 190/190 통과 확인
- 전부 AWGN/flat fading 기준, 각 기능은 독립적으로 격리 구현(조합 확장은 다음 과제) — 상세 설계는 `STRUCTURE.md` 참조

### PHY LLS — 기능 조합 1단계: TDL + MIMO (2026-07-13)
`run_pdsch_simo_mrc_tdl_simulation()` / `run_pdsch_sm2x2_tdl_simulation()` 추가 — Tx-Rx 안테나 쌍마다 독립 TDL tap-set을 드로우해 RE별 2x2(또는 1x2) 채널 행렬 구성. 기존 검출 함수(`mrc_combine`/`mimo_zf_detect`/`mimo_mmse_detect`)는 RE 단위 순수 함수라 수정 없이 재사용. SM_2X2는 flat 채널처럼 파일럿 RE 전체를 평균해 단일 `h_hat`을 구하는 방식이 주파수선택적 채널에서는 틀리므로, LS+보간(`ls_estimate`/`interpolate_channel`)을 안테나 쌍마다(4회) 수행해 RE별 채널을 얻도록 변경. 부수 발견: `main.c`가 `USE_DMRS=1`일 때 `MIMO_MODE`/`HARQ_ENABLE`/`CHANNEL_MODEL`을 전혀 참조하지 않아, 기존에 구현된 SIMO_MRC/SM_2X2/HARQ/TDL(SISO) 함수들이 config로 도달 불가능한 dead code였음 — 전체 배선 수정. 검증: SM_2X2/SIMO_MRC 모두 SNR 증가에 따라 BLER 단조 감소, 동일 SNR에서 TDL이 flat fading 대비 항상 열화(예상된 방향)됨을 실측 확인. TDL+HARQ, MIMO+HARQ 조합은 미포함(다음 단계).

### PHY LLS — 기능 조합 2단계: TDL + MIMO(SM_2X2) + HARQ 전체 결합 (2026-07-13)
`run_pdsch_sm2x2_tdl_harq_simulation()` 추가 — 2계층 공간다중화(2x2, ZF/MMSE) + 주파수선택적 TDL 채널(안테나 쌍마다 독립 tap-set, RE별 채널) + circular-buffer IR/Chase 소프트 컴바이닝을 한 함수에 결합. 레이어 A/B 각각 독립 mother LDPC 코드워드 + 독립 soft buffer, 채널은 HARQ 재전송마다 재드로우(시간 다이버시티). 단순화(문서화됨): 두 레이어가 같은 재전송 occasion을 공유한다고 가정(레이어별 완전 독립 HARQ 프로세스 스케줄은 아님) — 한쪽 CRC만 통과해도 다른 쪽이 실패하면 계속 재전송. `main.c` dispatch에 HARQ_ENABLE 분기 세분화: MIMO_MODE=SM_2X2 && CHANNEL_MODEL=TDL이면 이 신규 함수로, 그 외 HARQ 조합은 기존 SISO `run_pdsch_harq_simulation()`으로 폴백. 검증: BLER(HARQ) ≤ BLER(1st) 항상 성립(재전송 이득), AvgTx가 SNR 증가에 따라 감소, HARQ 적용 시 동일 SNR에서 BLER이 HARQ 없는 SM_2X2+TDL 대비 크게 개선(20dB에서 0.93→0.29), IR이 Chase보다 우세한 기존 경향 재확인.

### PHY LLS — 기능 조합 3단계: TDL + MIMO(SIMO_MRC) + HARQ (2026-07-14)
`run_pdsch_simo_mrc_tdl_harq_simulation()` 추가 — 단일 코드워드(SIMO는 레이어 1개라 SM_2X2 조합보다 단순) + MRC(1x2 수신 다이버시티) + 안테나 브랜치마다 독립 TDL tap-set(매 HARQ attempt 재드로우) + circular-buffer IR/Chase. `run_pdsch_harq_simulation()`의 단일 코드워드 재전송 루프에 `run_pdsch_simo_mrc_tdl_simulation()`의 채널/추정 블록을 그대로 이식. `main.c` dispatch에 SIMO_MRC+TDL 분기 추가 — SIMO_MRC/SM_2X2 두 MIMO 모드 모두 TDL+HARQ까지 커버. 검증: BLER(HARQ)가 BLER(1st) 대비 전 SNR에서 크게 개선(10dB에서 0.957→0.093), AvgTx 단조 감소, IR이 Chase보다 우세한 경향 재확인. 이로써 "기능 간 조합"(TDL×MIMO×HARQ) 완료.

### PHY LLS — TDL을 PUCCH로 확장(Format 1/3 + 페이딩), PUSCH/PUCCH 배선 수정 (2026-07-14)
조사 결과 PUSCH는 이미 `run_pusch_tdl_simulation()`으로 TDL 구현이 끝나 있었음 — 실제 남은 작업은 PUCCH뿐. PUCCH는 DMRS/LS 채널추정 파이프라인이 전혀 없어(F0~F3 전부 AWGN, H=1 가정), 새 파일럿 구조를 설계하는 대신 **genie-aided(완벽한 CSI)** 방식 선택 — `tdl.c`의 `tdl_channel_apply()`가 `h_out`으로 실채널을 노출하도록 설계돼 있어 이 접근과 정확히 맞아떨어짐. `run_pucch_format1_tdl_simulation()`: 반복 심볼마다 독립 TDL 재드로우, 매치드필터 합산을 채널가중 MRC로 일반화. `run_pucch_format3_tdl_simulation()`: `run_pusch_tdl_simulation()`의 TDL+DFT-precoding 등화 수식을 그대로 재사용, LS추정 단계만 생략하고 genie `h_known`을 직접 입력. 부수 발견: `main.c`가 PUSCH/PUCCH를 `#include`조차 하지 않아 이 채널들이 config로 전혀 도달 불가능했음 — 함께 수정. 검증: F1/F3 모두 TDL에서 크래시/NaN 없이 SNR에 따라 단조 감소, F1은 반복 횟수 늘릴수록(1→4) 결합 이득 유지(-10dB에서 0.147→0.01), F3는 MMSE가 ZF보다 우세. F0/F2는 이번 범위 밖(AWGN 고정 유지).

### PHY LLS — PRACH 추가(UL 랜덤 접속, 프리앰블 검출 + TA 추정) (2026-07-14)
`prach.c`/`prach.h`: 단일 함수 `run_prach_simulation()`. PRACH는 스펙상 본래 목적이 초기 타이밍 획득(TA)이라, 단순 시퀀스 검출에서 그치지 않고 TA 추정까지 포함. ZC 루트시퀀스 `x_u(n)=exp(-j*pi*u*n*(n+1)/L_RA)` — L_RA(839 long/139 short)가 둘 다 소수라 근사 없이 정확한 공식 적용. 미지의 전파지연을 순환천이로 모델링, ZC의 이상적 순환 자기상관을 그대로 활용해 프리앰블 인덱스와 TA를 단일 순환상관 스윕으로 동시 추정. 검증: SHORT/LONG 둘 다 크래시 없이 SNR 증가에 따라 프리앰블 오검출률·TA MAE 단조 감소.

### PHY LLS — MMSE+TDL+PUSCH precoding 비선형 등화(DFE) 탐색 (2026-07-14)
`run_pusch_tdl_dfe_simulation()`(`PUSCH_DFE_ENABLE=1`): MMSE+DFT-precoding 잔여 ISI를 실제로 계산해서 제거하는 블록 병렬간섭제거(block PIC) 프로토타입. 실측 결과(16QAM, MCS10, 20RB, TDL DS=300ns): 15dB에서는 DFE가 오히려 악화(0.86→0.877), 20~25dB에서는 소폭 개선, 30dB 이상에서는 둘 다 무오류로 수렴 — "이 채널·MCS 조합에서는 비선형 등화의 이득이 크지 않고 SNR 문턱 이하에서 손해"라는 결론(300 trial 기준).

### PHY LLS — Turbo 등화(반복적 소프트 등화-복호 교환) 확장 (2026-07-14)
`run_pusch_tdl_turbo_simulation()`(`PUSCH_TURBO_ENABLE=1`): DFE의 하드 슬라이싱을 LDPC의 소프트(extrinsic) 피드백으로 대체. 신규 라이브러리 함수 2개: `ldpc_decode_soft()`, `qam_soft_symbol()`. 정확성 검증: iters=1일 때 turbo 결과가 no-DFE와 정확히 일치함을 수식적으로 증명. 실측 결과(16QAM, MCS10, 20RB, TDL DS=300ns, 3회 반복): turbo가 거의 전 SNR에서 no-DFE와 hard-DFE 둘 다를 능가(15dB에서 hard-DFE는 악화됐지만 turbo는 개선).

### PHY LLS — PUCCH F1/F3 + TDL을 HARQ와 결합 (2026-07-14)
`run_pucch_format1_tdl_harq_simulation()`, `run_pucch_format3_tdl_harq_simulation()`: F2/F3 UCI는 스펙상 별도 CRC가 없어 genie(정답) 비트 일치를 종료 판정 기준으로 대체. F3는 N=64 Polar mother codeword에 `rate_matching.c`의 범용 circular-buffer를 재사용. 검증: F1은 저SNR에서 BLER(HARQ)≪BLER(1st), F3는 MMSE가 ZF보다 우세, K=11에서는 IR이 Chase보다 근소 우위.

### PHY LLS — PRACH를 OFDM 그리드(RE) + TDL 다경로로 정교화 (2026-07-14)
`run_prach_tdl_simulation()`: 기존 ZC 시퀀스 도메인 순환시프트 지연 모델 위에 `tdl.c` 기반 진짜 다경로 페이딩을 얹음. 핵심 통찰: L_RA-포인트 순환시프트가 DFT shift 정리에 의해 연속시간 지연과 수학적으로 정확히 등가임을 증명. 검증: TDL 버전은 SNR이 높아져도 다경로 지연확산에 의한 noise floor에 수렴(채널 자체 한계, 물리적으로 타당). 이로써 "확정 순서"(PUCCH F1/F3+TDL+HARQ, PRACH OFDM 그리드 정교화) 완료.

### 파일 헤더 배너 정비 / Git 히스토리 정리 (2026-07-15)
전체 소스/헤더 파일(52개, `PHY/src/*.c` + `PHY/include/*.h`)에 박스형 파일 헤더 배너(파일명 + 한 줄 설명 + `Author: Inseok Kang`) 추가. `origin/develop`이 별도 세션/기기에서 `PHY/common`+`PHY/lls_sim`+`PHY/ber_sim` 구조로 재구조화된 채 갈라져 있던 것을 발견 — 기능 자체는 로컬 WSL 작업이 최신이라 판단해, 원격의 재구조화 히스토리는 `origin/archive/common-lls-sim-refactor` 브랜치로 보존하고 로컬 기준으로 `develop`을 force-push. 이후 이 저장소의 `PHY/src`/`PHY/include` 평면 구조가 기준. (이 사고의 교훈은 `tasks/lessons.md` 참조)

### PHY LLS — 4×4 SU-MIMO 공간 다중화 (SM_4X4) 추가 (2026-07-21)
`mimo_channel_draw/apply_4x4`, `mimo_zf/mmse_detect_4x4` 구현. 4×4 Gauss-Jordan 역행렬(`inv4x4`, 부분 피벗팅)이 4×4 MMSE의 핵심. MMSE de-biasing은 WH = I − N₀·A⁻¹ 항등식으로 α_t를 A⁻¹ 대각만으로 계산(W 행렬 전체 불필요). FDM 파일럿 설계: 6·num_rb 파일럿을 4 레이어에 라운드로빈 분배(nppl = floor / 4). `run_pdsch_sm4x4_simulation()` — 레이어별 독립 코드워드, DMRS LS 채널 추정.

### PHY LLS — Type I SP 4포트 CSI-RS 코드북 생성기 (2026-07-21)
표준: TS 38.214 Sec 5.2.2.2.1 (N1=2, N2=1, Ng=2, O1=4, P=4). DFT 빔 벡터 v_l = [1, e^{jπl/4}]^T, 코피에이징 φ_n = e^{jπn/2}. Rank-1(32 코드워드): W = (1/2)[v_l; φ_n·v_l]. Rank-2 변형A(16): 직교 빔쌍(l, l+4), v_l^H v_{l+4} = 0 수학적 보장. Rank-2 변형B(32): 동일빔 교차편파, 부호 반전으로 직교성 확보. `codebook_type1_sp_4port_pmi_search()`: rank-1 PMI 전수탐색(기존 4Rx 호환). 검증: 전 80 코드워드 ‖W[:,j]‖²=1, 직교성 <1e-10 확인.

### PHY LLS — RI+PMI 동시 적응 선택 (CL_4PORT) 추가 (2026-07-22)
`codebook_type1_sp_4port_ri_pmi_select()`: 80 후보 전수탐색, adaptive/R1-best/R2-best 동시 반환. Rank-1: C₁ = log₂(1 + ‖H·W‖²/N₀). Rank-2: C₂ = Σ_l log₂(1 + α_l/(1−α_l)), α_l = 1 − N₀·(A⁻¹)_ll (2×2 Gramian). `mrc_combine_4rx()`: 4-Rx MRC(rank-1 프리코딩 후 유효 채널에 적용). `mimo_mmse_detect_4rx2()`: 4Rx×2Layer 과결정 MMSE(2×2 Gramian으로 축소, inv2x2 재사용). `run_pdsch_cl_4port_simulation()`: adaptive/R1-fixed/R2-fixed BLER 3열 동시 출력 + 공정 비교(동일 채널 H, 동일 노이즈 n, 동일 정보 비트) — iid Rayleigh 4×4에서 R1선택률=0% 검증됨(iid 채널은 항상 rank-2 용량이 우세, 공간 상관 채널에서 rank-1 선택 발생).

### PHY LLS — UL Closed-Loop Power Control (ULPC) 추가 (2026-07-22)
표준: TS 38.213 §7.2.1 PUSCH 전력 제어. 전력 공식: P_tx(i) = min(P_CMAX, P_0 + α·PL + f(i)) [dBm]. TPC 명령: δ ∈ {−1, 0, +1, +3} dB(Table 7.2.1-1), 데드밴드 ±0.5dB, genie-aided. f(i) = f(i-1) + δ 누산, ±30dB 클램프(구현 정의). 출력 Part 1: 시계열 수렴(고정 PL) — OL vs CL P_tx/SINR/f(i)/TPC 서브프레임별. Part 2: PL 스윕 — OL vs CL 정상상태 비교, P_CMAX 클램핑 구간 표시. Part 3: α 설계 함의 — P_CMAX 한계 PL = P_CMAX − SINR_target − N_floor. α=0.8 검증: PL=100dB에서 f_ss=14dB, SINR_CL=+10.4dB(목표 10dB), 6SF만에 수렴. P_CMAX=23dBm(NR Power Class 3, TS 38.101-1), 클램핑 한계 PL=124.4dB.

### PHY LLS — 공간상관 채널에서 CL_4PORT rank-1 선택 거동 검증 (2026-08-02)
`mimo_apply_tx_correlation_4x4(h, rho)` 추가 — Kronecker 모델(H_corr = H_iid · Rtx^(1/2)), Rtx는 XPOL 편파 그룹별(포트 0-1, 포트 2-3) 2×2 지수상관 블록, 교차편파 상관은 0(표준 dual-pol 단순화). 2×2 블록 제곱근은 닫힌형(a=(√(1+ρ)+√(1-ρ))/2, b=(√(1+ρ)-√(1-ρ))/2)이라 별도 고유분해 불필요, ρ=0이면 완전히 기존 i.i.d. 동작으로 축소. `SPATIAL_CORR_TX`(MIMO_MODE=CL_4PORT 전용) config로 제어. RX는 비상관 유지(gNB 압축 배열이 핵심 변수).

**실측 결과**: SNR=10dB에서는 ρ=0.95까지 R1선택률이 0%에 머물다가 ρ≥0.99에서야 0.1%로 미미하게 나타남. SNR=-5dB(저SNR)에서는 ρ 증가에 따라 R1선택률이 0.0→0.1→0.3→0.4%(ρ=0→0.8→0.9→0.99)로 단조 증가하는 뚜렷한 경향은 확인되나, ρ→1에서도 0.4%에 그침. BER_Adapt 자체는 ρ 증가에 따라 꾸준히 열화(예상된 방향).

**원인 분석(수식으로 확인, 버그 아님)**: 닫힌형 2×2 블록 제곱근에서 ρ→1이면 a=b=1/√2로 수렴 — 즉 편파 쌍 내 두 안테나가 완전히 상관돼도 "그 편파의 유효 안테나 1개"로 축소될 뿐, **두 편파(pol1/pol2)는 여전히 완전히 독립**이라 유효 공간자유도가 2개 그대로 남는다. 코드북의 rank-2 "동일빔 교차편파 다이버시티" 변형(i1_3=1)이 정확히 이 2개의 독립 편파 자유도만 있으면 성립하도록 설계되어 있어, 동일편파 내부 상관이 아무리 심해져도 RI+PMI는 거의 항상 이 변형을 통해 rank-2를 계속 선택한다. 즉 이번 검증에서 "동일편파 상관만으로는 rank-1이 거의 선택되지 않는다"는 것 자체가 결론 — 진짜로 rank-1 전환을 보려면 교차편파(XPD) 누설 상관을 추가로 모델링해야 하며, 이는 후속 과제로 `tasks/todo.md`에 남김(사용자와 확인 후 이번 라운드는 여기서 마무리, 2026-08-02).

### PHY LLS — 완성도 작업 1단계: PBCH/PDCCH에 페이딩 채널 추가 (2026-08-02)
"NTN 같은 새 영역보다 기존 LLS 완성도 우선"이라는 사용자 확인(2026-08-02, `tasks/todo.md` 참조) 이후 첫 작업. PBCH/PDCCH는 이 LLS에서 유일하게 AWGN 전용으로 남아있던 채널이라 최우선 착수.

- `run_pbch_fading_simulation()`: PUCCH와 같은 genie-aided CSI 방식(DMRS 파이프라인 없음). FLAT_FADING은 SSB 전체(E/bps=432 심볼)에 단일 탭 유지, TDL은 심볼 인덱스를 RE로 취급해 SCS_KHZ 기준 진짜 다경로 페이딩 적용. ZF/MMSE 둘 다 지원.
- `run_pdcch_fading_simulation()`: blind-decoding 후보들 중 **실제 송신된 후보(TX candidate)에만** 페이딩 채널 적용 — 나머지 후보는 실제 전송과 무관한 순수 노이즈 드로우라 채널을 적용할 대상 자체가 없음(기존 구조 그대로 유지). ZF/MMSE 등화 후 TX 후보 전용 평균 유효 노이즈분산을 계산해(기존 `run_pucch_format3_tdl_simulation()`의 단순화와 동일 패턴) 기존 스칼라 `qam_demap_llr()` 경로에 그대로 흘려보냄.
- `main.c`에 `CHANNEL_MODEL` 분기 추가(AWGN=기존 함수, 그 외=신규 함수), `config/sim_config.txt`의 `CHANNEL_MODEL` 주석을 "PDSCH+DMRS 전용" 옛 문구에서 실제 지원 채널 목록으로 갱신.
- **구현 중 발견한 버그(내 코드, 기존 코드 아님) 및 수정**: `qam_demap_llr_mmse()`는 이미 `mmse_equalize()`를 거친 등화 신호를 입력으로 기대하는데(비교 대상인 실수 PAM 레벨이 α로만 스케일되고 채널 위상은 보정 안 됨 — pdsch.c/pusch.c 기존 호출부는 전부 `mmse_equalize()` 먼저 호출 후 그 결과를 넘기고 있었음), PBCH 초안 코드는 원시 rx를 바로 넘겨서 SNR과 무관한 BLER floor(~0.77~0.88)가 발생 — `mmse_equalize()` 선행 호출로 수정 후 재검증, floor 사라지고 단조 감소 확인.
- **검증**: AWGN 경로 회귀 없음(기존과 동일). FLAT_FADING은 ZF/MMSE 결과가 거의 동일(단일탭 채널에서 이론적으로 당연). TDL은 MMSE가 ZF보다 전 SNR에서 확실히 우세(PDCCH -6dB에서 P_detect 0.22 vs 0.014). 어느 조합에서도 floor 없이 SNR 증가에 따라 단조 개선 확인.

### 원격 개발 환경 — Mac에서 RTX 4060 WSL2로 Tailscale SSH 연결 (2026-08-03)
Mac을 가벼운 접속 단말로 사용하고 Codex/Claude Code, Git, Sionna 및 GPU 작업은 Windows 4060 PC의 WSL2에서 실행하기 위한 원격 개발 경로를 구축했다. 공유기 포트포워딩이나 공인 인터넷 SSH 노출 없이 Tailscale 사설 Tailnet만 사용한다.

- **서버 환경 확인**: Windows WSL `2.5.9.0`, kernel `6.6.87.2-1`, Ubuntu `24.04.2 LTS`, `systemd` 활성화, WSL2 배포판 정상 실행을 확인했다. WSL 사용자/호스트는 `inseok@KANG`이며 RTX 4060(약 8 GB VRAM)의 WSL CUDA 접근을 확인했다.
- **OpenSSH 서버**: WSL Ubuntu에 `openssh-server`를 설치하고 `ssh.service`를 활성화했다. 사용자 요구에 따라 기본 포트 22 대신 `22299`를 사용하도록 `/etc/ssh/sshd_config.d/99-custom-port.conf`에 설정했다. Ubuntu 24.04의 `ssh.socket`이 22번 포트를 다시 열지 않도록 socket activation을 비활성화하고 일반 `ssh.service` 방식으로 운용한다.
- **로컬 검증**: Windows PowerShell에서 `ssh -p 22299 inseok@localhost` 접속에 성공했다. SSH 로그인 셸에서 `nvidia-smi`를 찾지 못한 문제는 WSL 제공 바이너리 `/usr/lib/wsl/lib/nvidia-smi`가 SSH 세션의 `PATH`에 없던 것이 원인이었으며, `/usr/lib/wsl/lib`를 사용자 `PATH`에 추가해 해결했다. WSL에서는 Ubuntu용 `nvidia-utils-*`를 별도 설치하지 않고 Windows NVIDIA 드라이버가 제공하는 WSL 도구를 사용한다.
- **Tailscale 구성**: Windows 호스트가 아니라 WSL2에 Tailscale을 직접 설치하고 노드 이름을 `sionna-4060-wsl`로 등록했다. Mac에도 macOS용 Tailscale 앱을 설치해 동일 계정/Tailnet으로 연결했다. 확인된 주소는 Mac `100.109.115.82`, WSL `100.76.237.65`이며, 해당 주소는 공개 IP가 아닌 Tailnet 내부 주소다.
- **Mac SSH 인증/별칭**: Mac의 기존 ED25519 키(`~/.ssh/id_ed25519`) 공개키를 WSL에 등록해 비밀번호 없는 로그인을 구성했다. Mac `~/.ssh/config`에는 `Host KANG_HOME`, `HostName 100.76.237.65`, `User inseok`, `Port 22299`, `IdentityFile ~/.ssh/id_ed25519`, `IdentitiesOnly yes` 및 keepalive 설정을 추가했다.
- **최종 검증**: Mac에서 `ssh KANG_HOME` 명령으로 WSL2에 정상 접속했다. 최종 경로는 `Mac terminal → Tailscale → WSL2 OpenSSH:22299 → RTX 4060/Sionna 개발 환경`이다.

운용 시에는 Mac과 WSL에서 Tailscale이 연결된 상태인지 확인한 뒤 `ssh KANG_HOME`만 실행하면 된다. 장시간 시뮬레이션은 추후 `tmux` 세션으로 분리하면 Mac 연결이 끊어져도 작업을 유지할 수 있다.

### PHY LLS — CL_4PORT 교차편파(XPD) 누설 상관 + UL CLPC 시변 PL 추가, 2건의 기존 결함 발견 (2026-08-27)

**XPD 누설 상관**: `mimo_apply_tx_correlation_4x4(h, rho, rho_xpol)` — 2026-08-02에 남겼던 후속 과제. Rtx = R_pol ⊗ R_ant(Kronecker product)로 확장, 포트 순서 [pol1_ant0, pol1_ant1, pol2_ant0, pol2_ant1](pol=외곽 인덱스, ant=내곽 인덱스)에서 R_ant(rho)는 기존 동일편파 쌍(포트 0-1, 2-3), 신규 R_pol(rho_xpol)은 동일 안테나 위치의 편파 간(포트 0-2, 1-3)에 적용. `sqrt(A⊗B)=sqrt(A)⊗sqrt(B)`와 `(A⊗I)(I⊗B)=A⊗B` 항등식으로, 기존 rho 믹싱 후 같은 닫힌형 2×2 블록 제곱근을 rho_xpol로 순차 적용하면 정확히 Kronecker 모델이 됨(고유분해 불필요, 손으로 유도해 검증). `SPATIAL_CORR_XPOL` config로 제어, 0=기존과 완전히 동일(회귀 48/48 유지 확인).

**UL CLPC 시변 PL**: `run_ulpc_simulation()` Part 1을 Gauss-Markov(AR1) 정상상태 프로세스로 확장 — `PL(sf) = PL_mean + corr·(PL(sf-1)-PL_mean) + sqrt(1-corr²)·std·randn()`(Gudmundson 1991 그림자페이딩 자기상관 모델의 구현 정의 근사). `UL_PC_PL_VAR_STD_DB`/`UL_PC_PL_VAR_CORR` config 신규, std=0이면 기존 고정 PL과 완전히 동일. 실측(P0=-95dBm, α=0.8, PL 평균100±4dB, corr=0.85): OL SINR은 (1-α)=20%만큼만 PL 변동을 따라가 -2.7~-4.1dB에서 계속 흔들리는 반면, f(i) 수렴 후 CL SINR은 9.3~10.3dB로 목표 10dB 근방에 훨씬 타이트하게 유지됨 — CL이 시변 PL에서도 (1-α)·PL 잔여를 실제로 추종한다는 설계 의도가 그대로 확인됨.

**발견 1 (수정 완료) — `codebook.c` RI/PMI 선택기의 catastrophic cancellation**: XPD 누설 상관을 검증하려고 손으로 완전 rank-1 채널(4개 열이 모두 동일 벡터)을 만들어 `codebook_type1_sp_4port_ri_pmi_select()`의 2×2 Gramian 중간값을 디버그 하네스로 직접 찍어보니, `A = H_eff^H H_eff + N0·I`는 N0>0인 한 항상 `det ≥ N0²`로 엄밀히 양수여야 하는데, `det = A00·A11 − A01·A10`을 그대로 계산하는 기존 코드가 두 큰 값의 차로 유효자릿수를 잃어(H_eff 두 열이 거의 평행해지는 고상관 영역) 부동소수점 오차로 음수가 나올 수 있었음 — 이러면 `1/det`의 부호가 뒤집혀 `a0`/`a1`이 1 근처까지 치솟고 해당 rank-2 후보의 log2 항이 터무니없이 커짐. `det`를 실수부로만 계산하고 `N0²`의 상대 하한(`1e-6·N0²`) 미만이면 해당 후보를 스킵하도록 수정(코드 주석에 유도 과정 기록). 회귀 48/48 유지.

**발견 2 (미수정, 사용자 확인 대기) — rank-1/rank-2 코드북 전력 정규화 불일치**: 위 수정을 반영한 뒤에도 rho=rho_xpol→1(채널이 수학적으로 완전 rank-1)인 극단 케이스에서 rank-1 선택률이 여전히 0%로 유지됨. `codebook_type1_sp_4port_rank1()`은 ‖W‖²=1(코드 내 정규화 증명 주석 존재)인데, `codebook_type1_sp_4port_rank2()`도 동일한 `norm=0.5`를 컬럼마다 독립 적용해 각 레이어가 개별적으로 ‖W2[:,l]‖²=1 — 즉 rank-2의 총 송신전력(두 레이어 합)이 rank-1의 정확히 2배(+3dB)가 되어 RI 선택기의 용량 비교가 애초에 공정하지 않음(3GPP 관례상 rank-2는 레이어당 전력을 P/2로 나눠 총 송신전력이 rank와 무관하게 동일해야 함). 이게 2026-08-02에 기록한 "동일편파 상관만으로는 rank-1이 거의 선택 안 됨" 결론과 이번 XPD 극단값 결과 모두의 실제 근본 원인일 가능성이 높음 — 다만 CL_4PORT를 쓰는 기존 시뮬레이션 전체(BLER 실측치 포함)에 영향을 주는 근본적인 변경이라 이번 라운드에서는 고치지 않고 `tasks/todo.md`에 사용자 확인 대기 항목으로 남김.

---

## 🔄 업데이트 이력 (원본 CLAUDE.md 기준)

| 날짜 | 내용 |
|------|------|
| 2026-05-24 | 최초 작성 — 기본 프로필 및 PHY 컨텍스트 |
| 2026-05-24 | 직급(수석연구원), 3GPP Rel-17/18/19, Massive MIMO Eigen BF 완료, Aerial/NTN 추가 |
| 2026-05-24 | 사이드 프로젝트 PHY LLS 개발 환경 추가(WSL + Claude Code + Git) |
| 2026-07-13 | PHY LLS: MIMO(2x2)/HARQ(IR·Chase)/TDL/PUSCH(DFT-s-OFDM)/PUCCH(F0-3) 추가, Polar shortening 버그 수정 |
| 2026-07-13 | PHY LLS: TDL+MIMO 조합(SIMO_MRC/SM_2X2) 추가, main.c PDSCH dispatch 배선 수정 |
| 2026-07-13 | PHY LLS: TDL+MIMO(SM_2X2)+HARQ 전체 결합 추가 |
| 2026-07-14 | PHY LLS: TDL+MIMO(SIMO_MRC)+HARQ 추가 — PDSCH TDL×MIMO×HARQ 조합 완료 |
| 2026-07-14 | PHY LLS: PUCCH F1/F3에 TDL(genie-aided) 추가, main.c PUSCH/PUCCH 배선 수정 |
| 2026-07-14 | PHY LLS: PRACH 추가 — ZC 프리앰블 검출 + TA 추정 |
| 2026-07-14 | PHY LLS: PUSCH+TDL DFE(비선형 등화) 탐색 — 저SNR 손해/중SNR 소폭 이득 실측 |
| 2026-07-14 | PHY LLS: PUSCH+TDL Turbo 등화 추가 — no-DFE/hard-DFE 모두 능가 실측, ldpc_decode_soft/qam_soft_symbol 신규 |
| 2026-07-14 | PHY LLS: PUCCH F1/F3+TDL을 HARQ와 결합 — UCI에 CRC 없어 genie bit-match로 종료 판정, F3는 rate_matching.c 범용 circular buffer를 N=64 Polar mother code에 재사용 |
| 2026-07-14 | PHY LLS: PRACH를 RE grid + TDL 다경로로 정교화 — 순환시프트=DFT shift 정리로 연속시간 지연과 등가임을 이용, 검출 알고리즘 무변경으로 tdl.c만 추가 |
| 2026-07-15 | PHY LLS: 전체 소스/헤더 파일(52개) 박스형 파일 헤더 배너 추가 |
| 2026-07-15 | git: origin/develop 재구조화 히스토리 발견 → archive 브랜치로 보존, 로컬 기준 force-push로 develop 정리 |
| 2026-07-21 | PHY LLS: 4×4 SU-MIMO 공간 다중화(SM_4X4) 추가 — Gauss-Jordan 4×4 역행렬, ZF/MMSE 검출, FDM 파일럿 레이어별 분배 |
| 2026-07-21 | PHY LLS: Type I SP 4포트 코드북 생성기 추가 — TS 38.214 Sec 5.2.2.2.1, rank-1(32) + rank-2 변형A/B(48), 전 80 코드워드 검증 |
| 2026-07-22 | PHY LLS: RI+PMI 동시 적응 선택(CL_4PORT) 추가 — 추정 Shannon 용량 기준 80후보 전수탐색, mrc_combine_4rx / mimo_mmse_detect_4rx2 신규, adaptive/R1-fixed/R2-fixed BLER 3열 비교 출력 |
| 2026-07-22 | PHY LLS: UL Closed-Loop Power Control(ULPC) 추가 — TS 38.213 §7.2.1, TPC {-1,0,+1,+3}dB genie-aided, 시계열 수렴 + PL 스윕 + α=0.8 설계 함의 출력, P_CMAX=23dBm (NR Power Class 3) |
| 2026-08-02 | `CLAUDE.md` 분리 작업(이 문서 생성) — 상시 컨텍스트/이력/작업/교훈으로 재구성. 원본: `CLAUDE.md.original-20260802.bak` |
| 2026-08-03 | 원격 개발 환경 구축 — Mac에서 Tailscale과 SSH 별칭 `KANG_HOME`을 통해 Windows RTX 4060 PC의 WSL2(`22299`)에 키 인증으로 접속 |
