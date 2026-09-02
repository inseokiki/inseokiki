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

**발견 2 (수정 완료, 사용자 확인 후 진행) — rank-1/rank-2 코드북 전력 정규화 불일치**: 위 수정을 반영한 뒤에도 rho=rho_xpol→1(채널이 수학적으로 완전 rank-1)인 극단 케이스에서 rank-1 선택률이 여전히 0%로 유지됨. `codebook_type1_sp_4port_rank1()`은 ‖W‖²=1(코드 내 정규화 증명 주석 존재)인데, `codebook_type1_sp_4port_rank2()`도 동일한 `norm=0.5`를 컬럼마다 독립 적용해 각 레이어가 개별적으로 ‖W2[:,l]‖²=1 — 즉 rank-2의 총 송신전력(두 레이어 합)이 rank-1의 정확히 2배(+3dB)가 되어 RI 선택기의 용량 비교가 애초에 공정하지 않았음(3GPP 관례상 rank-2는 레이어당 전력을 P/2로 나눠 총 송신전력이 rank와 무관하게 동일해야 함). `norm`을 `0.5`→`0.5/√2`로 수정해 레이어당 전력 0.5·합계 1(=rank-1과 동일)로 맞춤.

**재검증 결과(수정 후, 손으로 만든 극단값 하네스 + 무작위 iid 채널 3개 SNR 지점)**: iid Rayleigh 4×4에서 이제 R1선택률이 SNR에 따라 정상적으로 갈림 — SNR=-10dB(저SNR): ≈90~93%, SNR=0dB: ≈10~20%, SNR=+10dB(고SNR): ≈0.01~1% (레이어 늘릴수록 고SNR에서 유리해지는 MIMO 용량 이론과 정확히 일치). 상관을 걸면 R1선택률이 단조 증가하고, rho=rho_xpol→1(채널이 수학적으로 완전 rank-1)에서는 **전 SNR에서 R1선택률=100%**로 정확히 수렴 — 물리적으로 기대한 그대로. 즉 2026-08-02에 기록한 "동일편파 상관만으로는 rank-1이 거의 선택 안 됨" 결론과 이번 XPD 극단값에서 관측한 0%는 둘 다 물리 현상이 아니라 이 전력 정규화 버그가 만든 인공물이었음이 확정됨. 회귀 48/48 유지. `CL_4PORT`를 쓴 기존 BLER 실측치(2026-07-22, 2026-08-02, 2026-08-03 항목의 adaptive/R1-fixed/R2-fixed 비교 등)는 이 수정 전 RI 선택 편향이 반영된 결과이므로 참고 시 유의.

### Type I SP 8-port 코드북 rank-1 구현 (2026-08-30)

2026-08-27에 착수했다가 3GPP 스펙 문서(`38214-he0.docx`)의 OLE 수식 추출 불가로 보류됐던
CSI 8-port 확장을 재개. 세부는 `tasks/todo.md` "진행 중" 항목 참조, 여기엔 이번에 새로
확인한 문서 파싱 방법과 결과만 기록.

**문서 파싱 방법 개선**: 기존엔 "OLE 수식이라 추출 안 됨"으로 문서 전체를 포기했는데,
실제로는 표(`w:tbl`)의 일반 텍스트 셀과 수식이 들어간 셀이 섞여 있어 필요한 값에 따라
추출 가능 여부가 다름을 확인. 단순 정규식으로 raw XML을 훑으면 태그가 텍스트를 쪼개서
"Table 5.2.2.2.1-2" 같은 캡션조차 못 찾을 수 있음(실제로 처음엔 실패) — `xml.etree`로
`w:tbl`/`w:tr`/`w:tc`를 구조적으로 순회하며 각 셀의 `w:t` 텍스트만 모으는 방식으로 바꾸자
표 구조와 순수 텍스트 셀은 정확히 읽힘. 반면 셀 내용 자체가 OLE 개체(수식)인 경우는
이 방법으로도 빈 문자열만 나옴 — "표 구조는 파싱 가능, 표 안의 수식만 여전히 막힘"이
정확한 결론.

**확인된 것**: Table 5.2.2.2.1-2(P_CSI-RS별 지원 (N1,N2)/(O1,O2) 조합)를 이 방법으로 직접
읽어, P=8에 (N1,N2)=(4,1),(O1,O2)=(4,1) 옵션이 실제로 존재함을 원문으로 확정(대안:
(N1,N2)=(2,2),(O1,O2)=(4,4) — 2D 배열이라 이 프로젝트엔 미적용). `tasks/todo.md`의 기존
가정과 일치.

**구현(rank-1만)**: `PHY/src/codebook_8port.c`/`include/codebook_8port.h` 신규. 빔 벡터
`v_l[k]=e^{j2πlk/(N1·O1)}`와 코피에이징 `φ_n=e^{jπn/2}`는 정의 자체가 N1에 의존하지
않아, 기존 4-port(N1=2) 코드의 구조를 N1=4로 그대로 대입 — 별도 스펙 확인 없이도
안전한 일반화(수학적으로 자명). 정규화 `W=(1/√8)[v_l; φ_n·v_l]`, 64개 코드워드
(16빔×4코피에이징) 전부 `||W||²=1` 확인. half-grid 직교성(l vs l+8, 16포인트 그리드의
절반)도 4-port의 l vs l+4(8포인트 그리드의 절반)와 동일한 성질로 유지됨을 별도 검증
프로그램(`/tmp`, 미커밋)으로 확인. `c_Makefile`의 `COMMON_SRCS`에 추가, 전체 빌드
경고 없음, 회귀 테스트 48/48 유지 확인. 아직 `main.c`/`pdsch.c`에 시뮬레이션 모드로
배선하지 않음(코드북 함수만 존재, 실제 PDSCH 체인 미연결).

**여전히 막힌 것(rank-2 이상)**: Table 5.2.2.2.1-3(i1,3→k1,k2 매핑)의 k1 값 자체가
Equation Editor 3.0 계열 `.wmf` OLE 이미지로만 존재해 위 구조적 파싱으로도 텍스트를
못 얻음. 4-port 코드는 k1=N1·O1/2(전체 그리드의 절반, "반대편 빔")를 쓰는데, 이게
N1에 실제로 비례하는 값인지 아니면 표준이 N1 무관하게 k1=O1(=4)을 쓰는지는 이
문서만으로 구분 불가 — 둘 다 N1=2인 4-port 케이스에서는 동일한 값(4)이 나와서
기존 코드만 봐서는 구별이 안 됐던 것. 이 값을 확정하기 전에는 rank-2를 구현하지 않기로
함(3GPP 부정합 리스크). `.wmf` 뷰어/변환기(pandoc, LibreOffice 등 여전히 미설치)나
다른 스펙 소스 확보가 필요.

### Type I SP 8-port 코드북 rank-2 구현 (2026-08-31)

3gpp-server MCP 연결 후 재개. 로컬 `38214-he0.docx`의 OLE(.wmf) 수식은 여전히 텍스트
추출이 안 됐지만, MCP 서버가 제공하는 TS 38.214 v18.10.0(Rel-18) 원문은 수식이 **뷰어블
PNG**(`image://...`)로 렌더링되어 있어, `get_section`으로 절 5.2.2.2.1 전체를 받고
`get_image`로 Table 5.2.2.2.1-3의 각 셀 이미지(image208~image221)를 직접 열람해
막혀있던 값을 확정함.

**확정된 값**: Table 5.2.2.2.1-3(i1,3 → k1,k2 매핑, 2-layer)을 4개 (N1,N2) 그룹별로
재구성한 결과:

| i1,3 | N1>N2>1 | N1=N2 | N1=2,N2=1 | **N1>2,N2=1 (8-port)** |
|---|---|---|---|---|
| 0 | (0,0) | (0,0) | (0,0) | (0,0) |
| 1 | (O1,0) | (O1,0) | (O1,0) | (O1,0) |
| 2 | (0,O2) | (0,O2) | N/A | **(2·O1,0)** |
| 3 | (2·O1,0) | (O1,O2) | N/A | **(3·O1,0)** |

즉 우리 케이스(N1=4,N2=1,O1=4)는 **k1 = i1,3 · O1**(i1,3=0,1,2,3 → k1=0,4,8,12), **k2=0**.
`N1·O1/2` 가설은 기각됨 — 4-port(N1=2)는 그룹이 "N1=2,N2=1"이라 i1,3∈{0,1}만 유효했고
그 두 값에서 우연히 `N1·O1/2`(=4)와 `i1,3·O1`(i1,3=1일 때 O1=4)이 같은 값을 줘서
기존 코드만으로는 두 가설이 구분되지 않았던 것.

같은 절에서 Table 5.2.2.2.1-6(codebookMode=1, 2-layer 코드북 원식)도 함께 확인:
`W = 1/√(2·P_CSI-RS) · [v_l, v_l'; φ_n·v_l, −φ_n·v_l']`, `l'=l+k1`, `n=i2`. 위 k1 값과
이 원식을 그대로 `codebook_type1_sp_8port_rank2()`(`PHY/src/codebook_8port.c`)에 구현.
정규화(열당 0.5, 총 1 = rank-1과 동일 총 전력)와 직교성은 수식으로 증명 가능한 형태라
디버그 하네스로 256개 코드워드(16빔×4×4) 전부 `||W[:,l]||²=0.5`(오차 0) ·
`|W0^H W1|`(오차 ~1e-17, 부동소수점 잡음 수준)를 확인. `codebook_8port.h`에 rank-2
함수 선언·`CB8_I13_COUNT`/`CB8_RANK2_TOTAL` 추가. 전체 clean 재빌드 경고 없음, 회귀
48/48 유지. 아직 `main.c`/`pdsch.c`에 배선 안 됨(rank-1과 동일하게 다음 세션 결정 사항).

**부수 발견 (수정 안 함, 범위 밖)**: 위 검증 과정에서 기존 `codebook.c`(4-port)의
rank-2 "변형 A"(빔쌍, 코드상 `i1_3==0`)가 스펙 원식과 실제로 다른 구조임을 확인.
스펙은 두 열이 **같은 n**을 쓰고 2번째 열에만 부호를 반전(`φ_n·v_l, −φ_n·v_l'`)하는데,
기존 코드는 `φ_{n1}=1 고정, φ_{n2}=i2에 따라 4개 값 가변`이라는 스펙에 없는 별도 스킴을
사용 중(둘 다 내부적으로는 직교·정규화가 성립하는 유효한 프리코더지만, 실제 3GPP가
정의한 코드워드 집합과는 다름). 추가로 `i1_3` 라벨 자체도 스펙과 반대로 붙어있음
(스펙: i1,3=0→같은빔(k1=0), i1,3=1→l+O1 / 기존 코드: 반대). RI/PMI 선택이 전수탐색이라
BLER 실측치에는 영향이 없을 가능성이 높으나(선택되는 코드워드 집합의 물리적 성질은
동일), 실제 3GPP 코드워드 비트 매핑과는 다름 — 4-port 수정 여부는 사용자 확인 후 별도
진행.

### 4-port rank-2 코드북, TS 38.214 원식으로 재작성 (2026-08-31)

위 부수 발견을 사용자 확인 후 수정. `codebook_type1_sp_4port_rank2()`를 8-port와 동일한
단일 수식으로 통일: `W = 1/√(2P)·[v_l, v_l'; φ_n·v_l, −φ_n·v_l']`, `l'=(l+k1) mod 8`,
`k1=i1_3·O1`(i1_3∈{0,1} → k1=0,4). "변형 A/B"로 분기하던 로직과 자체 코피에이징 스킴을
제거. `codebook_type1_sp_4port_print()`와 `codebook_type1_sp_4port_ri_pmi_select()`의
탐색 범위도 함께 정정 — 기존엔 i1_3=0(빔쌍) 케이스만 i1_1을 절반(0~3)으로 잘라 48개
(rank-2)/80개(전체) 후보를 탐색했으나, 스펙엔 그런 축소가 없어(i1_3=0/1 모두 i1_1
0~7 전 범위 유효) 64개(rank-2)/96개(전체)로 정정. `codebook.h` 문서 주석도 함께 갱신.

**검증**: 64개 코드워드 전부 열당 전력=0.5(오차~1e-16, 부동소수점 잡음), 직교성
오차~1e-17 확인(별도 하네스, i1_3=0에서 col0=col1(같은 빔) 정성 확인 포함). clean
재빌드 경고 없음, 회귀 48/48 유지.

**결과 영향 판단**: RI/PMI 선택기가 두 i1_3 값 전체를 전수탐색하므로, 선택 가능한
코드워드 **집합**의 물리적 성질(직교 빔쌍 다이버시티 + 동일빔 교차편파 다이버시티) 자체는
수정 전후 동일 — 이전 BLER 실측치가 이 라벨/스킴 불일치로 왜곡됐을 가능성은 낮다고 판단
(2026-08-27의 전력 정규화 버그처럼 RI 선택 편향을 만드는 종류의 결함은 아니었음). 다만
실제 3GPP UE/gNB가 보고·해석하는 PMI 비트와는 이전 코드가 달랐던 것은 사실이며, 이번
수정으로 정합됨.

### 8-port 코드북, PDSCH 시뮬레이션 배선 (2026-08-31)

`codebook_8port.c`의 rank-1/2 프리코더를 `main.c`/`pdsch.c` 시뮬레이션 체인에 연결.
`run_pdsch_cl_4port_simulation()`(평탄 페이딩, genie-aided CSI)과 동일한 구조를 8 Tx
포트로 확장한 `run_pdsch_cl_8port_simulation()`을 신규 작성, `main.c`에
`MIMO_MODE=CL_8PORT` 라우팅 추가.

**신규 추가**:
- `codebook_type1_sp_8port_ri_pmi_select()`(`codebook_8port.c`) — 4-port
  `codebook_type1_sp_4port_ri_pmi_select()`와 동일한 두 기준(rank-1: 후-빔포밍 파워,
  rank-2: MMSE 후-검출 SINR 합)을 8-port 인덱스 범위(16빔, i1_3 0~3)로 확장. rank-1 64 +
  rank-2 256 = 320 후보 전수탐색.
- `mimo_channel_draw_4x8()`(`mimo.c`) — i.i.d. Rayleigh, H[4][8].

**재사용(수정 없이 그대로 사용 가능함을 확인)**: `mrc_combine_4rx()`/
`mimo_mmse_detect_4rx2()`는 프리코딩 이후의 유효 채널 H_eff=H·W가 항상 4(Rx)×rank
차원이라 Tx 포트 수(4 또는 8)와 무관 — 8-port에도 그대로 재사용.

**미구현으로 남긴 것**: Tx 공간상관(Kronecker). 4-port `mimo_apply_tx_correlation_4x4()`는
N1=2 전용의 단순 인접쌍 2×2 회전 트릭(포트 (0,1)과 (2,3) 사이)이라 N1=4로 그대로
확장할 수 없음 — 4개 안테나 간의 상관을 표현하려면 N1×N1 상관 행렬을 별도로 유도해야
하고, 이전에 CL_4PORT의 XPD 상관 확장 때도 순진한 일반화가 물리적으로 부정확했던 전례가
있어(2026-08-27 항목 참조) 이번엔 검증 없이 임의로 확장하지 않고 i.i.d.로만 남김. TDL/HARQ
변형도 미착수(4-port 평탄 버전에 대응하는 범위로 이번 세션 완료). `tasks/todo.md`에 후속
과제로 등록.

**검증**: 회귀 48/48 유지, clean 빌드 경고 없음. 수동 SNR 스윕(-10~10dB, MCS10 16QAM,
200trial/포인트)에서:
- BLER이 SNR 증가에 따라 세 시나리오(Adaptive/R1-fixed/R2-fixed) 모두 단조 감소
- R1선택률이 저SNR(-10dB)≈82.5% → 고SNR(0dB 이상)≈0%로 정상적으로 갈림(MIMO 용량
  이론과 일치 — 4-port에서 이미 검증된 것과 동일한 정성적 패턴)
- 동일 MCS/SNR 조건으로 CL_4PORT와 나란히 비교: R2fix BLER이 R1fix보다 나쁜 현상도
  양쪽에 동일하게 나타남(이 시뮬레이터가 rank와 무관하게 MCS를 고정하는 기존 관례 때문 —
  4-port에서 이미 알려진 특성이지 8-port 신규 결함이 아님을 확인). 8-port rank-2가 4-port
  rank-2보다 고SNR(10dB)에서 더 낮은 BLER(0.325 vs 0.57)을 보인 것도 8개 Tx 안테나가
  주는 추가 빔포밍 이득으로 물리적으로 타당.

### 8-port 코드북, TDL/HARQ 변형 추가 (2026-08-31)

CL_8PORT 평탄 페이딩 배선에 이어, 4-port의 `run_pdsch_cl_4port_tdl_simulation()`과
`run_pdsch_cl_4port_harq_simulation()` 구조를 그대로 8 Tx 포트로 확장한
`run_pdsch_cl_8port_tdl_simulation()`/`run_pdsch_cl_8port_harq_simulation()`을
`pdsch.c`에 신규 작성, `main.c`의 `MIMO_MODE=CL_8PORT` 라우팅에 TDL/HARQ 분기 추가.

**TDL 변형**: 4-port와 동일하게 Wideband PMI(모든 data RE의 H[k]를 평균한 H_avg 기준
RI+PMI 선택) 방식. `tdl_draw()`/`tdl_freq_response()`가 Tx-Rx 페어별로 독립 호출되는
구조라 `taps[4][4]`→`taps[4][8]`, `H_cache`도 4Rx×8Tx로 크기만 바꾸면 그대로 재사용
가능함을 확인 — tdl.c/tdl.h 수정 불필요.

**HARQ 변형**: 4-port와 동일하게 FLAT_FADING/TDL 모두 지원, 시도 0의 H_avg(또는
H_flat0) 기준으로 RI+PMI를 한 번 선택해 이후 재전송까지 고정, 채널 자체는 시도마다
재추첨(시간 다이버시티), CW 수는 rank_fix에 따라 1개/2개.

**두 변형 모두 공통**: Tx 공간상관은 평탄 페이딩 버전과 동일하게 미지원(i.i.d.만) —
N1=4 Kronecker 상관 모델 유도가 아직 없다는 동일한 제약이 그대로 이어짐.

**검증**: clean 재빌드 경고 없음(3-port 무관 기존 경고 3건만 유지), 회귀 48/48 유지.
수동 검증:
- TDL(MCS10, DS=300ns, -5~15dB 스윕): BLER_Adapt가 1.0(-5dB)→0.215(15dB)로 단조
  감소, R1선택률 86.5%→0%로 SNR에 따라 정상적으로 갈림 — 평탄 페이딩 CL_8PORT와
  동일한 정성적 패턴.
- HARQ(MCS15, FLAT/TDL 둘 다, -5~10dB): BLER(1st)는 높게 유지되다가 HARQ 재전송으로
  BLER(HARQ, 최종)가 크게 개선(예: TDL 10dB에서 BLER(1st)=1.0 → BLER(HARQ)=0.345),
  AvgTx가 고SNR에서 4.0에서 하락(조기 성공 종료) — HARQ가 의도대로 동작함을 확인.

`tasks/todo.md`의 "CL_8PORT TDL/HARQ 변형" 항목 완료 처리. 남은 후속 과제는 Tx 공간상관
(Kronecker N1=4) 모델 유도 하나만 남음.

### CL_8PORT Tx 공간상관(Kronecker N1=4) 모델 추가 (2026-08-31)

CSI 8-port 코드북 작업의 마지막 후속 과제. `mimo_apply_tx_correlation_4x4()`(4-port,
N1=2)를 그대로 8-port(N1=4)로 일반화한 `mimo_apply_tx_correlation_4x8()`을 `mimo.c`/
`mimo.h`에 신규 작성.

**핵심 관찰**: 기존 N1=2 구현의 R_ant 블록 `[[1,rho],[rho,1]]`(대칭 제곱근 `a*I+b*J`,
`a,b`는 `sqrt(1±rho)`의 평균/반차)을 다시 보니, 이는 사실 2-원소 ULA에 대한 **지수상관
모델**(`R[i][j]=rho^|i-j|`, Loyka 2001; `CLAUDE.md` 참고서적 목록의 Björnson et al.,
*Massive MIMO Networks*에도 나오는 표준 단순화 모델) 그 자체와 동일한 특수 케이스였음 —
즉 이미 확립된 규칙을 N1=4로 그대로 확장하는 문제로 재정의됨(완전히 새로운 모델을
고안할 필요 없음).

**구현**: 4x4 지수상관 행렬 `R_ant[i][j]=rho^|i-j|`의 Cholesky 인수 `L`(`R_ant=L·L^T`,
`chol_exp_corr4()`)을 계산해 각 편파 그룹(ports 0-3, 4-7) 내부에 적용. 대칭 제곱근이
아니라 하삼각 Cholesky 인수를 쓴 이유: 입력이 i.i.d. 복소 가우시안이라 회전 불변이므로
`L·L^T=R_ant`만 만족하면 어떤 인수분해든 2차 통계량(분산·상관)이 동일 — N1=2에서 기존
대칭 제곱근과 Cholesky 인수가 통계적으로 동등함을 직접 검산(둘 다 `Var=1`, `Corr=rho`
재현), 4x4 고유분해를 피할 수 있어 구현이 단순함. XPD 누설 상관(`rho_xpol`)은
`tasks/todo.md`에 예상해둔 대로 편파 그룹 간(같은 n1, 다른 g) 관계라 N1과 무관 —
기존 2x2 블록 로직을 n1=0..3 각각에 반복 적용하는 것만으로 그대로 재사용.

`codebook_type1_sp_8port_*` 세 시뮬레이션 함수(평탄/TDL/HARQ) 전부에 배선: 평탄은
채널 드로우 직후, TDL은 RE별 `H_cache[d]`에 개별 적용 후 `H_avg` 누적(4-port TDL과
동일 순서), HARQ는 FLAT/TDL 두 경로 모두(시도 0 선택 시점 + 재전송 재추첨 시점) 적용.
`config_parser.c`의 요약 출력 조건도 `CL_4PORT`→`CL_4PORT || CL_8PORT`로 확장.

**검증**: clean 재빌드 경고 없음, 회귀 48/48 유지. 표면 지표만으로 판단하지 않고
(`tasks/lessons.md` 2026-08-27 교훈 적용) 중간값을 직접 검산하는 standalone 하네스로:
- 200,000회 Monte Carlo로 rho=0.7일 때 편파 그룹 내 4개 포트 간 실측 공분산이 이론
  `rho^|i-j|`와 오차 <0.003으로 일치, 포트별 분산이 1.0 근방(0.997~1.002)으로 정규화
  유지됨을 확인.
- rho_xpol=0.6일 때 같은 n1·다른 편파 그룹 간 실측 상관(port0-port4)=0.5976(이론
  0.60), 다른 n1·같은 그룹 간 상관(port0-port1)=0.0006(이론 0)으로 XPD 로직이 N1과
  무관하게 정확히 재사용됨을 확인.
- 시뮬레이션 레벨에서 rho=rho_xpol=0(i.i.d.) → 0.999(거의 완전한 rank-1 채널)로
  올렸을 때 R1선택률이 0.4%→100%로 물리적으로 타당하게 수렴(2026-08-27 4-port XPD
  검증 때와 동일한 패턴) — 새 8-port 상관 모델이 RI 선택기와 정합적으로 상호작용함을
  확인.

CSI 8-port 코드북 확장 과제(2026-08-27 착수) 전체 완료 — rank-1/2 코드북, 4-port
정합화, 시뮬레이션 배선(평탄/TDL/HARQ), Tx 공간상관까지 모두 마무리.

### Massive MIMO — 32-port(N1=4,N2=4) Type I SP 코드북, rank 1~4 (2026-08-31)

사용자가 "Massive MIMO(64안테나)" 구현을 요청. 진행 전 스코프 확인 질문(구현 방식:
Eigen-BF/코드북 확장/단순 ZF·MRT — 코드북 확장 선택; RI 범위: 1~2/1~4/1~8 — 1~4 선택)
후, 3gpp-server MCP로 TS 38.214 §5.2.2.2.1의 2D 배열(N1>1, N2>1) 코드북 스펙을 조사.

**중요한 사전 확인 사항**: TS 38.214 Rel-18(v18.10.0) Type I Single-Panel 코드북은
**64 CSI-RS 포트를 정의하지 않는다** — §5.2.2.2.1 서두가 지원 포트 수를 4/8/12/16/24/
**32**로만 명시(`{3000..3031}`까지), TS 38.211 v18.9.0 Table 7.4.1.5.3-1(CSI-RS 슬롯
내 위치)도 최대 X=32까지만 정의되어 있어 애초에 64-포트 CSI-RS 리소스 자체를 구성할
방법이 Rel-18에 없음(Rel-19는 MCP 서버 아카이브 제약으로 미확인, 다만 Type I SP가
거기서 확장됐을 가능성은 낮다고 판단). 이 사실을 사용자에게 먼저 보고하고, "64안테나
소자 = 32 CSI-RS 포트 × 2편파"로 스코프를 재정의(사용자 확인 후 진행) — Table
5.2.2.2.1-2에서 32포트 중 진짜 2D 배열(N1>1,N2>1)인 조합은 (N1,N2)=(4,4)와 (8,2)
둘뿐이며, 정사각 배열인 **(N1,N2)=(4,4), (O1,O2)=(4,4)**를 채택(다른 32포트 옵션
(16,1)은 N2=1이라 기존 8-port와 동일한 1D 구조).

**RI 범위**: rank 1~4까지 구현(사용자 확인). rank 3/4는 P≥16 분기(Table
5.2.2.2.1-7/-8)라 rank 1/2(Table 5.2.2.2.1-5/-6)와 코드북 구조 자체가 다름 — k1/k2
빔 오프셋 대신 θ_p=e^{jπp/4} co-phasing을 쓰고, 절반 길이 빔벡터 ṽ_{l,m}(l의 DFT
간격이 2배, i1_1 범위도 0~15→0~7로 절반)을 사용.

**구현**:
- `codebook_32port.c`/`.h` 신규. 빔 벡터: `u_m`(수직 N2 방향 DFT 성분)이 추가된
  2D 빔벡터 `v_{l,m}`(길이 N1·N2=16), rank3/4 전용 `ṽ_{l,m}`(길이 8). rank1~4
  프리코더 4개, 표에서 확인한 원식 그대로 구현(rank2의 (k1,k2)는 Table
  5.2.2.2.1-3 "N1=N2" 열: i1,3=0→(0,0),1→(O1,0),2→(0,O2),3→(O1,O2)).
- RI+PMI 선택기: rank 1~4 전 범위를 하나의 일반 N×N(N≤4) Gramian 역행렬 기반
  MMSE 후-검출 SINR 합 공식으로 통일(rank=1도 예외 없이 같은 경로 — n=1일 때
  기존 "후-빔포밍 파워/N0" 공식과 수학적으로 동치임을 대수적으로 확인:
  A=N0+|h|², a=|h|²/A, log2(1+a/(1-a))=log2(1+|h|²/N0)). 4-port/8-port가 썼던
  2×2 전용 catastrophic-cancellation 방어(det 부호 확인, 2026-08-27 발견)는 더
  일반적인 부분피벗 Gauss-Jordan 역행렬로 대체 — 특정 케이스에 대한 임시방편이
  아니라 원인 자체(단순 2×2 폐형 역행렬 공식의 수치 불안정성)를 구조적으로
  해소하는 방식.
- `mimo_channel_draw_4x32()`, `mimo_mmse_detect_4rx3()`(4Rx×3Layer MMSE, 3×3
  Gramian, `mimo_mmse_detect_4rx2()`를 그대로 일반화) 신규 — rank4는 기존
  `mimo_mmse_detect_4x4()`를 그대로 재사용(프리코딩 후 유효 채널이 항상 4×rank라
  Tx 포트 수와 무관, 8-port 확장 때와 동일 논리).
- `run_pdsch_cl_32port_simulation()`, `main.c`에 `MIMO_MODE=CL_32PORT` 라우팅.
  4-port/8-port의 "Adaptive/R1fix/R2fix 3열 비교" 관례와 달리 **Adaptive 단일
  시나리오만** 보고하도록 설계를 바꿈 — R1fix~R4fix 4개 시나리오를 전부 추가하면
  최대 4개 코드워드 인코딩이 5배(1+4)로 늘어나는데 반해 검증 가치는 낮다고 판단한
  것(3GPP 표준 준수 여부와는 무관한 시뮬레이터 리포팅 설계 선택). 대신 SNR별
  선택 rank 분포(%)와 평균 rank를 출력해 massive MIMO 다중화 이득이 SNR에 따라
  어떻게 나타나는지 직접 보여줌. Tx 공간상관/TDL/HARQ는 8-port 때와 동일 사유로
  이번 범위에서 제외, `tasks/todo.md`에 후속 과제로 등록.

**검증**:
- codebook_32port.c 전용 standalone 하네스: rank 1~4 전체 5120개 코드워드
  (1024+2048+1024+1024) 정규화 오차 최대 ~3.3e-16, 직교성 오차 최대 ~1.1e-16
  (부동소수점 잡음 수준) — 코드워드가 너무 많아(8-port의 16배) 4/8-port처럼 개별
  출력 대신 최댓값 집계 방식으로 검증.
- RI 선택기 물리 타당성(결정론적 극단값이 아닌 실측 채널 300trial×6 SNR포인트):
  i.i.d. Rayleigh 채널에서 SNR -5→20dB로 올릴 때 rank 선택 분포가 rank2 위주
  (-5dB: rank2 81%)에서 rank4 위주(20dB: rank4 100%)로 물리적으로 타당하게
  이동 — 4 Rx 안테나가 rank4의 물리적 상한이라 고SNR에서 rank4 수렴이 이론과
  일치. 선택기 1회 호출 ~2ms로 확인(5120후보×Gramian 역행렬), 실사용 trial 수
  기준 성능상 문제 없음.
- clean 빌드 경고 없음(기존 무관 경고만 유지), 회귀 48/48 유지.
- 실제 시뮬레이션 end-to-end(MCS10, -10~20dB, 100trial/포인트, 7포인트 총
  2.25초): BLER 1.0(-10dB)→0.16(20dB) 단조 감소, 평균 선택 rank 1.16→4.00
  단조 상승 동반 — massive MIMO의 핵심 이점인 SNR에 따른 다중화 이득 증가가
  실측으로 재현됨.

### Massive MIMO — 16-port Eigen-Beamforming(SVD), 비-코드북 개루프 방식 (2026-08-31)

32-port 코드북 완료 직후 사용자가 "16 port eigen, SVD 개발" 요청 — 앞서 처음
Massive MIMO 방식을 고를 때 제시했던 3가지 옵션(코드북 확장/Eigen-BF/단순 ZF·MRT)
중 처음엔 코드북 확장을 선택했었고, 이번엔 두 번째 옵션인 Eigen-beamforming을
별도 모드로 추가하는 것. CL_4/8/32PORT 코드북 계열과 근본적으로 다른 접근 —
3GPP Type I SP 코드북(양자화된 유한 후보)에서 탐색하는 폐루프 방식이 아니라,
채널 H의 SVD(H=U·Σ·V^H)에서 우특이벡터 V를 직접 프리코더로 쓰는 개루프 방식
(실제 TDD gNB는 SRS reciprocity로 H를 직접 추정해 이 방식을 씀 — 이 프로젝트는
다른 모듈과 동일하게 genie-aided CSI 가정). 3GPP가 규정하는 코드북이 아니라
신호처리 알고리즘 자체라 3GPP 절/표 인용 대상이 아니며, `CLAUDE.md` 참고서적
목록의 Tse & Viswanath *Fundamentals of Wireless Communication*(SVD 기반 병렬
부채널 분해 챕터)을 근거로 문서화.

**핵심 설계**: Rx=4(기존 관례 고정)이므로 4×16 채널의 SVD를 표준 방식 대신
4×4 Gram 행렬 A=H·H^H의 Hermitian 고유분해로 경량 계산 — 고유값 λ_i=σ_i²,
고유벡터 u_i(=U의 열), 그리고 v_i=H^H·u_i/σ_i(=V의 열, SVD 정의 H^H u_i=σ_i v_i
로부터 유도). 4×4 Hermitian 고유분해는 복소수로 확장한 Cyclic Jacobi 알고리즘을
신규 구현(비대각 원소 a_pq=r·e^{iβ}를 먼저 위상 대각행렬 Φ=diag(1,e^{-iβ})로
실수 대칭 서브행렬로 만든 뒤 표준 실수 2×2 Jacobi 회전 적용, 여러 sweep 반복) —
이번 세션에서 가장 수치적으로 리스크가 큰 신규 알고리즘이라 시뮬레이션 배선 전에
독립 standalone 하네스로 먼저 검증(아래).

rank 적응은 등력 분배(각 활성 스트림에 총 송신전력 1을 균등 분배) 가정 하에
r=1..4 중 `sum log2(1+σ_i²·(1/r)/N0)`를 최대화하는 r 선택 — water-filling(비균등
최적 전력 할당)은 이번 범위에서 제외(후속 과제), `codebook_32port.c`의 "열당
1/rank 전력" 관례와 동일한 가정이라 CL_32PORT와 직접 비교 가능하도록 의도적으로
맞춤. SVD 정의상 H·v_i=σ_i·u_i이므로 프리코더 W=V[:,0:r]/√r를 쓰면 유효 채널
H·W의 열들이 자동으로 직교(U의 열이 정규직교이므로) — 코드북 프리코더와 달리
설계 자체로 스트림 간 간섭이 없어짐. 이 덕분에 CL_32PORT와 동일한 기존 검출기
(mrc_combine_4rx/mimo_mmse_detect_4rx2/4rx3/4x4)를 수정 없이 그대로 재사용
가능하고(유효 채널이 대각에 가까워 특히 잘 조건화됨), h_eff도 명시적 행렬곱
없이 `h_eff[r][c]=σ_c·U[r][c]/√rank`로 대수적으로 바로 유도(H·v_c=σ_c·u_c
관계를 이용, `docs/analysis` 검증에서 확인된 관계식 그대로 적용).

**신규 파일**: `eigen_16port.c`/`.h`(Hermitian 고유분해, SVD, rank 선택기),
`mimo_channel_draw_4x16()`(`mimo.c`), `run_pdsch_eigen_16port_simulation()`
(`pdsch.c`), `main.c`에 `MIMO_MODE=EIGEN_16PORT` 라우팅. Tx 공간상관/TDL/HARQ는
다른 massive MIMO 모드들과 동일 판단으로 이번 범위 제외.

**검증**:
- 신규 Jacobi 고유분해/SVD를 실제 시뮬레이션에 배선하기 전에 2000회 무작위 4×16
  i.i.d. 채널로 독립 standalone 하네스 검증: 전체 재구성 오차 ‖H−U·Σ·V^H‖_F
  최대 7.7e-15, U/V 정규직교성(U^H U−I, V^H V−I) 오차 최대 2.1e-15, SVD 정의
  관계 ‖H·v_i−σ_i·u_i‖ 최대 8.5e-15(전부 부동소수점 잡음 수준), 특이값 내림차순
  정렬 위반 0건 — 새 수치 알고리즘을 표면 결과만으로 신뢰하지 않고 중간 계산값을
  직접 검산한 것(`tasks/lessons.md` 2026-08-27 교훈 적용).
- rank 선택기 물리 타당성(500trial×6 SNR포인트): 평균 rank가 SNR에 따라 매끄럽게
  상승(-5dB→0dB 구간에서 rank4로 빠르게 수렴), CL_32PORT보다 저SNR에서도 rank4를
  더 적극적으로 선택 — 코드북 양자화 없이 진짜 채널 고유벡터를 쓰므로 "약한"
  고유모드도 실제 유효 이득이 32-port DFT 코드북의 정렬 안 맞는 후보들보다 훨씬
  크기 때문(4×16 i.i.d. 채널의 4개 고유값이 서로 크게 차이 나지 않는 것과도
  일치) — 다른 메커니즘(코드북 양자화 손실 vs 완전 최적 방향)에서 기인한
  정량적 차이이지 결함 아님.
- 실제 시뮬레이션 end-to-end(MCS10, -10~20dB, 100trial/포인트, 7포인트 총
  0.98초): BLER 1.0(-10dB)→0.0(20dB) 완전 수렴 — 동일 조건 CL_32PORT는 0.16에서
  바닥(코드북 프리코더의 잔여 스트림간 간섭 때문) — 개루프 eigen-beamforming이
  폐루프 코드북 방식의 양자화 손실 없는 상한(upper bound)임을 실측으로 보여주는
  물리적으로 타당한 대비.
- clean 빌드 경고 없음(기존 무관 경고만 유지), 회귀 48/48 유지.

### 채널추정 확장 — MMSE(Wiener)/DFT 기반 (2026-08-31)

16-port Eigen-BF 완료 후 사용자가 "imperfect CSI로 빔포머 강건성 테스트" 설계를
논의하다가, 빔포머(프리코더 설계) 작업 전에 먼저 채널추정 체인 자체를 LS 하나만
쓰지 말고 MMSE/DFT까지 갖추자고 지적 — 타당한 지적으로 즉시 반영. 기존
`channel_estimation.c`는 LS 추정(`ls_estimate`)과 선형보간(`interpolate_channel`)
만 있었고, `mmse_equalize()`는 이름과 달리 채널추정이 아니라 이미 알려진/추정된
채널로 수신 데이터를 등화하는 함수라 별개.

**신규 추가** (`channel_estimation.c`/`.h`, 범용 — 특정 채널/MIMO 모드에 종속되지
않음):
- `mmse_channel_estimate()` — 파일럿 도메인 LMMSE(Wiener) 스무딩. 지수 PDP 기반
  주파수 상관 모델 `ρ(Δf)=1/(1+j2πΔf·τ_rms)`(van de Beek et al. 1995 IEEE VTC;
  Molisch *Wireless Communications*와 동일 결과 — mimo.c의 Kronecker 공간상관,
  tdl.c의 PDP와 동일한 "구현 정의, 3GPP 비규정" 철학)로 M×M(M=파일럿 수) 상관
  행렬을 만들어 `H_mmse = R·(R+N0·I)⁻¹·H_ls`. 임의 크기 M×M 복소 Gauss-Jordan
  역행렬(`inv_dynamic`, malloc 기반)을 신규 구현 — 기존 mimo.c/codebook_32port.c의
  고정크기(N≤4) 버전을 일반화.
- `dft_channel_estimate()` — 시간영역 잡음절단(truncation) 기법. `dft_precode.h`의
  기존 임의 M-포인트 유니터리 DFT/IDFT(PUSCH DFT-s-OFDM용으로 이미 있던 것)를
  그대로 재사용 — IDFT로 시간영역 채널 임펄스 응답 추정치를 얻은 뒤 처음
  num_taps개(지연확산 이내)만 남기고 나머지를 0으로 잘라 다시 DFT.

**검증**: TDL 멀티패스 채널(DS=300ns, 20RB, comb-2 DMRS 파일럿 120개/240 부반송파)
기준 300trial×6 SNR포인트로 LS/DFT(tap 10/20/40)/MMSE의 정규화 MSE 직접 비교
(순수 i.i.d. 채널로는 두 기법 모두 효과가 없어 검증 무의미 — 반드시 주파수상관이
있는 멀티패스로 검증해야 함):
- MMSE가 전 SNR 구간에서 LS와 모든 DFT tap 설정보다 항상 낮은 NMSE(예: 20dB에서
  MMSE=2.3e-3 vs LS=7.7e-3) — LMMSE 추정기가 이론상 항상 LS 이상이어야 한다는
  사실과 정확히 일치.
- DFT 기반은 tap 수 선택에 따라 편향-분산 트레이드오프가 뚜렷하게 나타남 — tap이
  너무 적으면(10개) 저SNR에서는 유리하지만 고SNR에서 LS보다 나빠지는 오차 바닥이
  생김(실제 채널 에너지를 잘라내는 편향), tap 20/40은 이 바닥이 크게 줄어듦(교과서
  적 "윈도우 폭과 SNR의 트레이드오프"와 일치) — 새 구현이 알려진 이론적 특성을
  정확히 재현함을 확인.
- clean 빌드 경고 없음, 회귀 48/48 유지(기존 LS/interpolate_channel 시그니처
  불변이라 기존 PDSCH/PBCH/PDCCH 사용처 전부 영향 없음).

이 채널추정 확장은 아직 어떤 특정 시뮬레이션에도 배선되지 않은 범용 빌드
블록 상태 — 다음 단계로 EIGEN_16PORT(또는 다른 massive MIMO 모드)의 프리코더
설계에 genie-aided H 대신 이 추정 체인으로 얻은 H_est를 먹여 "불완전 CSI 하의
빔포머 강건성"을 실측하는 배선 작업이 남아있음(사용자와 다음에 이어서 진행 예정).

### EIGEN_16PORT에 imperfect CSI 배선 — TDL + LS/MMSE/DFT 채널추정 (2026-08-31)

바로 이어서 사용자가 "진행하자"고 확인 — 위에서 완료한 채널추정 체인(LS/MMSE/DFT)을
실제로 16-port Eigen-BF 프리코더 설계에 연결. `run_pdsch_eigen_16port_tdl_simulation()`
신규 작성(`run_pdsch_eigen_16port_simulation()`의 TDL+imperfect-CSI 확장), `main.c`에
`MIMO_MODE=EIGEN_16PORT`+`CHANNEL_MODEL=TDL` 라우팅. 새 설정
`EIGEN16_CHAN_EST=NONE|LS|MMSE|DFT`(config_parser.c에 kv_str/검증/요약출력 추가,
기본값 MMSE)로 네 가지를 직접 비교 가능.

**아키텍처**: 프리코더 설계용 채널(H_est)과 실제 하향링크 전송 채널(H_true)을
분리 — UE 4안테나가 SRS를 보내고 gNB가 16안테나로 관측하는 UL reciprocity를
모사해, 파일럿 부반송파(120개, comb-2 DMRS 패턴 재사용)에서 4×16=64개 (Rx,Tx)
안테나쌍마다 노이즈 섞인 관측치를 만들고, 선택한 방식(LS=단순평균/MMSE=Wiener
필터 후 평균/DFT=시간영역 절단 후 평균)으로 처리한 뒤 wideband 평균 H_est_avg를
얻어 SVD 프리코더를 설계한다. 실제 하향링크 전송은 RE별 진짜 주파수선택적
H_true(TDL)로 통과 — 두 채널의 불일치(추정오차 + wideband 프리코더 대
주파수선택적 채널의 근본적 불일치, 후자는 CL_4PORT/8PORT TDL에도 이미 있는
특성)가 실제 잔여 스트림간 간섭으로 나타남. 검출은 flat 버전과 동일한 기존
검출기 재사용(이번엔 h_eff가 더 이상 완전 대각이 아니라 MMSE 보정이 실질적으로
작동).

**성능 최적화**: MMSE의 M×M(M=파일럿 수=120) Wiener 필터 역행렬은 파일럿
패턴·지연확산·SNR(N0)에만 의존하고 실제 채널 값과 무관 — 트라이얼×64안테나쌍마다
다시 풀면 압도적으로 느려지므로, `channel_estimation.c`의 기존
`mmse_channel_estimate()`를 `mmse_build_filter()`(SNR 포인트당 1회, O(M³))와
`mmse_apply_filter()`(이후 매번 O(M²) 행렬-벡터 곱)로 분리하는 리팩터링을
먼저 수행(기존 함수는 두 개를 순서대로 호출하는 wrapper로 유지, 동작 불변
확인). DFT 방식의 num_taps는 지연확산의 8배를 활성 부반송파 샘플주기로 나눈
값(구현 정의 휴리스틱 — 앞선 standalone 검증에서 tap이 너무 적으면 고SNR에서
바닥이 생기는 것을 반영해 넉넉하게 설정).

**검증 및 중요한 발견**: NONE/LS/MMSE/DFT 4가지를 동일 조건(MCS10, DS=300ns,
20RB, -10~20dB, 100trial)으로 나란히 비교 — genie(NONE)이 예상대로 항상 최고
(20dB에서 BLER=0.76, 추정 세 방식은 0.82~0.84로 전부 그보다 나쁨, 상한(upper
bound) 성질이 유지됨을 확인). 그런데 LS/MMSE/DFT 셋 사이의 차이가 예상보다
훨씬 작게 나타나(거의 통계적으로 구분 안 될 정도) — **표면 결과만 보고 "MMSE가
왜 LS보다 안 좋아 보이지?"라고 결론 내리지 않고**(`tasks/lessons.md` 2026-08-27
교훈 적용), 별도 standalone 하네스로 wideband 평균 채널 자체의 NMSE를 직접
측정: 10~20dB에서 LS/MMSE/DFT의 NMSE가 실제로 거의 동일함(예: 20dB에서
LS=3.5e-4, MMSE=3.6e-4, DFT=3.2e-4)을 확인 — **원인은 설계 자체**: 이 아키텍처가
120개 파일럿을 단순 평균해 "wideband 스칼라 하나"만 프리코더에 쓰기 때문에,
평균화 자체가 이미 노이즈의 대부분을 없애버려서(120개 독립 관측치 평균 ≈
노이즈 분산을 120으로 나눔) MMSE의 "주파수상관을 활용한 추가 스무딩" 이득이
설계 단계에서 거의 사라짐. 이는 버그가 아니라 CL_4PORT/8PORT TDL과 동일한
"Wideband PMI" 설계 선택이 만드는 자연스러운 결과 — MMSE의 진짜 이득은
RE별(주파수선택적) 채널추정치가 필요할 때 나타나며, 이는 앞선 채널추정
자체의 standalone 검증(위 항목, RE별 NMSE에서 MMSE가 확실히 우위)에서 이미
확인됨. `tasks/todo.md`에 "RE별(주파수선택적) 프리코딩으로 확장하면 MMSE
이득이 더 뚜렷해질 것"을 후속 과제로 남김.

clean 빌드 경고 없음, 회귀 48/48 유지. DFT 방식은 (r,t) 쌍마다 O(num_active²)
직접 DFT/IDFT를 매 트라이얼 수행해 다른 세 방식(1.2~1.9초/7포인트×100trial)보다
현저히 느림(28초) — 실사용 시 트라이얼 수를 줄이거나 향후 FFT 기반으로 최적화
필요, 현재는 성능보다 정확성 검증이 우선이라 그대로 둠.

### EIGEN_16PORT DFT 채널추정 경로 성능 최적화 (2026-08-31)

바로 위 배선 작업에서 남긴 성능 이슈(DFT 방식이 다른 방식보다 15~20배 느림)를
곧바로 해결. 당초 후보였던 "FFT로 재작성"은 하지 않기로 함 — `dft_precode.c`는
PUSCH DFT-s-OFDM에도 쓰이는 이미 검증된 공용 코드이고, 그 파일 자체의 문서
주석이 "3GPP가 M을 {2,3,5}의 곱만 요구해 임의 M을 지원하려고 의도적으로
FFTW 없는 direct DFT를 쓴다"고 명시하고 있어 — 범용 FFT 재작성은 위험 대비
이득이 낮다고 판단.

대신 채택한 방법: `interpolate_channel→IDFT→절단→DFT→data RE 평균`이라는
파이프라인 전체가 파일럿 벡터 `h_ls_pilot`에 대해 **선형연산**이라는 점에
주목(모든 단계가 선형이고, `dft_num_taps`는 SNR·트라이얼·안테나쌍과 무관하게
고정이므로 이 선형함수 자체도 시뮬레이션 전체에서 고정) — 즉
`H_est_avg[r][t] = w_dft · h_ls_pilot`인 고정 가중치 벡터 `w_dft`(길이
num_pilots)가 반드시 존재한다. 이 `w_dft`를 시뮬레이션 시작 시 표준기저벡터
(단위벡터) num_pilots개를 파이프라인에 흘려 "임펄스 응답"을 측정하는 방식
(선형시스템의 중첩의 원리)으로 딱 1회만 계산해두고, 트라이얼×64안테나쌍마다는
`O(num_active²)` 전체 파이프라인 대신 `O(num_pilots)` 내적 한 번만 수행하도록
`run_pdsch_eigen_16port_tdl_simulation()`을 수정. MMSE도 이미 SNR당 1회 필터
계산 + O(M²) 재사용(`mmse_build_filter`/`mmse_apply_filter`) 패턴이라 동일한
최적화 철학의 연장.

**검증**: 최적화가 결과를 바꾸지 않는다는 것을 두 단계로 확인 — (1) 별도
standalone 하네스로 20개 무작위 파일럿 벡터에 대해 사전계산 경로(fast, w_dft
내적)와 기존 brute-force 경로(interpolate+dft_channel_estimate+평균)의 출력을
직접 비교, 오차 최대 ~2e-16(부동소수점 잡음 수준)으로 완전히 일치함을 확인.
(2) 실제 시뮬레이션(MCS10, DS=300ns, 20RB, -10~20dB, 100trial)을 재실행 —
동일 난수 시퀀스로 BER/BLER/AvgRank 등 출력이 전 SNR 포인트에서 최적화 전과
완전히 동일한 값(당연한 결과, 선형변환을 다른 경로로 계산했을 뿐)을 유지하면서
런타임은 **28.2초 → 1.56초로 약 18배 단축** — 이제 MMSE(1.9초)/LS(1.5초)와
비슷한 속도. 회귀 48/48 유지, clean 빌드 경고 없음.

`tasks/todo.md`의 "EIGEN_16PORT wideband→RE별 프리코딩 확장" 항목은 아직
남아있음(이번 세션에서 발견한, MMSE/DFT의 진짜 채널추정 이득이 BLER에
드러나게 하려면 필요한 아키텍처 확장 — 성능과는 별개의 후속 과제).

### EIGEN_16PORT Subband(PRG) 프리코딩 확장 (2026-08-31)

바로 위에서 남긴 후속 과제("확장시작해") 착수. `run_pdsch_eigen_16port_subband_
simulation()` 신규 — 기존 wideband 함수(`run_pdsch_eigen_16port_tdl_simulation()`)
는 그대로 두고 별도 함수로 추가(회귀 위험 최소화). 신규 설정
`EIGEN16_PRECODER_GRAN=WIDEBAND|SUBBAND`(기본 WIDEBAND, 기존 동작 그대로 보존)
로 main.c에서 분기. 설계: **RI(rank)는 여전히 wideband로 1회만 결정**(3GPP
RI 보고가 원래 wideband/주기적인 것과 일치)하되, **프리코더의 방향(빔포밍
벡터)은 PRG(Precoding Resource block Group, 4RB 단위 — gNB precoderGranularity
개념에 대응하는 구현 정의 값)마다 별도로 그 PRG의 파일럿만 사용해 재계산**.
20RB 기준 5개 PRG. MMSE는 SNR 포인트당 PRG 수만큼 필터 계산(각 PRG의 파일럿
부분배열로 `mmse_build_filter` 재호출), DFT는 PRG별 w_dft를 시뮬레이션 시작 시
1회씩 사전계산(wideband 버전에서 만든 "선형 임펄스 응답" 최적화를 그대로
PRG 단위에 적용 — PRG당 파일럿 수가 24개뿐이라 원래도 빠름).

**구현 중 발견한 버그(배포 전 수정)**: PRG별 프리코더 저장 버퍼를 처음에
`static cx_t Wg_store[64][16][4]`로 고정 크기 선언했었는데, num_prg(=⌈numRB/4⌉)가
NR 최대 273RB 기준 최대 69까지 갈 수 있어 64를 넘으면 버퍼 오버플로우가
발생할 수 있는 잠재 결함이었음 — 실행 전에 인지하고 `malloc`으로 num_prg
크기만큼 동적 할당하도록 즉시 수정(코드 리뷰 중 자체 발견, 실측 노출 전에 해소).

**검증 및 결과**:
1. **Subband 아키텍처 자체는 확실히 유효** — 같은 genie(NONE) 조건에서
   wideband 버전은 20dB에서 BLER=0.76이었는데 subband 버전은 BLER=0.40(500
   trial 기준 0.398)으로 거의 **2배 개선** — PRG별로 실제 주파수선택적
   채널에 맞춰 프리코더 방향을 다시 잡는 것이 wideband 평균 프리코더보다
   훨씬 낫다는 것을 실측으로 확인(실제 gNB가 PRG 단위 PMI/프리코딩을 쓰는
   이유와 정합).
2. **채널추정 방식(LS/MMSE/DFT) 간 BLER 차이는 subband에서도 여전히 거의
   안 보임** — 그런데 이번엔 원인이 다름. 별도 하네스로 PRG 단위(M=24
   파일럿) NMSE를 직접 재보니, **DFT가 LS/MMSE보다 확실히 좋음**(15dB에서
   DFT=4.2e-3 vs LS/MMSE≈5.8e-3, ~1.4배)! 즉 "평균화가 노이즈를 다 없애서
   차이가 안 보인다"던 wideband(M=120)의 원인과 달리, subband(M=24)는
   채널추정 자체에서 방법 간 실제 차이가 존재함에도 최종 BLER(500trial,
   15dB=0.85 안팎, 20dB=0.38~0.40)에서는 네 방식이 거의 구분 안 됨.
3. **DFT가 MMSE보다 나은 이유(중요한 구조적 발견)**: "PRG 평균"이라는
   목표량은 수학적으로 주파수 샘플들의 평균 = 시간영역(지연) 표현의
   DC 성분(tap 0)과 정확히 같다. DFT 기반 추정은 시간영역에서 노이즈가
   지배적인 꼬리 탭들을 직접 잘라내므로 이 DC 성분을 구조적으로 잘
   분리해낸다. 반면 이번에 구현한 MMSE(Wiener) 필터는 **파일럿 지점별
   개별 MSE를 최소화**하도록 설계된 것이라(각 파일럿 위치에서의 점 추정
   최적화), 그 출력들을 나중에 평균 내는 것이 "PRG 평균값 자체의 MSE"를
   최소화하는 것과 같지 않다 — 목표량 자체가 다르다. 진짜 "PRG 평균에
   최적화된 MMSE"를 만들려면 R_hh를 파일럿-파일럿이 아니라 파일럿-평균값
   상관으로 재구성한 별도의(더 단순한, 스칼라 타깃) Wiener 필터가 필요함
   (버그 아니라 추정기 설계 목표와 사용 목적의 불일치 — `tasks/lessons.md`
   식으로 표현하면 "표면 결과만 보고 MMSE 구현이 틀렸다고 결론 내리지 않고"
   원인을 수식으로 끝까지 추적해 정확한 구조적 이유를 확인한 사례).
4. **채널추정 품질 차이가 BLER에 거의 전달되지 않는 이유(시스템 레벨
   해석)**: 이 설정(rank4가 고SNR에서 100% 선택, MCS 고정)에서는 잔여
   스트림간 간섭·고정 MCS-rank 불일치 등 다른 오차 요인이 지배적이라,
   프리코더 설계 채널의 NMSE가 30~40% 줄어드는 정도로는 최종 BLER에
   유의미한 변화를 주지 못하는 것으로 보임 — "이 동작점은 채널추정
   정확도가 아니라 다른 요인에 의해 성능이 제한된다"는 것 자체가 유효한
   시스템 레벨 결론.

회귀 48/48 유지, clean 빌드 경고 없음. `tasks/todo.md`에 "PRG-평균 타깃
전용 MMSE(Wiener) 추정기" 아이디어를 후속 과제로 등록(위 3번 발견에서
파생 — 구현하면 이론상 DFT보다도 나은 NMSE를 보여줄 수 있으나, 4번 발견을
고려하면 최종 BLER 개선은 제한적일 가능성이 큼).

### CL_32PORT Tx 공간상관(Kronecker N1=N2=4 2D) 모델 (2026-09-01)

Massive MIMO 관련 남은 후속 과제 중 사용자가 지정한 것부터 진행. CL_8PORT
때(2026-08-31) 만든 N1=4 1차원 지수상관 모델을, 32-port가 실제로는 진짜
2D 배열(N1=4 수평 × N2=4 수직)이라는 점에 맞춰 확장.

**모델**: `mimo_apply_tx_correlation_4x32()`(`mimo.c`/`.h`) 신규 — URA
(Uniform Rectangular Array)의 표준적인 분리 가능(separable) Kronecker
상관 모델 R_2D = R_horiz ⊗ R_vert를 적용. 수평/수직 두 축은 서로 다른
텐서 성분이라(같은 축의 8-port rho vs 다른 축의 rho_xpol이 커뮤트했던
것과 동일 논리) 두 4×4 지수상관 Cholesky 인수(rho_h^|i-j|, rho_v^|i-j|,
8-port 때 만든 `chol_exp_corr4()` 그대로 재사용)를 순서 무관하게 순차
적용하는 것만으로 정확한 결합 연산자가 됨 — 새 고유분해나 32×32 행렬
연산이 전혀 필요 없음. 편파 간 XPD(rho_xpol)는 기존과 동일한 (n1,n2)당
2×2 블록을 16번 반복(N1/N2와 무관, 이전에도 확인된 성질).

신규 설정 `SPATIAL_CORR_TX_VERT`(수직 축) 추가 — 기존 `SPATIAL_CORR_TX`는
CL_32PORT에서는 수평 축으로 재해석(4/8-port에서는 기존 의미 그대로 유지,
필드 재사용이라 하위 호환 깨짐 없음). `run_pdsch_cl_32port_simulation()`
채널 드로우 직후에 배선, `config_parser.c` 요약 출력 조건에 CL_32PORT
추가.

**검증**: 200,000회 Monte Carlo로 수평(고정 n2=0, n1=0..3)·수직(고정
n1=0, n2=0..3) 두 축 각각의 실측 공분산이 이론 rho_h^|i-j|/rho_v^|i-j|와
오차 <0.005로 일치, 32포트 전체 분산이 1.0 근방(0.997~1.004)으로 정규화
유지됨을 확인. XPD 교차상관(rho_xpol=0.6)도 이론값과 오차 <0.001로 일치,
축 간 교차항(다른 (n1,n2), 같은 편파)은 0.004로 사실상 0(축 분리성 확인).
시뮬레이션 레벨: i.i.d.(모든 rho=0)에서 rank 선택이 섞여 나옴(평균
2.31) → rho_h=rho_v=rho_xpol=0.999(거의 완전 상관, 사실상 rank-1 채널)
에서 rank1 100% 선택 + BLER 1.0→0.0067로 극적 개선 — 4-port/8-port
공간상관 검증 때와 동일한 물리적으로 타당한 패턴(완전 상관 시 공간
자유도가 사라져 코드북이 정확히 rank-1로 수렴). 회귀 48/48 유지, clean
빌드 경고 없음.

남은 massive MIMO 후속 과제는 CL_32PORT TDL/HARQ 변형 하나만 남음.

### CL_32PORT TDL/HARQ 변형 (2026-09-01)

Massive MIMO 후속 과제 마지막 항목. `run_pdsch_cl_32port_tdl_simulation()`
(Wideband PMI)과 `run_pdsch_cl_32port_harq_simulation()`(HARQ Circular
Buffer, FLAT/TDL 모두)을 8-port TDL/HARQ 버전과 동일한 구조로 32 Tx
포트·rank 1~4까지 확장. TDL은 flat 버전과 동일하게 "Adaptive" 단일
시나리오만 보고(고정-rank 4개 시나리오는 코드워드 인코딩 배수만 늘리고
검증 가치가 낮다는 flat 버전 때의 설계 판단을 그대로 유지). HARQ는 시도
0에서 wideband H_avg(TDL) 또는 H_flat0(FLAT) 기준으로 RI+PMI를 rank
1~4 중 하나로 고정한 뒤 재전송까지 유지(CW 수=rank, 최대 4개) — 5G NR
HARQ가 재전송 동안 동일 프리코더를 유지하는 실제 동작과 일치. Tx
공간상관(2D Kronecker, 2026-09-01 앞서 완료)도 두 함수 모두에 배선(TDL은
RE별 H_cache에 개별 적용 후 H_avg 누적, HARQ는 시도마다 채널 재추첨 시
동일하게 적용 — 8-port TDL/HARQ와 동일 순서).

**검증**: 회귀 48/48 유지, clean 빌드 경고 없음. 수동 시뮬레이션:
- TDL(MCS10, DS=300ns, 20RB, -10~20dB, 100trial): BLER 1.0(-10dB)→0.86
  (20dB) 단조 감소, 평균 선택 rank 1.00→3.79 동반 상승 — 8/32-port flat
  버전에서 이미 검증된 것과 동일한 massive MIMO 다중화 패턴이 TDL 채널
  에서도 그대로 재현됨.
- HARQ(MCS15, FLAT/TDL 둘 다, -5~10dB, 100trial): BLER(1st)는 높게
  유지되다가(고MCS라 1차 시도 성공률 낮음) HARQ 재전송으로 BLER(HARQ,
  최종)가 개선(예: TDL 10dB에서 BLER(1st)=1.0 → BLER(HARQ)=0.84), AvgTx가
  고SNR에서 4.0 아래로 하락(조기 성공 종료), AvgRank도 SNR에 따라 상승 —
  기존 4/8-port HARQ와 동일한 물리적으로 타당한 패턴, 8-port에서 확립된
  구조를 rank 1~4로 확장한 것이 정상 동작함을 확인.

CSI/Massive MIMO 관련 이번 작업 스레드(2026-08-27 착수) 전체 완료 —
4-port 정합화, 8-port rank1/2 + 배선(평탄/TDL/HARQ/공간상관), 32-port
코드북(rank1~4) + 배선(평탄/TDL/HARQ/2D 공간상관), 16-port Eigen-BF
(SVD, 평탄/TDL/imperfect CSI/subband 프리코딩), 채널추정(LS/MMSE/DFT)
확장까지 모두 마무리.

### CL_4/8/32PORT TDL에 채널추정(LS/MMSE/DFT) 연결 (2026-09-01)

사용자가 "채널추정·빔포머 필요요소가 다 들어갔는지" 확인하는 과정에서, 앞서
만든 LS/MMSE/DFT 채널추정 체인이 실제로는 EIGEN_16PORT 한 경로에만 배선돼
있고, 코드북 기반 빔포머 3종(CL_4PORT/CL_8PORT/CL_32PORT)은 여전히
genie-aided RI+PMI 설계였다는 사실을 정직하게 짚어 보고 — 사용자가 마저
연결해달라고 요청해 즉시 진행.

**구현**: 신규 공용 설정 `CHAN_EST_METHOD=NONE(기본값)/LS/MMSE/DFT` 하나로
CL_4PORT/CL_8PORT/CL_32PORT의 TDL 변형(`run_pdsch_cl_4port_tdl_simulation`,
`_cl_8port_tdl_simulation`, `_cl_32port_tdl_simulation`) 전부를 제어 —
EIGEN_16PORT 전용 `EIGEN16_CHAN_EST`는 그대로 남기고(기존 동작 불변), 코드북
계열에 별도 이름의 새 필드를 도입(세 모드가 개념적으로 동일한 "wideband RI+
PMI 설계용 채널추정"이라 하나로 공유). NONE(기본값)은 기존 genie 평균과
완전히 동일한 코드 경로를 그대로 타서 하위 호환 100% 보존.

패턴은 EIGEN_16PORT TDL 때 만든 것을 그대로 재사용하되 한 가지 차이를
반영: CL_4/8/32PORT는 **Tx 공간상관이 있어** 파일럿 위치에서도 진짜 채널을
r,t 쌍마다 독립적으로 뽑을 수 없고(상관이 t축을 가로질러 섞으므로), 파일럿
위치 하나마다 전체 4×T 채널행렬을 먼저 만들고 `mimo_apply_tx_correlation_
4xT`를 그 위치에서 한 번에 적용한 뒤에야 노이즈를 더해 각 (r,t) 쌍의 파일럿
시퀀스를 뽑아냈다(EIGEN_16PORT는 애초에 Tx 상관이 없어서 (r,t) 쌍마다 독립
루프로 충분했음, 이번엔 그 전제가 깨져 위치 우선 루프로 재설계). DFT/MMSE
사전계산(w_dft 임펄스 응답, mmse_build_filter)은 Tx 상관과 무관한 순수
주파수영역 선형연산이라 기존 코드 그대로 재사용 가능함을 확인(추가 검증 불필요
— 이미 EIGEN_16PORT에서 별도로 검증됨).

**검증**: 회귀 48/48 유지(NONE 기본값이 기존 동작과 코드 경로까지 동일해
회귀 결과 자체가 무변경 보장), clean 빌드 경고 없음(기존 무관 경고만 유지).
CL_4PORT/CL_8PORT/CL_32PORT 세 모드 모두 NONE/MMSE로 수동 실행 — 크래시
없이 물리적으로 타당한 값(BLER이 SNR에 따라 감소, rank/R1% 분포가 기존
패턴과 일치) 확인. NONE과 MMSE 간 차이는 100trial 규모에서는 잡음 수준으로
작게 나타남 — EIGEN_16PORT wideband 때 이미 규명한 것과 동일한 원인
(wideband 평균화 자체가 노이즈 대부분을 제거해 추정방식 간 차이가 희석됨,
버그 아님)이 여기서도 그대로 적용되는 것으로 판단, 별도 재검증 없이 기존
결론을 재사용.

이제 채널추정(LS/MMSE/DFT)이 EIGEN_16PORT뿐 아니라 코드북 기반 빔포머 3종
전부에 연결됨 — "필요 요소가 다 들어갔는가"에 대한 답이 완전해짐.

### 평균 타깃 전용 MMSE(Wiener) 추정기 도입 (2026-09-01)

"다음 단계 추가" 요청에서 사용자가 선택한 항목. subband 확장(2026-08-31)에서
발견한 문제(기존 MMSE는 "파일럿 지점별 개별 MSE 최소화"가 목표라, 그 출력을
서브밴드/wideband 평균에 쓰면 DFT보다도 NMSE가 나쁠 수 있음)를 근본적으로
해결 — LMMSE를 **타깃 자체**(Y = 평균값)에 대해 다시 유도했다.

**수식**: 목표를 Y=(1/D)·Σ_d H(f_target[d])로 두면
Ŷ = r_yz · (R_pilot,pilot+N0·I)⁻¹ · h_ls (스칼라)이고
r_yz[p] = E[Y·H*(f_pilot[p])] = (1/D)·Σ_d ρ(f_target[d]−f_pilot[p])
(ρ는 기존 mmse_build_filter와 동일한 지수 PDP 주파수상관 모델). 이는
**어떤 선형 추정기보다도 이 타깃에 대해 낮거나 같은 MSE를 갖는다는 것이
LMMSE 이론상 보장**되므로(직교성 원리), 올바르게 구현했다면 DFT를 포함한
기존 추정기를 항상 이기거나 최소한 같아야 한다 — 강력하고 검증 가능한
이론적 예측.

**구현**: `mmse_build_avg_filter()`(`channel_estimation.c`/`.h`) 신규 —
`pilot_pos`/`num_pilots`에 더해 `target_pos`/`num_target`(평균 낼 위치들)을
받아 길이 M 가중치 "행벡터" w_avg를 반환, 이후 `Ŷ=Σ_p w_avg[p]·h_ls[p]`
내적 한 번으로 스칼라 추정치를 얻는다(기존 `mmse_build_filter`+
`mmse_apply_filter`+평균의 2단계보다 한 단계 적고 더 정확함).

**검증(구현 전에 먼저 이론 예측을 확인)**: 별도 하네스로 PRG 단위(M=24)
NMSE를 재측정 — 새 avg-최적 MMSE가 **모든 SNR에서 DFT를 확실히 이김**
(20dB: MMSE(avg,신규)=1.11e-3 vs DFT=1.75e-3 vs MMSE(기존,point-최적)=3.53e-3
— 기존 point-최적 MMSE는 여전히 LS와 별 차이 없었던 반면 신규 avg-최적
MMSE는 DFT 대비 약 1.6배 낮은 NMSE). LMMSE 이론의 예측이 정확히 실측으로
확인됨.

**배선**: 5개 사용처(`run_pdsch_cl_4port_tdl_simulation`,
`_cl_8port_tdl_simulation`, `_cl_32port_tdl_simulation`의 wideband MMSE,
`run_pdsch_eigen_16port_tdl_simulation`의 wideband MMSE,
`run_pdsch_eigen_16port_subband_simulation`의 wideband(rank 선택용)+PRG별
(프리코더 방향용, 원래 발견이 나온 자리) MMSE 총 6곳) 전부를
`mmse_build_filter`+`mmse_apply_filter`+평균에서 `mmse_build_avg_filter`+
내적으로 교체 — 이제 더 이상 필요 없어진 `h_est_pilot` 중간 버퍼도 6곳
전부에서 제거. PRG별 필터는 그 PRG의 파일럿 → 그 PRG의 data RE 평균을
타깃으로 정확히 맞춰 유도(기존에 "PRG 평균"이라는 목표와 무관하게 파일럿
지점 각각을 최적화하던 것과의 근본적 차이).

**최종 BLER에서 관측된 것(예측과 정합)**: subband EIGEN_16PORT를 500trial로
재실행한 결과, 채널추정 자체의 NMSE는 확연히 개선됐지만 최종 BLER(15dB
0.838~0.852, 20dB 0.380~0.402 범위)는 NONE/LS/MMSE/DFT 네 방식이 여전히
거의 구분 안 됨 — 2026-08-31에 이미 예측했던 "이 동작점(rank4 100% 선택,
고정 MCS)은 채널추정 정확도가 아니라 다른 요인(잔여 스트림간 간섭 등)에
의해 성능이 제한된다"는 결론이 그대로 재확인됨. 즉 이번 수정은 **채널추정
자체를 이론적으로 올바르게 만든 것**이지(LMMSE 이론 예측과 정확히 일치),
BLER 개선을 만드는 것이 목적이 아니었고 실제로도 아니었다 — 두 가지가
독립적으로 검증된 결과.

회귀 48/48 유지, clean 빌드 경고 없음. 6곳 모두 crash 없이 물리적으로
타당한 값(BLER 단조감소 등) 확인.

### OLLA (Outer Loop Link Adaptation) 신규 착수 (2026-09-01)

CSI/Massive MIMO/채널추정 스레드가 마무리되며 사용자가 "다음 단계 리스트를
세우고 바로바로 진행"하라고 요청 — 새 영역 확장 후보(MU-MIMO/OLLA/빔관리/
CSI Type II) 중 이번 세션 내내 반복 관찰된 "고정 MCS라 rank나 채널 품질과
무관하게 BLER이 갈리는" 현상(CL_4/8/32PORT 전체에서 R2fix가 R1fix보다
나쁘게 나오는 등 계속 언급됐던 것)의 근본 원인인 "폐루프 링크적응 없음"을
가장 먼저 해소하기로 판단해 OLLA부터 착수.

**설계**: 개루프(open-loop) CQI/MCS 선택은 (근사) SNR→MCS 매핑 하나로
결정되는데, 이 매핑이 실제 코드/복호기 성능과 정확히 맞을 이유가 없다
(격차 자체가 코드/블록길이별로 다름 — Tse & Viswanath, *Fundamentals of
Wireless Communication*의 SNR gap 개념, 프로젝트 참고서적에 이미 포함).
OLLA는 매 전송의 ACK/NACK(CRC 결과)를 SNR 오프셋에 누적 반영해 실제 BLER을
목표로 수렴시키는 폐루프 보정 — 3GPP가 알고리즘 자체를 규정하지 않는
구현 정의 영역(Dahlman et al., *5G NR* 책에서 개념적으로 다룸, 참고서적
목록에 포함).

**구현**:
- `olla.c`/`.h` 신규 — `OLLAState`(offset_db, ACK 시 +step_up, NACK 시
  -step_down), `step_up = step_down·bler_target/(1-bler_target)`로
  정상상태 BLER이 정확히 목표로 수렴하도록 자동 계산(구간별 확률보행의
  표준 평형 조건). `olla_select_mcs(effective_snr_db, gap_db, table)`
  — Shannon 용량(η=log2(1+SNR)) + 구현 마진(gap_db)으로 effective_snr_db
  이하에서 요구 SNR을 만족하는 최고 인덱스 MCS를 반환.
- **구현 중 발견·즉시 수정한 버그**: `olla_select_mcs`를 처음에는 "MCS
  인덱스 오름차순 = 스펙트럼 효율 오름차순"을 가정해 첫 미달 지점에서
  조기 종료하도록 짰는데, `tasks/lessons.md` 교훈에 따라 배선 전에 먼저
  MCS 표를 직접 덤프해 이 가정을 검증하다가 TS 38.214 Table 5.1.3.1-1이
  변조차수 전환 경계(MCS16→17, 16QAM→64QAM)에서 스펙트럼 효율이 아주
  미세하게(2.5703→2.5664) 감소하는 지점이 있음을 발견 — 실제 스펙 수치
  (버그 아님, 해당 전환 구간의 의도된 특성)지만 내 조기종료 최적화가
  이 경우 잘못된 결과를 낼 수 있었음. 전수 스캔(29개뿐이라 성능 영향
  없음)으로 즉시 수정.
- `run_pdsch_olla_simulation()`(`pdsch.c`) 신규 — 이 프로젝트의 다른
  PDSCH 함수와 근본적으로 다른 구조(SNR sweep 대신 고정 SNR 시계열,
  MCS가 트라이얼마다 바뀌므로 LDPC 코덱도 트라이얼마다 재초기화).
  SISO/AWGN, DMRS 미모델링(순수 링크적응 연구). Open-Loop(오프셋 항상 0)
  과 OLLA(폐루프 적응) 2-pass를 동일 SNR/트라이얼 수로 비교해 OLLA의
  가치를 직접 보여주는 구조. `MIMO_MODE=OLLA_ENABLE=1`로 `main.c`에서
  다른 dispatch보다 우선 배선.

**검증**: 두 SNR(4dB, 10dB, 각 300/500trial)에서 실행. 두 경우 모두
**Open-Loop이 완전히 빗나감**(BLER=1.0, 즉 100% 블록 실패) — Shannon+
3dB gap 근사가 이 프로젝트의 실제 LDPC 코덱과 (DMRS 미모델링으로 인한)
비교적 짧은 유효 블록길이 조합에는 너무 낙관적이라는 흥미로운 부수
발견(버그 아니라 실제 코덱 성능 특성 — 짧은 블록길이 LDPC는 이상적인
긴 블록길이 코드보다 SNR gap이 더 큰 것이 잘 알려진 현상이라 물리적으로
타당, `OLLA_SNR_GAP_DB` 기본값을 더 크게 조정하는 건 후속 튜닝 과제로
남기고 임의로 고치지 않음 — 오히려 이 낙관적 기본값 덕분에 OLLA가 얼마나
큰 보정을 필요로 하고 실제로 해내는지가 더 극적으로 드러남). OLLA는 두
SNR 모두에서 수백 트라이얼 내로 오프셋을 큰 폭(-5~-7dB)으로 낮춰 MCS를
재조정(4dB: MCS7→2, 10dB: MCS17→8)하고 최종 BLER을 0.12~0.13으로 목표
(0.10)에 근접 수렴시킴 — 토이 모델이 아니라 실제 LDPC+QAM 체인으로 폐루프
보정의 가치를 직접 실증. 회귀 48/48 유지, clean 빌드 경고 없음(기존 무관
경고만 유지).

SISO/AWGN 한 경로만 구현된 상태 — 다른 MIMO 모드(SIMO_MRC/SM_2X2/SM_4X4/
CL_XPORT 등)로 확장하는 건 `tasks/todo.md`에 후속 과제로 등록.

### MU-MIMO — Zero-Forcing Beamforming (ZF-BF) 신규 착수 (2026-09-01)

OLLA에 이어 사용자의 "다음 단계 리스트를 세우고 바로바로 진행" 요청에 따른
두 번째 항목. 기존 SM_2X2/SM_4X4/CL_XPORT는 전부 SU-MIMO(단일 사용자가
여러 스트림/레이어를 받음)였고, 서로 다른 사용자를 동시에 공간적으로
분리해 서비스하는 MU-MIMO는 이 프로젝트에 전혀 없던 축 — gNB PHY 개발자의
실무 관심사(다중 사용자 스케줄링, 공간 분리)에 맞춰 선택.

**스코프(1차 구현)**: gNB Nt=4 안테나, K=2 사용자(각 1 Rx 안테나,
MU-MISO 다운링크 — 교과서·실무에서 가장 흔한 MU-MIMO 기본형). 평탄
페이딩 전용(TDL/HARQ는 후속 과제, `tasks/todo.md` 등록). 두 사용자
동일 고정 MCS(이 프로젝트의 다른 고정-MCS SNR-sweep 함수들과 동일 관례).

**설계**: 프리코딩은 우측 유사역행렬 Zero-Forcing Beamforming
H^+ = H^H(HH^H)^-1 (H는 K×Nt) — 잡음 없는 이상적인 경우 H·H^+=I_K로
사용자 간 간섭을 설계상 정확히 제거한다(Tse & Viswanath; Björnson et al.,
*Massive MIMO Networks* — 둘 다 프로젝트 참고서적의 표준 기법. 3GPP는
프리코더 알고리즘 자체를 규정하지 않으므로 구현 정의). 열별
‖W[:,k]‖²=1/K로 정규화(등력 분배) — 이 스케일링은 실수 양수라 간섭
제거 성질을 보존하며(H[j]·W[:,k]=0, j≠k는 그대로), k=j인 유효 채널만
실수 양수 스칼라로 스케일된다.

**구현**:
- `mumimo.c`/`.h` 신규 — `mumimo_channel_draw()`(i.i.d. Rayleigh 2×4),
  `mumimo_zf_precode()`(2×2 Gramian A=HH^H를 폐형 공식으로 역행렬,
  W_raw=H^H·Ainv, 열별 정규화; Gramian이 특이(두 사용자 채널이 거의
  같은 방향)에 가까우면 방어적으로 0벡터 반환).
- **배선 전 독립 검증**(`tasks/lessons.md` 교훈에 따라 standalone
  하네스로 먼저 확인): 50,000 트라이얼 Monte Carlo로 (1) 사용자 간
  간섭 `|H[k]·W[:,j]|`(j≠k) 최대값 2.110e-15(부동소수점 잡음 수준,
  정확한 널링 확인), (2) 유효 채널이 항상 실수/음수 없음, (3) 평균
  총 프리코더 전력=정확히 1.000000(정규화 정상), (4) 유효 채널 이득
  범위 [0.1391, 3.0793](사용자 채널 상관도에 따른 ZF 열화의 물리적으로
  타당한 분산)를 확인 — 전부 이론 예측과 일치.
- `run_pdsch_mumimo_simulation()`(`pdsch.c`) 신규 — 다른 PDSCH 함수와
  달리 MIMO 검출기가 필요 없음(ZF 간섭제거로 y_k=h_eff_k·x_k+n_k인
  순수 스칼라 채널로 환원, h_eff_k는 트라이얼당 1회 계산 후 블록 내
  고정). 두 사용자는 서로 다른 독립 데이터(별도 CW, 별도 LDPC
  코드워드)를 받는다는 점이 SU-MIMO 다중 레이어(같은 사용자의 서로
  다른 스트림)와의 근본적 차이. 사용자별 BER/BLER + "둘 중 하나라도
  실패" Combined BLER 지표 3열 출력. `MIMO_MODE=MU_MIMO`로 `main.c`에
  신규 분기(HARQ 분기와 동일 위치, TDL/HARQ 조합은 아직 없음 — 무조건
  평탄 페이딩 함수로 직행).

**검증**: `regression_test.sh`에 "PDSCH MU_MIMO flat" 케이스 추가,
49/49 통과(기존 48개 전부 유지). `make clean && make` 경고 없음(기존
무관 경고 5건만 유지). 수동 SNR 스윕(-5~20dB, 2000trial/pt, MCS10
16QAM): 두 사용자 BLER이 거의 대칭(i.i.d. 통계상 예상대로) 이고 SNR
증가에 단조 감소, Combined BLER이 항상 개별 사용자 BLER 이상(합집합
상한과 일치) — 물리적으로 타당.

TDL/HARQ 조합, K>2 또는 다중 Rx 안테나(MU-MIMO 진짜 다중 스트림/사용자)
확장은 `tasks/todo.md`에 후속 과제로 등록.

### 빔 관리(Beam Management) — SSB/CSI-RS 기반 P1 절차 신규 착수 (2026-09-01)

MU-MIMO에 이은 "다음 단계 리스트"의 세 번째 항목. TS 38.213 §8.5 P1
절차(gNB Tx 빔 스위핑 + UE Rx 빔 스위핑으로 최적 빔 쌍 탐색)를 UE Rx
빔 스위핑은 생략(단일 Rx 안테나, 구현 정의 스코프 축소)하고 gNB Tx
빔 스위핑만 모델링. CSI-RS 채널추정(channel_estimation.c)은 이미
있지만 "빔 스위핑 절차 자체"(여러 후보 빔을 순차 송신 → RSRP 측정 →
선택)는 이 프로젝트에 전혀 없던 축이라 선택.

**설계**: 후보 빔 집합은 이미 구현된 CL_32PORT rank-1 코드북
(`codebook_type1_sp_32port_rank1()`)의 i1_1×i1_2×i2 전체 격자
(16×16×4=1024, CB32_RANK1_TOTAL)를 그대로 재사용 — 새 코드북을 만들
필요가 없었다. "참 채널"은 단일 지배경로(LOS) 가정, 코드북 v_{l,m}
공식(array-manifold steering vector, TS 38.214 §5.2.2.2.1)을 정수
격자가 아닌 연속값 l_true/m_true/n_true로 일반화해 생성(공식 자체는
스펙 그대로, 연속값 일반화는 구현 정의 — 표준 안테나 어레이 이론,
Tse & Viswanath; Björnson et al. 참고서적 근거) — 이렇게 하면 참
방향이 코드북 격자에서 벗어난 경우까지 포함해 "양자화 손실"과
"측정잡음 손실" 두 성분을 분리해 평가할 수 있다.

**구현**:
- `beam_mgmt.c`/`.h` 신규 — `beam_mgmt_true_channel()`(연속값 LOS
  steering vector), `beam_mgmt_p1_sweep()`(1024 후보 순차 송신,
  빔마다 `num_rep`회 반복 관측한 RSRP=|H_true^H·W_c|²+noise 평균으로
  최댓값 빔 선택 — SSB 버스트 반복을 통한 측정잡음 평균화 모사),
  `beam_mgmt_genie_best()`(잡음 없이 1024개 전수탐색, 격자 위에서의
  진짜 최적 빔 — 양자화 손실만 반영하는 기준선).
- **배선 전 발견·수정한 설계 결함**: 처음에는 후보를 i1_1×i1_2
  256개로만 두고 i2(편파 위상)를 0으로 고정했는데, standalone
  하네스로 "격자 위(정수 l,m,n) 참 채널은 genie 탐색에서 반드시
  gain=1.0으로 정확히 일치해야 한다"는 불변조건을 검증하다가 n_true가
  1 또는 3일 때 최대 이득이 0.5로 떨어지는(코드북 후보의 편파 위상이
  참 채널과 안 맞아 두 편파 그룹이 상쇄) 결함을 발견 — i2를 뺀 것
  자체가 원인이었으므로 후보 집합을 1024개(i2 포함) 전체로 확장해
  해결(회귀 전 자체 발견·수정, `tasks/lessons.md` 교훈대로 배선 전
  standalone 하네스 검증이 유효했던 사례).
- `run_pdsch_beam_mgmt_simulation()`(`pdsch.c`) 신규 — 매 트라이얼마다
  임의 연속 방향의 참 채널을 뽑고, Genie(양자화 손실만)와 P1(양자화+
  측정잡음)가 각각 선택한 빔을 실제 Tx 프리코더로 써서 rank-1 SISO
  등가 스칼라 채널로 같은 트라이얼·같은 SNR 조건에서 독립 CW 2개를
  전송·복호 비교(MU-MIMO의 "서로 다른 사용자" 비교와 달리 "같은 채널,
  다른 빔 선택 방식" 비교라는 점이 다름). `MIMO_MODE=BEAM_MGMT`로
  `main.c` 신규 분기, 신규 설정 `BEAM_MGMT_NUM_REP`(기본 4, 빔당 RSRP
  반복 관측 횟수).
- **검증**: standalone 하네스(수정 후) — 격자 위 200/200 정확히 일치
  (gain=1.0), off-grid 2000trial 최악 양자화 손실 gain=0.78(항상
  ≤1.0), 초저잡음(N0=1e-6)에서 P1==Genie 매치율 99.7%(300trial),
  저SNR(N0=5)에서 평균 P1 선택 gain(0.054)이 평균 genie gain(0.924)
  보다 뚜렷이 낮음(측정잡음 손실 실증) — 전부 이론 예측과 일치.
  `regression_test.sh`에 "PDSCH BEAM_MGMT flat" 케이스 추가, 50/50
  통과(기존 49개 유지). `make clean && make` 경고 없음(기존 무관
  경고 5건만 유지). 수동 SNR 스윕(-10~15dB, MCS10 16QAM, 1000trial/pt):
  P1==Genie 매치율이 0.3%→45.6%로 단조 증가, Genie/P1 BLER 둘 다 SNR
  증가에 단조 감소, P1이 모든 SNR에서 Genie보다 항상 나쁨(측정잡음
  손실이 전 구간에서 일관되게 관찰) — 물리적으로 타당.

UE Rx 빔 스위핑, TDL/HARQ, P2/P3(빔 정제) 절차는 `tasks/todo.md`에
후속 과제로 등록.

### LLS 완성도 점검 — HARQ dispatch 오배선(silent wrong-dispatch) 발견·수정 (2026-09-01)

사용자 요청으로 "LLS 완성도"를 점검(신규 기능 추가 대신 기존 코드베이스
전체를 감사) — `main.c`의 실제 dispatch 트리를 모든 `run_pdsch_*`/
`run_pucch_*`/`run_pusch_*`/`run_prach_*` 함수 존재 여부와 대조.

**발견(가장 중요)**: `main.c`의 PDSCH `HARQ_ENABLE=1` 분기가
`SM_4X4`/`CL_4PORT`/`CL_8PORT`/`CL_32PORT`/`SM_2X2+TDL`/`SIMO_MRC+TDL`만
전용 함수로 처리하고, 그 외 모든 `MIMO_MODE`(`MU_MIMO`/`BEAM_MGMT`/
`EIGEN_16PORT`/`SM_2X2`+비TDL/`SIMO_MRC`+비TDL)는 `run_pdsch_harq_simulation()`
(MIMO_MODE를 완전히 무시하는 순수 SISO 함수)으로 조용히 떨어지고
있었음 — 에러 없이 `MIMO Mode: MU_MIMO` 헤더를 찍으면서 실제로는 SISO
결과를 냄. 50-case 회귀 테스트는 이런 "잘못된" 조합을 애초에 실행하지
않아 못 잡고 있었음(단순 기능 공백이 아니라 잘못된 결과를 조용히
내보내는 정확성 결함).

**수정**: `config_parser.c`의 `HARQ_ENABLE=1` 검증 블록에 `main.c`의
실제 dispatch 조건을 그대로 미러링한 화이트리스트 체크 추가 —
지원되는 조합(SISO/SM_4X4/CL_4PORT/CL_8PORT/CL_32PORT/SM_2X2+TDL/
SIMO_MRC+TDL) 외에는 `CFG_ERR`로 명시적 에러 후 종료(기존
`OLLA_BLER_TARGET`/`EQUALIZER` 등과 동일한 검증 패턴 재사용). `!ollaEnable
&& useDmrs` 조건도 함께 걸어 실제 dispatch 순서(OLLA·`!useDmrs`가 HARQ
분기보다 먼저 체크됨)와 정확히 일치시킴.
**검증**: MU_MIMO/BEAM_MGMT/EIGEN_16PORT/SM_2X2(flat)/SIMO_MRC(flat) +
HARQ_ENABLE=1 전부 수동으로 명확한 에러 메시지와 함께 종료(exit code 1)
확인, 반대로 기존 정상 조합(SISO+HARQ, SM_2X2+HARQ+TDL)은 여전히 통과
확인. `regression_test.sh`에 negative test 2건 추가(MU_MIMO+HARQ,
SM_2X2+HARQ+flat), 52/52 통과(기존 50개 유지). `make clean && make`
경고 없음.

같은 감사에서 함께 발견된 낮은 우선순위 항목(수정 안 함, `tasks/todo.md`
등록): PUSCH(상향)에 MIMO가 전혀 없음(DL은 SIMO/SM_2X2/SM_4X4/
CL_4/8/32PORT/EIGEN_16PORT/MU_MIMO까지 풀스택인데 UL은 전무, 구조적
비대칭), PUCCH F0/F2는 페이딩 채널 변형이 없음(기존에 이미 문서화된
한계, 신규 발견 아님), `STRUCTURE.md`의 PDSCH dispatch 표가 `CL_4PORT`
에서 멈춰 있어 `CL_8PORT` 이후 7개 기능이 문서에 없음(문서 드리프트).

### STRUCTURE.md 전면 갱신 — 문서 드리프트 해소 (2026-09-01)

위 완성도 점검에서 발견한 문서 드리프트 해소. `codebook.c`/`codebook_8port.c`/
`codebook_32port.c`/`eigen_16port.c`/`olla.c`/`mumimo.c`/`beam_mgmt.c`/
`ul_power_ctrl.c` 8개 파일이 모듈 테이블에 전혀 없었고, PDSCH dispatch
트리는 `CL_4PORT`에서 멈춰 CL_8PORT 이후 7개 기능이 빠져 있었으며,
최상위 채널 분기 트리에도 `ULPC`/`BER`/`NONE`이 없었음. `main.c`의
실제 if/else if 우선순위를 그대로 옮겨적어 재작성(OLLA 최우선 →
`!USE_DMRS` → HARQ 화이트리스트 → MU_MIMO/BEAM_MGMT → 나머지 MIMO_MODE별
flat/TDL). 스크립트로 `pdsch.c`/`pucch.c`/`pusch.c`/`prach.c`의 모든
`run_*` 함수와 STRUCTURE.md의 언급을 diff — 양방향 모두 빈 결과(완전
일치) 확인. `regression_test.sh` "48-case" 표기도 실제 케이스 수로 수정.
부수 발견(수정 안 함): `PHY/src/ber_sim.c`가 `c_Makefile`에 전혀
포함되지 않은 채(2026-07-15 커밋 이후 방치) 저장소에 남아있는 고아
파일 — 독립 실행형 BER 툴 시도로 보이나 현재 빌드되지 않음. 문서 전용
변경이라 재빌드/회귀 불필요.

### PUSCH UL SU-MIMO(SM_2X2) 추가 — DL/UL 구조적 비대칭 해소 (2026-09-01)

완성도 점검에서 발견한 두 번째 항목(PUSCH UL MIMO 전무) 착수. DL은
SIMO/SM_2X2/SM_4X4/CL_4·8·32PORT/EIGEN_16PORT/MU_MIMO까지 완비돼 있었지만
UL(PUSCH) 5개 함수는 전부 단일안테나였음.

**설계**: `mimo.c`의 검출 함수(`mimo_zf_detect`/`mimo_mmse_detect`)와
채널 드로우(`mimo_channel_draw_2x2`)는 RE 단위 순수 함수라 DL/UL 방향과
무관하게 그대로 재사용 가능 — 새 MIMO 수학 코드 없이 `run_pdsch_sm2x2_simulation()`/
`_tdl_simulation()`(pdsch.c)의 구조를 PUSCH 파이프라인(LDPC+rate matching+
DMRS+DFT precoding)에 그대로 이식. UE가 2개 레이어로 서로 다른
코드워드를 동시 전송, gNB가 2 Rx 안테나로 수신, 레이어별 FDM DMRS로
채널 추정.

**스펙 제약 발견·반영**: TS 38.211 §6.3.1.4가 "Transform Precoding은
1개 레이어를 초과하는 전송에 사용할 수 없음"을 명시 — 구현 선택이
아니라 스펙 강제 사항이므로, SM_2X2는 CP-OFDM(`TRANSFORM_PRECODING=0`)
전용으로 설계하고 `config_parser.c`에 `TRANSFORM_PRECODING=1`과 함께
쓰면 `CFG_ERR`로 명시적으로 막는 검증을 추가.

**같은 오배선 클래스 재도입 방지**: 지난 HARQ dispatch 오배선 수정과
동일한 원칙 적용 — PUSCH `HARQ_ENABLE=1`은 여전히 `MIMO_MODE`를 무시하고
무조건 SISO `run_pusch_harq_simulation()`으로 가는데(HARQ+MIMO 조합은
이번 범위 밖), 이 상태로 `MIMO_MODE=SM_2X2`+`HARQ_ENABLE=1`을 조용히
받아들이면 똑같은 함정이 재발하므로 `config_parser.c`에 PUSCH+
MIMO_MODE≠SISO+HARQ_ENABLE=1 조합을 미리 차단하는 검증도 함께 추가.

**구현**: `run_pusch_sm2x2_simulation()`(평탄 페이딩)/`run_pusch_sm2x2_tdl_simulation()`
(TDL, 4개 독립 Tx-Rx 안테나쌍 tap-set) 신규(`pusch.c`), `main.c`에
`MIMO_MODE=SM_2X2` 분기(HARQ 분기보다 뒤, 채널모델 무관 SISO 분기보다 앞)
추가, `#include "mimo.h"` 추가.

**검증**: `regression_test.sh`에 positive 2건(PUSCH SM_2X2 flat/TDL) +
negative 2건(SM_2X2+Transform Precoding, SM_2X2+HARQ) 추가, 56/56 통과
(기존 52개 유지). `make clean && make` 경고 없음. 수동 SNR 스윕(-5~15dB,
MCS10 16QAM, MMSE, 1000trial/pt): flat/TDL 둘 다 BLER 단조 감소, 동일
SNR에서 TDL이 flat 대비 항상 열화(DL SM_2X2와 동일한 기존 패턴, 상세는
2026-07-13 항목 참조) — 물리적으로 타당. 두 신규 검증(Transform
Precoding 위반, HARQ 미지원 조합) 수동으로 명확한 에러+exit code 1 확인,
기존 SISO+DFT-s-OFDM 조합은 영향 없이 그대로 통과 확인.

K>2/다중 Rx 안테나, SM_4X4/코드북 기반 UL, TDL+HARQ 조합은 `tasks/todo.md`에
후속 과제로 등록.

### OLLA를 SIMO_MRC/SM_2X2로 확장 — 첫 MIMO 모드 편입 + 페이딩 유발 BLER 하한 발견 (2026-09-01)

`tasks/todo.md` "진행 중" 목록에서 사용자가 첫 항목("OLLA를 다른 MIMO
모드로 확장")을 선택, 세부 범위(어느 모드부터)를 물어 "SM_2X2까지 함께"로
확정.

**설계**: 기존 SISO OLLA(`run_pdsch_olla_simulation()`)와 동일한 구조
(고정 SNR 시계열, Open-Loop vs OLLA 2-pass, MCS가 트라이얼마다 바뀌므로
LDPC 코덱도 매 트라이얼 재초기화, DMRS 미모델링·genie-aided 채널)를
유지하되, 물리 채널만 AWGN → 2-branch MRC 다이버시티(SIMO_MRC) / 2x2
공간다중화(SM_2X2, ZF/MMSE)로 교체. **핵심 설계 결정**:
`olla_select_mcs()`에 넣는 effective_snr_db 예측식을 SISO와 완전히
동일하게(nominal_snr+offset) 유지하고, MRC의 평균 다이버시티 이득이나
MIMO 검출 손실을 예측식에 반영하지 않음 — OLLA의 강점이 "물리계층의
구체적 이득/손실 요인을 몰라도 ACK/NACK만으로 수렴한다"는 데 있음을
그대로 보여주기 위한 의도적 단순화(구현 정의).

**구현**: `run_pdsch_olla_simo_mrc_simulation()`(`mimo_channel_draw_1x2`+
`mrc_combine`, 트라이얼당 1회 채널 드로우·심볼마다 독립 노이즈)와
`run_pdsch_olla_sm2x2_simulation()`(`mimo_channel_draw_2x2`+
`mimo_zf_detect`/`mimo_mmse_detect`, 2 레이어가 OLLA가 고른 동일 MCS
공유, ACK는 두 레이어 CRC 모두 통과해야 함 — 이 프로젝트 다른 SM_2X2
함수들과 동일한 결합 판정 관례) 신규(`pdsch.c`). `main.c`의
`OLLA_ENABLE=1` 분기를 `MIMO_MODE`로 세분화(SIMO_MRC/SM_2X2/그 외=SISO).
**같은 오배선 클래스 재도입 방지**: `config_parser.c`에 `OLLA_ENABLE=1`+
지원 안 되는 `MIMO_MODE`(SM_4X4/CL_XPORT 등) 조합을 CFG_ERR로 차단하는
검증 추가 — PDSCH/PUSCH HARQ 화이트리스트와 동일 원칙.

**검증 중 발견(버그 아님, 물리적으로 타당함을 확인)**: SIMO_MRC를 4dB
nominal SNR로 실행하자 OLLA가 offset을 -12dB 이상까지 낮췄음에도
MCS가 테이블 최하단(MCS0)에 고정된 채 결합 BLER이 0.15~0.16에서
더 내려가지 않는 현상 발견. `tasks/lessons.md` 교훈에 따라 배선을
의심하기 전에 먼저 standalone 하네스로 MCS0 고정 성능을 직접 측정
— 이론상 요구 SNR(Shannon+3dB gap) 대비 실제 채널이 부족한 확률은
0.7%뿐인데 측정 BLER은 14.27%(약 20배)로 훨씬 높음, 이는 2026-09-01
OLLA 최초 구현 때 이미 발견한 "Shannon+3dB gap 근사가 이 프로젝트의
실제 LDPC 코덱(특히 짧은 유효 블록길이)에는 너무 낙관적"이라는 특성과
정확히 일치 — 새로운 결함이 아니라 기존에 알려진 특성이 블록-플랫 페이딩
(트라이얼마다 채널이 무작위로 바뀜, AWGN 전용이던 SISO OLLA에는 없던
효과)과 결합해 "가장 낮은 MCS로도 목표 BLER을 달성 못 하는" 진짜 하한
(floor)을 만들어낸 것 — MCS 테이블 범위 안에서 폐루프가 보정할 수
있는 한계를 보여주는 타당한 발견. 더 높은 nominal SNR(SIMO_MRC 15dB,
SM_2X2 20dB)로 재실행하면 두 모드 모두 정상적으로 MCS를 재조정하며
목표 BLER 근처로 수렴하는 경향을 확인(SISO 때와 동일하게 500trial
안에서 완전 수렴까지는 아니지만 명확한 하강 추세) — OLLA 메커니즘
자체는 두 신규 모드에서도 올바르게 동작함을 확인.

`regression_test.sh`에 positive 3건(OLLA SISO/SIMO_MRC/SM_2X2 dispatch)
+ negative 1건(OLLA+SM_4X4) 추가, 60/60 통과(기존 56개 유지). `make
clean && make` 경고 없음. SM_4X4/CL_XPORT로의 추가 확장은
`tasks/todo.md`에 후속 과제로 등록.

### MU-MIMO를 TDL/HARQ로 확장 (K=2 고정) (2026-09-01)

`tasks/todo.md` "진행 중" 두 번째 항목 착수 — 사용자가 "2번, MU-MIMO
TDL/HARQ 확장부터"로 범위 확정(K>2/다중 Rx 확장은 이번 범위 밖, 별도
후속 과제로 유지).

**설계**: MU-MIMO의 ZF-BF 프리코더(`mumimo_zf_precode()`)는 K=2 폐형
2×2 역행렬이라 RE당 비용이 사실상 무시할 만한 수준 — 다른 massive-MIMO
모드(EIGEN_16PORT subband 등)가 SVD/고유분해 비용 때문에 PRG 단위
서브밴드 근사를 쓴 것과 달리, MU-MIMO TDL은 PRG 근사 없이 매 RE 정확히
채널·프리코더를 재계산한다. RE 인덱싱은 genie-aided라 실제 DMRS
그리드가 없으므로 `pbch.c`의 페이딩 함수가 이미 쓰던 "심볼 인덱스를
그대로 RE로 취급" 단순화를 재사용. ZF-BF의 H·H^+=I는 채널 값과 무관하게
항상 성립하는 대수적 항등식(우측 유사역행렬의 정의상 성질)이므로,
h_eff[u]가 RE마다 달라져도 여전히 정확히 실수 양수 스칼라라는 성질이
그대로 유지됨을 확인(평탄 버전에서 "이론상 항상 실수"라고만 적었던
것을 이번에 정확한 대수적 근거로 명확히 함).

**구현**:
- `run_pdsch_mumimo_tdl_simulation()`(`pdsch.c`) 신규 — 사용자-Tx
  안테나 쌍마다(K×Nt=8개) 독립 TDL tap-set을 드로우, RE마다 H[K][Nt]를
  `tdl_freq_response()`로 계산 후 `mumimo_zf_precode()`로 프리코더
  재설계. 노이즈 분산은 RE마다 달라지므로(다른 SM_2X2/CL_XPORT TDL
  함수들과 동일한 기존 근사 재사용) RE별 nv를 누적 평균해 demap에 사용.
- `run_pdsch_mumimo_harq_simulation()`(`pdsch.c`) 신규 — flat/TDL
  둘 다 하나의 함수로 지원(`CL_32PORT` HARQ와 동일한 `is_tdl` 내부
  분기 패턴). 두 사용자 모두 독립 mother LDPC 코드워드 + 영구
  soft-combining 버퍼(`rate_matching.c`의 범용 circular buffer
  `rate_match_select`/`rate_match_combine` 재사용), "같은 재전송
  occasion 공유"(이 프로젝트 다른 다중스트림 HARQ 함수들과 동일한
  단순화) — 두 사용자 CRC가 모두 통과해야 종료. MU-MIMO는 RI/PMI 같은
  별도 "고정 후 재사용" 단계가 없어(ZF-BF는 채널만 있으면 즉시 설계
  가능) CL_32PORT HARQ보다 구조가 단순 — attempt마다 채널을 바로
  재드로우하고 즉시 재설계.
- `main.c`에 `MIMO_MODE=MU_MIMO`+`CHANNEL_MODEL=TDL` 분기(HARQ
  비활성) 및 `MIMO_MODE=MU_MIMO`(HARQ 활성, flat/TDL 함수 내부 분기)
  추가. `config_parser.c`의 HARQ 화이트리스트에 `MU_MIMO` 추가(기존엔
  차단 대상이었음 — 전용 함수가 이제 생겼으므로 화이트리스트 갱신).

**검증**: 수동 SNR 스윕(-5~20dB, MCS10, 1000trial/pt) — flat/TDL
비교 결과 TDL이 flat 대비 항상 열화(예: 12.5dB BLER_Comb 0.619(TDL)
vs 0.544(flat)), 이 프로젝트의 다른 TDL 조합들과 동일한 기존 패턴과
일치. HARQ(MCS15, -5~10dB, 500trial/pt, flat/TDL 둘 다): BLER(HARQ,
최종)이 BLER(1st) 대비 크게 개선(예: flat 10dB에서 0.994→0.214),
AvgTx가 고SNR에서 하락, TDL이 flat 대비 항상 열화 — 전부 기존
HARQ 함수들과 동일한 물리적으로 타당한 패턴. `regression_test.sh`에
positive 4건(MU_MIMO TDL, MU_MIMO HARQ flat/TDL) 추가, 기존 "MU_MIMO+
HARQ는 미지원" negative test는 이제 유효한 조합이 됐으므로 제거,
62/62 통과. `make clean && make` 경고 없음.

K>2/다중 Rx 안테나(진짜 다중 스트림/사용자) 확장은 `tasks/todo.md`에
후속 과제로 유지.

### 빔 관리를 TDL/HARQ로 확장 (2026-09-01)

사용자가 "SU-MIMO 관련 먼저 끝내고 MU-MIMO는 나중, 빔관리 확장부터"로
순서 확정. 빔관리 확장 범위(TDL/HARQ vs P2/P3 빔정제 vs UE Rx 빔스위핑)를
먼저 확인, "TDL/HARQ 확장(추천)" 선택 — 이번 세션 MU-MIMO/OLLA와 같은
증분 패턴.

**설계(핵심 분석적 발견)**: P1/Genie 빔 선택은 wideband로 유지하고
(빔 관리는 실무에서도 데이터 스케줄링보다 훨씬 느린 주기로 갱신되는
절차), TDL은 선택된 빔의 하향링크 데이터 전송에만 적용 — 단일
지배경로(LOS) 클러스터 자체의 다중경로를 모사하는 SISO TDL tap-set
1개를 32개 안테나 전체에 공통으로 곱하는 형태로 확장(같은 클러스터는
배열 전체에 동일한 주파수응답을 준다는 근사). **매 RE의 채널이
H_true[t]·g(RE) 형태(g(RE)가 모든 후보 빔에 동일하게 곱해짐)이므로,
후보 빔 간 순위는 g(RE) 값과 무관하게 항상 wideband 버전과 정확히
동일함을 대수적으로 증명** — TDL이 "어느 빔이 선택되는가"에는 영향을
주지 않고 "선택된 빔이 데이터 평면에서 얼마나 잘 동작하는가"에만
영향을 준다는, 이 프로젝트의 "CL_XPORT: wideband PMI 설계 + per-RE
실제 전송" 패턴과 정확히 같은 철학을 재확인.

**구현**: `run_pdsch_beam_mgmt_tdl_simulation()`(단일 클러스터 공유
SISO TDL tap-set), `run_pdsch_beam_mgmt_harq_simulation()`(flat/TDL
하나의 함수, 빔 선택은 트라이얼당 1회만 수행해 모든 attempt에서
재사용 — CL_32PORT HARQ의 "RI/PMI attempt 0 고정" 패턴과 동일 철학,
flat은 채널도 고정돼 attempt마다 잡음만 재드로우·TDL은 클러스터
tap-set을 attempt마다 재드로우) 신규(`pdsch.c`). Genie/P1 두 경로는
기존 다중스트림 HARQ 함수들과 동일하게 "같은 재전송 occasion 공유"
단순화 재사용(구현 정의 — 물리적으로는 독립 재시도 루프가 더 정확하지만
프로젝트 전체 일관성 우선). `main.c`/`config_parser.c` HARQ
화이트리스트에 BEAM_MGMT 반영.

**검증 중 확인한 사실(버그 아님)**: 수동 SNR 스윕에서 TDL이 flat보다
훨씬 크게 열화됨을 발견(15dB에서 BLER_Genie 0.13(flat) vs 0.80(TDL)) —
플레인 SISO PDSCH TDL vs flat 격차(같은 MCS10/15dB에서 0.52 vs 0.76,
격차 ~0.24)와 비교해 훨씬 큰 격차(~0.67). 원인 분석: 빔관리의 "flat"
동작점은 32안테나 빔포밍 배열이득(~15dB) 덕분에 매우 높은 유효
SNR에서 동작해(BLER이 waterfall 꼬리 깊숙이 위치) 절대 BLER이 이미
매우 낮은 상태이고, TDL의 "안테나 전체가 공유하는 단일 스칼라 페이딩"
(공간/주파수 다이버시티 전혀 없음 — MU-MIMO/SM_2X2/CL_XPORT처럼
안테나쌍마다 독립 TDL을 쓰는 경우와 다름)이 깊은 페이드 순간에는
전체 코드워드를 무방비로 무너뜨리므로, 이미 낮은 flat BLER 대비
상대적 열화폭이 훨씬 크게 나타난 것 — 다이버시티 없는 rank-1
빔포밍 링크가 페이드에 훨씬 취약하다는 잘 알려진 사실과 정성적으로
일치, 방향은 플레인 SISO TDL과 같고 크기만 다름(구조적으로 타당,
버그 아님). HARQ 초기 테스트(MCS15, -10~5dB)에서 BLER(HARQ)이 계속
1.0으로 안 움직여 결함을 의심했으나, SNR을 10~20dB로 넓혀 재검증한
결과 정상적인 waterfall 전이(10dB: 1.0→0.51, 15dB: 0.94→0.0, AvgTx
단조감소)를 확인 — 처음 테스트한 SNR 구간이 MCS15에 비해 너무 낮았을
뿐, 결합 자체는 정상 동작.

`regression_test.sh`에 positive 4건(BEAM_MGMT TDL, BEAM_MGMT HARQ
flat/TDL) 추가, 65/65 통과(기존 62개 유지, HARQ 화이트리스트 갱신으로
BEAM_MGMT+HARQ가 이제 유효한 조합이 됨). `make clean && make` 경고
없음.

UE Rx 빔 스위핑, P2/P3(빔 정제) 절차는 `tasks/todo.md`에 후속 과제로
유지.

### PUSCH UL SM_2X2에 HARQ 추가 (TDL 전용, DL과 동일한 비대칭) (2026-09-01)

사용자가 "SU-MIMO 먼저 끝내자"는 방향에서 UL MIMO 확장 범위(HARQ 추가
vs SM_4X4 신규 vs gNB Rx 안테나 확장)를 확인, "UL SM_2X2에 HARQ 추가
(추천)" 선택.

**구현**: `run_pdsch_sm2x2_tdl_harq_simulation()`(pdsch.c)의 구조를
PUSCH 파이프라인으로 그대로 이식 — mother LDPC 코드워드 + 영구
soft-combining 버퍼(레이어별 독립), 4개 독립 Tx-Rx 안테나쌍 TDL
tap-set을 attempt마다 재드로우(시간 다이버시티), `rate_matching.c`의
범용 circular buffer 재사용, 두 레이어가 같은 재전송 occasion을
공유(구현 정의, 이 프로젝트 다른 다중스트림 HARQ 함수들과 동일).
CP-OFDM 전용(TS 38.211 §6.3.1.4, 기존 검증 재사용). **DL과 동일한
비대칭 의도적으로 유지**: DL SM_2X2 HARQ도 TDL 전용이고 flat+HARQ가
없으므로, UL도 굳이 새로 통합 설계(flat/TDL 하나의 함수)를 시도하지
않고 기존 DL 정확히 1:1로 포팅 — 검증된 설계를 그대로 재사용해 새
설계 리스크를 피함(flat+HARQ는 DL/UL 둘 다 후속 과제로 남김).

`main.c` PUSCH HARQ 분기에 SM_2X2+TDL 특수 케이스 추가,
`config_parser.c`의 PUSCH HARQ 화이트리스트를 SM_2X2+TDL까지 허용하도록
확장(기존엔 PUSCH+MIMO_MODE≠SISO+HARQ 전체를 무조건 차단했음).

**검증**: 수동 SNR 스윕(0~35dB, MCS15, MMSE, 400~500trial/pt) —
BLER(HARQ)이 BLER(1st) 대비 항상 개선, AvgTx 고SNR에서 하락(정상
HARQ 패턴). 25~35dB에서 BLER(HARQ)이 0.55~0.66 부근에서 잘 안
내려가는 현상을 발견했으나, **DL의 기존(무수정) SM_2X2 HARQ TDL
함수를 동일 조건(MCS15, 15~35dB)으로 직접 재실행해 정확히 같은
수치대의 정체 현상(0.49~0.66)을 확인** — 이번에 새로 만든 UL 코드의
결함이 아니라 이 프로젝트의 SM_2X2+TDL 검출 조합에 이미 있던 특성을
그대로(정확하게) 재현한 것임을 확인. `regression_test.sh`에 positive
1건(PUSCH SM_2X2 HARQ TDL) 추가, 기존 negative test(flat+HARQ 차단)는
라벨만 명확화하고 그대로 유지, 66/66 통과(기존 65개 유지). `make
clean && make` 경고 없음.

flat+HARQ, K>2, 다중 Rx, 코드북 기반 UL(SM_4X4 상당)은 `tasks/todo.md`에
후속 과제로 유지.

### UL SIMO 수신 빔포밍 — 공간공분산 EVD(전력반복법), 비-코드북 (2026-09-01)

사용자가 다음 UL MIMO 확장 논의 중 "rx는 코드북 말고 빔포머 써야지"로
설계 방향 정정 — gNB Rx측은 PMI 코드북이 아니라 실제 채널(통계)로
빔을 만드는 것이 맞다는 지적. 스코프 확인(UE 1 Tx→gNB N Rx Eigen-BF
vs UE 2 Tx(SM_2X2)→gNB N Rx Eigen-BF 검출) 결과 "UE 1 Tx→gNB N Rx,
Eigen-BF(DL EIGEN_16PORT와 대칭)" 선택.

**설계**: UE 1 안테나뿐이라 채널 h(16×1)는 항상 rank-1 — 잡음 없는
순간 채널 하나만 보면 "고유빔포밍"은 수학적으로 MRC와 완전히 동일
(h h^H의 유일한 0이 아닌 고유벡터가 h 자신). 그래서 순간 채널이 아니라
"여러 파일럿 관측(각각 잡음 포함)의 공간공분산 R=(1/M)Σy_m y_m^H"에서
지배적 고유벡터를 뽑는 방식으로 설계 — 잡음은 16차원 전체에 고르게
퍼지고 신호는 h 방향에만 실리므로, M개 관측 평균 공분산의 지배적
고유벡터는 순간 채널 추정치 하나보다 잡음에 강건하다(실무의 SRS 기반
공간공분산 추정과 동일한 개념). 16×16 Hermitian 행렬 전체 고유분해
대신 지배적 고유벡터 1개만 필요하므로 전력반복법(power iteration)
사용 — EIGEN_16PORT의 4×4 Jacobi EVD(전체 스펙트럼 필요)와 다른 접근.

**구현**: `ul_eigen_bf.c`/`.h` 신규 — `ul_eigen_channel_draw()`(i.i.d.
Rayleigh SIMO, UE 1 Tx→gNB 16 Rx), `ul_eigen_beamform()`(M개 잡음
파일럿 LS 관측 → 표본공분산 → 전력반복법 50회 → 단위벡터 고유빔).
**배선 전 독립 검증**(standalone 하네스): (1) 잡음 없음+M=1에서
|w^H h|/||h||=1.0000000000(정확한 MRC 일치, 부동소수점 오차조차
없음), (2) 상당한 잡음(N0=2.0)에서 M을 1→256으로 늘리면 정렬도가
0.528→0.852→0.964→0.989→0.996으로 단조 개선(공분산 평균화의 잡음
강건성 이득을 정량적으로 확인), (3) 출력이 항상 정확히 단위노름 —
모두 설계 의도와 정확히 일치.
`run_pusch_ul_eigen_bf_simulation()`(`pusch.c`) 신규 — Genie MRC(완전
채널지식)과 Eigen-BF(파일럿 공분산 기반)를 같은 트라이얼·같은 채널
실현에서 병렬 비교(빔 관리 함수의 Genie/P1 비교와 동일 철학). 두
경로 모두 단위노름 빔 w로 얻은 유효 채널 g=w^H·h(복소 스칼라)로
등가 스칼라 채널(ZF 등화)로 단순화 — MU-MIMO/빔관리와 동일 관례.
`main.c`에 `MIMO_MODE=UL_EIGEN_BF` 신규 분기(평탄 페이딩 전용),
`config_parser.c`에 UL_EIGEN_BF+TDL 조합 차단 검증 추가(전용 함수
없음, 같은 오배선 클래스 재도입 방지 원칙 재사용).

**검증**: 수동 SNR 스윕(-15~5dB, MCS10, 1000trial/pt) — GainLoss(dB)
(=20log10(|g_est|/|g_genie|))가 모든 SNR에서 정확히 음수(코시-슈바르츠
상한과 일치, EigenBF는 Genie를 절대 능가 못 함)이고 그 크기가 SNR이
오를수록 단조로 줄어듦(-3.98dB→-0.01dB) — "파일럿이 덜 잡음낄수록
Eigen-BF가 Genie MRC에 수렴한다"는 설계 의도가 정확히 실측으로 확인됨,
두 경로 BLER 모두 SNR에 단조 감소. 회귀에 positive 1건+negative 1건
추가, 68/68 통과(기존 66개 유지). `make clean && make` 경고 없음.

TDL/HARQ, UE 2 Tx(SM_2X2)측 Eigen-BF 검출로의 확장은 `tasks/todo.md`에
후속 과제로 등록.

### UL 2계층(SM_2X2) 수신 빔포밍 — Genie SVD vs Eigen-BF (2026-09-01)

사용자가 "UE 2 Tx 쪽 진행하자"로 바로 이어진 후속 확장. UE가 2개
레이어로 동시 전송하면 채널 H(16×2)가 더 이상 rank-1이 아니므로
진짜 SVD가 필요해지는 지점.

**설계**: Genie 경로는 완전한 채널 지식으로 H의 좌특이벡터 U(16×2,
직교정규)를 정확히 계산 — 16×16 전체 대신 2×2 Gram 행렬(H^H H)의
**닫힌 형식**(2×2 Hermitian은 반복법 없이 직접 공식으로 풀림) 고유분해로
경량화. U는 H의 신호 부분공간을 정확히 張하므로 16차원 수신신호를
U로 2차원에 투영해도 신호 손실이 전혀 없음(에너지 없는 14차원을
버리는 것과 동치) — 이후 2×2 ZF/MMSE와 결합하면 완전한 채널지식
기준 최적 선형 수신기와 수학적으로 동치.
Eigen-BF(현실) 경로는 레이어마다 독립적으로 FDM 파일럿 관측 후
기존 `ul_eigen_beamform()`(1-Tx 버전에서 만든 전력반복법 함수)을
레이어별로 2회 호출해 w_A, w_B를 얻고, 신규 `ul_eigen_orthogonalize2()`
로 그람-슈미트 직교화해 Q=[w_A,w_B_perp] 구성 — 직교화가 필요한 이유:
직교화하지 않으면 결합 후 잡음 공분산이 N0·I가 아니게 되어 기존 2×2
검출기(mimo_zf_detect/mimo_mmse_detect)의 백색잡음 가정이 깨짐.

**구현**: `ul_eigen_bf.c`/`.h`에 `ul_eigen_svd_genie()`(2×2 Gram
닫힌형식 고유분해+좌특이벡터), `ul_eigen_orthogonalize2()`(그람-슈미트)
추가. **배선 전 독립 검증**(standalone 하네스, 500trial): U^H U와
I의 최대 오차 8.5e-15(부동소수점 잡음 수준, 완전한 직교정규 확인),
H와 U(U^H H) 재구성 오차 최대 2.4e-13(U가 H의 열공간을 정확히 張함을
확인), 직교화 후 |w1^H w2|=5.4e-17·‖w2‖²=1.0000000000(완벽한 직교
+ 단위노름). `run_pusch_ul_eigen_bf_2tx_simulation()`(`pusch.c`) 신규
— 두 경로 모두 2×2 유효채널(H_eff=combiner^H·H)로 등가 2차원 수신신호를
합성해 **기존 검증된 `mimo_zf_detect`/`mimo_mmse_detect`를 그대로
재사용**(신규 검출 코드 없이 위험 최소화). `MIMO_MODE=UL_EIGEN_BF_2TX`
신규 분기, TDL 조합은 전용 함수 없어 차단(1-Tx 버전과 동일 원칙).
랭크 적응 없이 항상 rank=2 고정(구현 정의 스코프 축소 — EIGEN_16PORT
DL의 랭크 적응까지는 이번 범위 밖).

**검증**: 수동 SNR 스윕(-10~10dB, MCS10, MMSE, 1000trial/pt) — 두
경로 BLER 모두 SNR에 단조 감소(1.0→0.0), EigenBF가 저~중SNR에서
Genie보다 살짝 나쁘고(예: -10dB BLER 동일하게 1.0이지만 BER
0.155(Genie) vs 0.195(EigenBF)) 고SNR(7.5~10dB)에서 거의 수렴 —
1-Tx 버전과 동일한 정성적 패턴("파일럿이 덜 잡음낄수록 EigenBF가
Genie에 근접"). 회귀에 positive 1건+negative 1건 추가, 70/70 통과
(기존 68개 유지). `make clean && make` 경고 없음.

TDL/HARQ, 랭크 적응, K>2로의 확장은 `tasks/todo.md`에 후속 과제로 등록.

### UL Eigen-BF(1-Tx)를 TDL/HARQ로 확장 — 채널 모델 설계 결함 발견·수정 (2026-09-01)

사용자가 "적은 순서대로 진행해"로 목록 첫 항목(UL Eigen-BF 1/2-Tx
TDL/HARQ 확장) 착수 지시.

**배선 전 검증에서 발견·수정한 설계 결함(가장 중요한 부분)**: 처음에는
다른 massive-MIMO TDL 함수들(SM_2X2/MU-MIMO)과 동일하게 gNB 16개 Rx
안테나 각각에 독립 TDL tap-set을 부여해 구현·수동 실행했더니, Eigen-BF
경로가 SNR을 올려도 Genie MRC에 전혀 수렴하지 못하는 현상 발견(플랫
버전에서는 SNR이 오를수록 격차가 0에 수렴했던 것과 대조적). 표면
결과만 보고 넘어가지 않고 standalone 하네스로 `tdl_freq_response()`
의 RE 간 상관을 직접 찍어봄(`tasks/lessons.md` 교훈 적용) — 파일럿이
걸쳐 있는 120RE 대역 안에서 이미 크게 감쇠·위상회전이 일어남을 확인,
즉 coherence bandwidth를 훨씬 초과한 폭에 파일럿이 퍼져 있었음. **원인
분석**: 안테나마다 독립으로 TDL이 걸리면 각 파일럿 RE에서 관측되는
"방향"이 사실상 무작위가 되어(고정된 공간 시그니처 자체가 없음)
wideband 공분산의 지배적 고유벡터가 수렴할 대상이 없어짐. SM_2X2/
MU-MIMO의 "안테나쌍마다 독립 TDL"은 서로 다른 Tx-Rx 링크가 서로 다른
산란체를 겪는 "풍부한 다중경로 다이버시티" 시나리오에는 맞는 모델이지,
"고정된 UE 방향에서 오는 지배경로가 gNB 배열 전체를 코히런트하게
비추는" 빔포밍 시나리오에는 안 맞음(빔 관리 함수를 만들 때 이미 한 번
정리했던 구분인데 이번에 다시 놓쳤던 것) — 올바른 모델은 빔 관리 TDL과
정확히 같은 "배열 전체가 공유하는 클러스터 주파수선택적 게인 × 고정
공간 시그니처"였음. 채널 모델을 이렇게 교체한 뒤 재실행하니 Eigen-BF가
전 SNR에서 Genie와 거의 일치(예: 5dB에서 BER 0.0160 vs 0.0163)하는
정상적인 결과로 회복됨.

**구현**: `run_pusch_ul_eigen_bf_tdl_simulation()`(공유 SISO 클러스터
게인 모델, 실제 DMRS/데이터 RE 그리드 사용) 신규. `run_pusch_ul_eigen_bf_harq_simulation()`
(flat/TDL 둘 다 하나의 함수, 빔 추정은 트라이얼당 1회만 attempt 0의
채널 상태로 수행해 이후 모든 attempt에서 재사용 — 빔 관리 HARQ와
동일 철학, TDL이면 클러스터 게인만 attempt마다 재드로우해 시간
다이버시티 모사, Genie는 매 attempt 정확히 추적하지만 Eigen-BF는
attempt 0에서 고정한 빔을 계속 써서 "낡은 빔" 시나리오가 자연스럽게
나타남) 신규. `main.c`/`config_parser.c`에 배선(UL_EIGEN_BF는 이제
flat/TDL 둘 다 HARQ 화이트리스트에 무조건 포함 — SM_2X2와 달리 TDL
제한 없음).

**검증**: 수동 SNR 스윕(HARQ flat -15~0dB, TDL -10~5dB, MCS15,
500trial/pt) — Genie/Eigen-BF 두 경로 모두 BLER(HARQ)이 BLER(1st) 대비
크게 개선(예: flat 0dB에서 0.988→0.044(Genie)/0.054(EigenBF)), AvgTx
고SNR에서 하락, TDL이 flat 대비 항상 열화 — 전부 기존 HARQ 함수들과
동일한 물리적으로 타당한 패턴. 회귀에 positive 3건(TDL 1건+HARQ
flat/TDL 2건) 추가, 72/72 통과(기존 70개 유지). `make clean && make`
경고 없음.

2-Tx(SM_2X2 대응)의 TDL/HARQ 확장은 이번에 발견한 "공유 클러스터
게인" 모델을 그대로 적용하면 될 것으로 판단 — 이어서 진행.

### UL Eigen-BF(2-Tx)를 TDL/HARQ로 확장 (2026-09-01)

1-Tx에서 발견한 교훈("공유 클러스터 게인 × 고정 공간 시그니처")을 그대로
2-Tx(H는 16×2 행렬)에 적용. H가 트라이얼 내내 고정이고 모든 RE가 공유
스칼라 g(re)만 곱해지므로, Genie 좌특이벡터 U와 Eigen-BF 직교화 빔 Q는
RE와 무관하게 트라이얼당 1회만 계산하면 되고, 2×2 유효채널도
H_eff(re)=g(re)·(combiner^H·H) — "고정 2×2 베이스 행렬을 스칼라
g(re)로 스케일"하는 것으로 단순화됨(U는 H에만 의존해 HARQ에서도 매
attempt 정확히 유효, Q는 1-Tx와 동일하게 attempt 0에서만 추정 후 재사용).

**구현**: `run_pusch_ul_eigen_bf_2tx_tdl_simulation()`/`_harq_simulation()`
(`pusch.c`) 신규. `main.c`/`config_parser.c`에 배선(UL_EIGEN_BF_2TX도
1-Tx와 동일하게 flat/TDL 둘 다 HARQ 화이트리스트에 무조건 포함, 기존
TDL 차단 검증은 제거).

**검증**: 수동 SNR 스윕 — TDL(비-HARQ, -10~10dB, MCS10): Genie/EigenBF
BLER 둘 다 SNR에 단조 감소하며 거의 일치(1-Tx와 동일 패턴). HARQ(flat
-10~5dB, TDL -5~10dB, MCS15, 500trial/pt): 두 경로 모두 BLER(HARQ)이
BLER(1st) 대비 크게 개선, AvgTx 고SNR에서 하락, TDL이 flat 대비 항상
열화 — 전부 물리적으로 타당한 기존 패턴과 일치. 회귀에 positive 3건
(TDL 1건+HARQ flat/TDL 2건, 기존 negative test 1건은 이제 유효한
조합이 돼 제거) 추가, 74/74 통과(기존 72개 유지). `make clean && make`
경고 없음.

랭크 적응(현재 항상 rank=2 고정), K>2로의 확장은 `tasks/todo.md`에
후속 과제로 등록. 이로써 사용자가 지시한 "적은 순서대로 진행" 목록의
첫 항목(UL Eigen-BF 1/2-Tx TDL/HARQ 확장) 완료.

### UL Eigen-BF(2-Tx)에 랭크 적응 추가 (2026-09-01)

"적은 순서대로 진행" 목록 두 번째 항목. 기존 flat/TDL/HARQ 세 함수
모두 항상 rank=2로 고정돼 있던 것을 rank 1/2 적응으로 확장.

**설계**: Genie 특이값 σ1,σ2(및 N0)로 등력분배 기준 추정 용량
C1=log2(1+σ1²/N0) vs C2=Σ_k log2(1+(σk²/2)/N0)를 비교해 트라이얼당
1회(HARQ는 attempt 0 이전, 이후 모든 attempt에서 고정 — 실제 RI도
HARQ 버스트 도중 안 바뀌는 것과 동일 관례) rank를 결정. 실제
네트워크에서도 UL 랭크는 gNB가 SRS 등으로 결정하므로 Genie 채널지식
기준으로 두는 것이 타당하고(codebook_32port RI 선택과 동일한
"등력분배" 관례), Genie/Eigen-BF 두 경로가 같은 rank를 공유해
"트라이얼당 CW 개수가 경로마다 다르면 생기는" 복잡함을 피함.

rank=1일 때는 새 검출 코드를 만들지 않고, 이미 계산돼 있는 2×2
유효채널의 열 0(레이어 A)만 골라 기존 `mrc_combine()`(2-branch, 이미
검증된 함수)으로 검출 — Genie는 U가 H의 열공간을 정확히 張해 무손실이고,
Eigen-BF는 Q[:,0]=w_A가 직교화로 안 바뀌므로 w_A 단독 사용보다 손해가
없다(오히려 w_B_perp 방향으로 샌 레이어 A 에너지까지 회수해 이론상
약간 더 좋음).

**구현**: `ul_eigen_svd_genie()`가 특이값 `sigma[2]`도 함께 반환하도록
시그니처 확장(기존 3개 호출처 모두 갱신), `run_pusch_ul_eigen_bf_2tx_simulation()`/
`_tdl_simulation()`/`_harq_simulation()`(`pusch.c`) 세 함수 모두에
랭크 적응 로직 추가, 출력에 AvgRank 컬럼 추가.

**검증**: 수동 SNR 스윕(flat/TDL 둘 다 -15~15dB, MCS10, 1000trial/pt) —
AvgRank가 저SNR에서 1.0 근처(1.08~1.09)로 시작해 SNR이 오를수록
매끄럽게 상승, -5dB 이상에서 정확히 2.00으로 수렴(항상 rank2 선택) —
등력분배 기준 이론과 정확히 일치하는 물리적으로 타당한 전이. HARQ
(flat, MCS15, 500trial/pt)에서도 동일한 rank 전이 패턴 재현, BLER(HARQ)
개선·AvgTx 감소 등 기존 HARQ 패턴도 그대로 유지됨을 확인. 새 config
표면이 추가되지 않아(기존 MIMO_MODE=UL_EIGEN_BF_2TX 자체에 랭크 적응이
내장됨) 회귀 74/74 그대로 통과(신규 케이스 불필요, 기존 케이스가
자연스럽게 랭크 적응 경로를 태움). `make clean && make` 경고 없음.

K>2(3/4 레이어)로의 확장은 `tasks/todo.md`에 후속 과제로 등록.

### UL Eigen-BF를 K=4(최대 4레이어)로 확장 (2026-09-02)

`tasks/todo.md`의 "UL Eigen-BF 2-Tx를 K>2(3/4 레이어)로 확장" 항목.
2-Tx 설계(닫힌 형식 2×2 SVD + rank 1/2 적응)를 4×4로 일반화 — UE
최대 4 Tx(레이어), gNB 여전히 16 Rx, rank는 1~4 사이에서 적응 선택
(3레이어도 이 적응의 한 결과로 자연히 커버 — DL EIGEN_16PORT가 이미
같은 방식으로 rank 1~4를 다루는 것과 동일한 선례).

**설계**: 2×2 Gram 행렬은 닫힌 형식으로 풀렸지만 4×4는 일반적으로
반복법이 필요 — DL EIGEN_16PORT가 이미 검증한 Cyclic Jacobi 4×4
Hermitian 고유분해를 재사용하기 위해, 원래 `eigen_16port.c`의 static
함수였던 것을 `utils.h`/`utils.c`의 `herm4x4_eig()`로 추출해 두
모듈이 공유하도록 리팩터링(2026-09-02, 검증된 기존 알고리즘 그대로
이동 — 로직 변경 없음, `eigen_16port.c`는 이제 이 공용 함수를
호출). `ul_eigen_bf.c`에 `ul_eigen_svd_genie4()`(4×4 Gram 행렬
고유분해 → 좌특이벡터 U(16×4)와 특이값 sigma[4])와
`ul_eigen_orthogonalize4()`(w[0..3] 순차 그람-슈미트 직교화,
`ul_eigen_orthogonalize2()`의 K=4 일반화) 추가. 랭크 적응은 새 함수를
만들지 않고 DL EIGEN_16PORT의 기존 `eigen_bf_16port_select_rank()`를
그대로 재사용(인터페이스가 이미 등력분배 기준 (sigma[4],N0,*rank)로
동일).

검출은 CL_32PORT가 이미 rank-adaptive 4Rx 시나리오에서 검증해 둔
검출기 계열(`mrc_combine_4rx`/`mimo_mmse_detect_4rx2`/`_4rx3`/
`mimo_mmse_detect_4x4`)을 `ul_eigen4_detect_re()`(pusch.c 신규 정적
헬퍼, flat/TDL/HARQ 세 함수가 공유)로 그대로 재사용 — 신규 검출
코드 없음. CL_32PORT 선례를 따라 등화기 선택 없이 MMSE 고정(ZF는
rank<4 사각 시스템에 대해 이 프로젝트에 미구현, rank=1은 MRC가
이미 최적이라 등화기 선택 자체가 무의미).

**구현**: `run_pusch_ul_eigen_bf_4tx_simulation()`/`_tdl_simulation()`/
`_harq_simulation()`(pusch.c) 3개 신규 함수, 각각 2-Tx 대응 함수의
구조(파일럿 관측→빔 추정→직교화→4×4 유효채널→rank개 CW 인코딩→
검출→LDPC 복호)를 그대로 K=4로 확장. 파일럿 관측 수는 레이어당
`(6*num_rb)/4`(정수 나눗셈, 1/2-Tx의 "6*num_rb를 등분" 관례를 4등분으로
확장 — num_rb가 4의 배수가 아니면 소수점 이하 관측 몇 개가 잘려나가는
근사, 물리적 의미 없는 synthetic 파일럿 예산이라 문제 없음). TDL
파일럿 RE는 4-way 인터리브(`pilot_pos[4*i+l]`)로 레이어별 분리(1/2-Tx의
2-way 인터리브 관례를 확장). `MIMO_MODE=UL_EIGEN_BF_4TX` 신규 —
`main.c`(flat/TDL/HARQ 배선), `config_parser.c`(PUSCH MIMO×HARQ
화이트리스트), `pusch.h`에 각각 추가. main.c에서 2-Tx flat 호출부에
남아있던 stale 주석("랭크적응 미지원", 실제로는 이미 지원 완료 상태였음)
도 함께 정리.

**검증**: `ul_eigen_svd_genie4()`/`ul_eigen_orthogonalize4()`를 독립
하네스로 먼저 검증(랜덤 H(16×4)) — U 열 직교정규성 오차 ~9e-16,
‖H^H U_k‖=sigma_k 일치 오차 ~3.6e-15, 직교화4 직교정규성 오차 ~2e-16
(전부 배정밀도 한계 수준, 2-Tx 검증 때와 동일한 정밀도). 수동 SNR
스윕으로 AvgRank가 -25dB(1.00)→-20dB(1.03)→-15dB(1.50)→-10dB(2.18)→
-5dB(3.11)→0dB(3.96)→5dB 이상(4.00)으로 매끄럽게 전이함을 확인(2-Tx의
1→2 전이 패턴을 1→4로 자연스럽게 일반화한 형태). flat/TDL/HARQ(flat+TDL)
전부 Genie가 Eigen-BF보다 약간 우세, BLER(HARQ)가 BLER(1st) 대비
개선, AvgTx가 고SNR에서 감소, TDL이 flat 대비 항상 열화 — 전부 기존
1/2-Tx 패턴과 일치. 회귀에 4건 추가(flat/TDL/HARQ flat/HARQ TDL) —
78/78 통과(기존 74개 유지), `make clean && make` 경고 없음(기존 5건
무관 경고만 유지).

### LDPC BP decoder edge-message 교정 (2026-09-02)

`docs/analysis/phy_development_direction_validation.md`(같은 날 사용자
제공 외부 검토 문서) 검증 결과, P0-2로 지적된 결함을 실제 코드에서
확인하고 문서가 제안한 1단계 수정을 착수 승인받아 진행.

**결함**: `ldpc_decode_soft()`의 variable-node 갱신이 `v2c[nv]`(variable
개수만큼만 할당)에 전체 posterior(모든 연결 check의 기여분 합)를 저장하고,
check-node 갱신이 이 값을 그대로 재사용 — 목적 check 자신이 이전
반복에서 보낸 기여분을 빼지 않아 self-feedback을 일으키는 구조였음
(정상 BP는 `q_{v→c} = L_ch,v + Σ_{c'≠c} r_{c'→v}`로 목적 check를
제외해야 함). 추가로 `ldpc_decode_soft()`가 조건 없이 `return 0;`을
실행해 헤더 독스트링("Returns 0 on success")과 실제 동작이 불일치했고,
syndrome 기반 조기 종료도 없었음.

**수정**: variable-node 갱신에서 매 반복 전체 posterior `full_llr[nv]`를
먼저 계산한 뒤, check-major 순서로 `q_edge[H_nnz] = full_llr[H_col[p]] -
c2v[p]`(edge별 extrinsic, 목적 check 자신의 이전 기여분만 정확히 제외)를
구성해 check-node 갱신 입력으로 사용 — `c2v[H_nnz]`는 기존에도 이미
edge 단위로 정확히 인덱싱돼 있었으므로 check-node 갱신 자체는 무변경.
매 반복 후 hard-decision으로 syndrome(H·hard^T==0, 전 체크 XOR)을
검사해 만족하면 조기 종료하고 실제 성공 여부(0=수렴/1=미수렴)를
반환하도록 변경. `ldpc.h`의 `ldpc_decode()` 독스트링도 실제 반환
의미에 맞게 정정.

**검증**: (1) 프로젝트의 `build_H()`가 아닌 손으로 구성한 5변수/2체크
트리형 그래프(순환 없음, sum-product가 정확한 marginal에 수렴함이
이론적으로 보장됨)로 독립 검증 — 반복 1회차 posterior가 손계산 값과
bit-exact(오차 0) 일치, 수렴 후 posterior가 브루트포스 전수조사로 구한
정확한 marginal LLR과 ~1e-7 오차로 일치(부동소수점 한계 수준).
(주의: 이 손계산 예제의 LLR은 의도적으로 서로 모순되게 골라 정확한
marginal이 hard-decision 시 유효 codeword를 만들지 않도록 했음 —
sum-product가 marginal별로는 정확해도 개별 hard-decision이 joint
constraint를 자동으로 만족하진 않는다는 잘 알려진 성질이며 결함이
아님, 첫 시도에서 이 지점을 결함으로 오인했다가 브루트포스 marginal과
직접 대조해 재확인함.) (2) 프로젝트의 실제 `ldpc_init`/`ldpc_encode`로
K=8/64/400·rate=1/3~1/2 조합 noise-free round-trip 500+100+200회 —
인코더 자체 syndrome 항상 0, 디코더 100% 무오류·100% 수렴 확인.
(3) 호출부 전수조사(`grep`) — 68개 `run_*` 함수 중 반환값을 참조하는
곳 없음(전부 bare-statement 호출), 반환값 의미 변경이 기존 BLER 계산
로직에 영향 없음을 확인. (4) 동일 PDSCH+DMRS config(MCS10, 2~12dB,
500trial/pt)로 수정 전/후 BLER 비교 — 12dB에서 BLER 0.900→0.806,
BER 0.00547→0.00391로 일관되게(방향성 있게) 개선, 극적이진 않음(같은
비표준 surrogate 코드라 self-feedback 편향이 크지 않은 구조였던 것으로
보임 — 진짜 큰 수치 변화는 검토 문서가 예상한 대로 P0-1(NR mother
code 이식) 단계에서 있을 것). 회귀 78/78 그대로 통과, `make clean &&
make` 경고 없음(기존 5건 무관 경고만 유지).

`docs/analysis/phy_development_direction_validation.md` Section
10.5~10.9에 정리된 1단계 계획(파일 범위, 검증 방법, 커밋 분할)을 그대로
따름. P0-1(NR LDPC 이식)/P0-3(표준 rate matching) 등 이후 단계는
사용자 승인 후 순차 진행.

### NR BG1/BG2 QC-LDPC 이식 — P0-1 완료 (2026-09-02)

`docs/analysis/phy_development_direction_validation.md`의 P0-1 재검증
(파일·라인 근거 확인, EnterPlanMode로 상세 설계 후 사용자 승인) 후
착수·완료. P0-2(BP decoder edge-message 교정)에 이은 검증 문서
2단계 항목.

**설계 검토 중 발견한 두 가지 부수 이슈(둘 다 사용자 확인 후 이번
범위에 포함)**:
1. 기존 53개 이상의 `ldpc_init` 호출부 중 ~40곳(직접 호출, rate
   matching 없이 `coded_size`를 그대로 QAM에 먹임)은 실제 NR LDPC로
   바뀌면 `coded_size`가 mother-code 길이(BG1≈1/3, BG2≈1/5)가 돼
   2~3배 커지고, BLER이 MCS 목표율이 아닌 mother-code 성능을 반영하게
   됨 — 표준 rate matching(P0-3)이 나올 때까지 이 상태 유지하기로 확정.
2. ~14곳의 HARQ mother-rate 호출부가 `ldpc_init`에 실제 목표율 대신
   `HARQ_MOTHER_RATE`(1/3)를 넘겨 TS 38.212 §6.2.2 BG 선택 기준과
   어긋날 수 있는 잠재 버그 발견 — 별도 커밋으로 같이 수정하기로 확정.

**스펙 사실 확정**: 3gpp-server MCP로 TS 38.212 v18.8.0 §5.3.2 원문을
직접 읽어 가장 위험했던 미확정 항목(circulant shift 방향)을 확인 —
"단위행렬 I를 오른쪽으로 P번 순환시프트"이므로 `lifted_col =
col*Zc + (k+shift)%Zc`(덧셈 방향)가 맞음. BG 선택 규칙(§6.2.2),
`Kb`/`Zc` 선택 규칙(§5.2.2), 표 크기(BG1 46×68·316개 entry×8
lifting-set, BG2 42×52·197개×8)는 별도 spec-lookup 서브에이전트로
확정, Zc 값 개수는 51개(52 아님, 8개 set 직접 합산으로 확정 — 최초
조사의 52는 오류였음).

**표 전사**: ~4100개 shift 정수를 손으로 옮기지 않고, 로컬 스펙 원본
(`3gpp/38212-hc0/38212-hc0.docx`)의 `word/document.xml`을
zipfile+regex로 직접 파싱해 표 5.3.2-1/5.3.2-2/5.3.2-3 전체를
프로그램적으로 추출(手전사 오타 위험 원천 제거, 사용자 결정: 외부
코드 fetch 없이 로컬 스펙 추출 + 자체 속성 검증만 사용). 추출 직후
구조적 자가검증: BG1 316개/BG2 197개 entry 정확히 일치, 두 표 모두
중복 (row,col) 0건, 행/열 범위 전체 커버 — 그리고 **가장 중요한
발견**: parity 열 중 `base_info_cols+4` 이상 인덱스는 (BG1/BG2 둘 다)
전부 차수 정확히 1, 8개 lifting-set 전부에서 shift=0(리프팅 후 순수
대각 항등 구조), base-graph 행 0-3은 그 열들을 전혀 참조하지 않음 —
문헌의 Richardson-Urbanke 매직 인덱스를 암기해 하드코딩하는 대신
**실제 표에서 이 구조를 실측 확인**해 인코딩 알고리즘을 설계.

**구현**: `PHY/include/ldpc_tables.h`+`PHY/src/ldpc_tables.c`(순수
데이터), `PHY/include/ldpc_nr.h`+`PHY/src/ldpc_nr.c`(`nr_select_bg_zc()`
BG/`Zc`/`Kb`/filler 선택, `build_H_nr()`가 기존 `H_row_ptr`/`H_col`/
`Ht_row_ptr`/`Ht_links` CSR 필드를 리프팅된 행렬로 채워 P0-2에서 이미
검증된 BP 디코더를 알고리즘 변경 없이 재사용, `ldpc_encode_prepare_nr()`
가 4×`Zc`(최대 1536) 크기 "core" 시스템의 GF(2) 역행렬을
Gauss-Jordan으로 1회만(bit-packed, `ldpc_init`당 1회, 트라이얼마다
아님) 계산해 캐싱, `ldpc_encode_nr()`은 이 캐시로 core를 풀고 나머지
parity는 대각 구조라 단순 XOR로 직접 풂). `LDPCCodec`(`ldpc.h`)에
`bg`/`Zc`/`Kb`/`base_rows`/`base_info_cols`/`base_cols`/`filler_size`/
`core_inv` 필드 추가(기존 `info_size`/`coded_size`만 외부에서 읽힘을
grep으로 재확인해 안전하게 확장). 최종 컷오버: `ldpc.c`의 `ldpc_init`/
`ldpc_encode`가 새 경로로 위임, `ldpc_decode_soft()`에 filler bit
위치 LLR 강제 로직만 추가(디코더 반복문 자체는 무변경), 구 modulo
기반 `build_H()` 완전 삭제. HARQ mother-rate 14개 호출부를 `cr`
전달로 수정, `printf("Mother Rate...")`가 이제 실제 달성 rate를
출력, 더 이상 안 쓰는 `HARQ_MOTHER_RATE` 매크로 제거(`rate_matching.h`
주석도 갱신 — LDPC는 이제 BG/lifting 구조가 있지만 rate matching
자체는 여전히 단순화 상태임을 명확히 함). 단일 코드블록만 지원
(`Kb·Zc<block_size`면 세그멘테이션 필요 상황이라 조용히 clamp하지
않고 진단 메시지 후 `exit(1)`).

**검증**(표 5개 함께 진행, 매 단계 독립 하네스로 확인 후 다음 단계):
(1) `nr_select_bg_zc()` — 회귀 설정값(numRB=51, MCS5/10) 및 BG 선택
경계(A=292/293, A=3824/3825, R=0.25/0.67)·8개 lifting-set 전부에서
`Kb*Zc>=block_size`/`filler>=0` 불변조건 확인, BG2 세그멘테이션
갭(A≈3824 근방 — 이 프로젝트가 어디서나 CRC24A 고정이라 스펙의
조건부 CRC16 분기와 달리 K'=A+24가 돼 생기는 좁은 경계, 및
K=8424,R=0.15) 둘 다 의도대로 `exit(1)` 확인. (2) `build_H_nr()` —
15개 설정에서 `H_nnz`가 이론값과 정확히 일치(최악 BG1/`Zc`=384:
121,344), 모든 리프팅된 행의 차수가 base row 차수와 일치, `H_col`
전부 유효 범위, `Ht_nnz`=`H_nnz`, H↔Ht 교차링크 100% 일치. (3)
`ldpc_encode_nr()` — 1145회 무작위 트라이얼에서 `H_lifted·coded^T=0`
syndrome 100% 성립(인코더와 독립적으로 검증), 시스템틱 비트 정확히
에코, filler 위치 100% 0. (4) 기존(무변경) BP 디코더로 noise-free
round-trip 100% 성공(1000회 이상), AWGN sweep에서 깨끗한 waterfall
곡선(BLER 1.0→0.0 급격한 전이, 저율 mother code에 부합하는 낮은
문턱 SNR) 확인. (5) 컷오버 후 전체 회귀 78/78 그대로 통과(PUCCH F3
Polar HARQ의 `rate_matching.c` 공유 경로 포함 전부 무사), `make
clean && make` 경고 없음(기존 5건 무관 경고만 유지). HARQ mother-rate
수정 후 실제 출력 확인(MCS10: Target Rate 0.3320 → Mother Rate
0.1477, BG2/`Zc`=56)으로 BG 선택이 실제 목표율 기준으로 정확히
동작함을 확인.

`docs/analysis/phy_development_direction_validation.md` Section 10에
전체 검증 세부 수치 기록. P0-2c(다중 코드블록 세그멘테이션)/P0-3(표준
BG 인지 rate matching)는 `tasks/todo.md`에 후속 과제로 등록, 착수 전
사용자 확인 필요.

### TS 38.212 §5.4.2.1 표준 rate matching 이식 — P0-3 완료 (2026-09-02)

P0-1(NR BG1/BG2 QC-LDPC) 직후 남아있던 검토 문서의 마지막 P0 항목.
"P0-3 진행해" 승인 후 EnterPlanMode로 범위 재조사 → 계획 승인 → 구현.

**범위 재산정**: 계획 단계에서 grep으로 재조사한 결과, LDPC를 쓰는
`run_pdsch_*`/`run_pusch_*` 51개 함수가 두 그룹으로 갈림을 확인—
(1) HARQ 14곳(pdsch.c 9 + pusch.c 5, 32개 실제 호출부)은 이미
`rate_matching.c`의 범용 circular buffer를 쓰고 있었지만 `k0(rv)`가
균등 1/4-버퍼 분할(TS 38.212 표준 아님)이었고, (2) 비-HARQ 37곳
(pdsch.c 25 + pusch.c 12)은 rate matching 자체가 전혀 없이 mother
codeword의 앞부분 `num_data*bps`비트만 truncate하는 방식이었음 —
P0-1로 mother codeword가 2~3배 커지면서 이 37곳의 BLER이 MCS
목표율이 아닌 mother-code 성능을 반영하게 된 원인이 바로 이것이었음
(P0-1 때 사용자가 승인한 임시 상태). 두 그룹 다 이번 범위에 포함해
한 번에 완료하기로 확정.

**스펙 사실 확정**: 3gpp-server MCP로 TS 38.212 v18.8.0 §5.4.2.1을
재조사 — bit-selection while-loop(순환버퍼, filler 위치 건너뜀,
`k=0,j=0; while k<E: if d[(k0+j)%Ncb]!=NULL: e[k]=d[...],k++; j++`)
는 원문 프로즈로 확인, Table 5.4.2.1-2의 `k0(rv,BG,Zc)` 수식 6개는
모두 이미지로 박혀 있어 `get_image`로 하나씩 직접 열어 확인(BG1:
rv1/2/3 = ⌊17·Ncb/(66Zc)⌋Zc / ⌊33·Ncb/(66Zc)⌋Zc / ⌊56·Ncb/(66Zc)⌋Zc,
rv0=0; BG2는 66→50·17/33/56→13/25/43) — 이전 spec-lookup
서브에이전트의 웹 교차검증 결과와 정확히 일치함을 원문 이미지로
재확인. Ncb=N(mother codeword 전체 길이, LBRM 미모델링 — 이
프로젝트가 상위계층 파라미터를 다루지 않으므로 LBRM 자체가 범위
밖) 가정도 스펙 원문(LBRM 없으면 Ncb=N)과 일치함을 확인.

**구현**: 신규 `PHY/include/nr_rate_matching.h`+`PHY/src/nr_rate_matching.c`
— `nr_ldpc_k0()`, `nr_ldpc_rate_match_select()`/`_combine()`(정수용,
filler 위치 건너뛰는 순환버퍼), `nr_ldpc_rate_match_select_soft()`
(double 버전, turbo 등화 전용 — 아래 참조). 기존 `rate_matching.c`
(PUCCH F3 Polar HARQ와 공유)는 전혀 건드리지 않음(Polar는 BG/`Zc`/
filler 개념이 없어 이 단순화가 계속 유효).

적용은 두 단계로 진행:
- **그룹 A(HARQ 14곳)**: `rate_match_select`/`rate_match_combine`
  호출을 `nr_ldpc_rate_match_select`/`nr_ldpc_rate_match_combine`로
  교체(k0 공식만 표준으로 바뀜, 나머지 구조 무변경) — Python으로
  32개 호출부(select 16 + combine 16) 일괄 치환.
- **그룹 B(비-HARQ 37곳)**: `run_pdsch_dmrs_simulation` 하나를 먼저
  완전히 손으로 패치·빌드·회귀·수동검증까지 마쳐 정확한 패턴을
  확정(TX: `nr_ldpc_rate_match_select(coded,acsz,...,rv=0,E,selbits)`
  →`qam_modulate(selbits,E,...)`, RX: `qam_demap_llr`→`memset(soft_
  buf,0,acsz*sizeof(double))`+`nr_ldpc_rate_match_combine(...)`→
  `ldpc_decode(&ldpc,soft_buf,...)`, `E=num_data*bps`로 통일, 기존
  `nd=min(nsym,num_data)` truncate 클램핑 전부 제거). 이 레퍼런스
  diff를 pdsch.c 담당/pusch.c 담당 fork 2개에 병렬로 위임(각자 24개/
  12개 함수를 직접 Read로 구조 파악 후 Edit, 중간중간 빌드 확인,
  최종 회귀까지 각자 수행) — SM_4X4/CL_XPORT/EIGEN_16PORT/MU_MIMO/
  UL_EIGEN_BF 등 레이어·rank 가변 함수는 레이어/경로별로 반복 적용.
  fork 완료 후 직접 통합 재검증, 그리고 fork가 구조 판단이 필요하다며
  건드리지 않고 남겨둔 2곳을 직접 처리: `run_pdsch_simulation`(RE
  그리드가 없는 가장 기초적인 legacy 벤치라 `E=A/cr`을 별도 유도,
  bps 배수로 올림해 심볼 정합), `run_pusch_tdl_turbo_simulation`
  (turbo iteration마다 posterior-llr extrinsic을 다음 iteration의
  a priori로 재활용하는데, rate matching 도입 후엔 "전송된 위치"가
  더 이상 0..llr_len-1 연속 구간이 아니게 돼 `nr_ldpc_rate_match_
  select_soft()`를 신규 추가해 acsz 전체 도메인의 extrinsic에서
  실제 전송된 E개 위치만 재선별하도록 재설계).

**검증**: `nr_ldpc_k0()` 손계산 값과 bit-exact 일치(BG1/BG2 각 rv0-3,
여러 `Zc`), select/combine round-trip(무작위 info→select→confident
LLR→combine→원래 값 정확히 복원, filler 위치는 항상 0 유지) 독립
하네스로 확인. 그룹 A 적용 후 HARQ 시뮬레이션 SNR 스윕에서 BLER(HARQ)
≤BLER(1st) 유지·AvgTx 단조 감소 등 기존 패턴 그대로 확인. 그룹 B
적용 후 `run_pdsch_dmrs_simulation`(BLER이 이제 MCS10 목표율에 맞는
더 높은 SNR 문턱에서 꺾임 — P0-1 이전 경향으로 복귀), MU_MIMO/
BEAM_MGMT/OLLA+SM_2X2(폐루프가 개루프 대비 MCS를 낮춰 목표 BLER
근처로 수렴) 등 스팟체크 전부 물리적으로 타당. `run_pdsch_simulation`
직접 검증(SNR -4~8dB에서 BLER 1.0→0.0033→0.0 매끄러운 워터폴).
`run_pusch_tdl_turbo_simulation`은 `PUSCH_TURBO_ITERS=1`일 때
BER/BLER이 path1(noDFE)과 소수점까지 정확히 일치(1.5370e-01=1.5370e-01)
하는 기존 correctness 불변조건이 재설계 후에도 그대로 성립함을
재확인 — turbo_iters=3에서도 3개 경로(noDFE/hardDFE/turbo) 모두 SNR
단조 감소, turbo가 항상 최선, hardDFE가 최악(오류전파)인 기존
패턴 유지. 전체 회귀 78/78 유지, `make clean && make` 경고 없음
(기존 5건 무관 경고만 유지).

`docs/analysis/phy_development_direction_validation.md` Section 10에
이어 검증 결과 기록. 이로써 검토 문서의 P0-1/P0-2/P0-3 전부 완료 —
남은 항목은 P0-2c(다중 코드블록 세그멘테이션)뿐, `tasks/todo.md`에
후속 과제로 등록, 착수 전 사용자 확인 필요.

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
