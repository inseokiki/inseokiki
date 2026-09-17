# 개발 이력 — 목차 (2026-05-24 ~)

> 상시 컨텍스트(프로필/답변원칙 등)는 `CLAUDE.md`, 현재/향후 작업은 `tasks/todo.md`, 교훈은 `tasks/lessons.md` 참조.
> 원본(2026-08-02 분리 이전, 2026-05-24~2026-07-22 누적) 전체는 `docs/analysis/CLAUDE.md.original-20260802.bak` 참조.
> 상세 이력은 아래 월별 파일로 분리되어 있다(2026-09-11 문서 정리). 이 파일은 목차와 최신 항목 링크만 유지한다.

---

## 월별 이력

| 파일 | 기간 | 주요 내용 |
|---|---|---|
| [`history/2026-07.md`](history/2026-07.md) | 2026-07-13 ~ 07-22 | DL/UL 기능 대거 확장(MIMO/HARQ/TDL/PUCCH/PRACH), SM_4X4, 4포트 코드북, CL_4PORT RI+PMI, ULPC |
| [`history/2026-08.md`](history/2026-08.md) | 2026-08-02 ~ 08-31 | CL_4PORT 공간상관/XPD, 원격 개발환경(Tailscale SSH), 8/32-port 코드북, EIGEN_16PORT, 채널추정 MMSE/DFT 확장 |
| [`history/2026-09.md`](history/2026-09.md) | 2026-09-01 ~ 09-11(진행 중) | OLLA/MU-MIMO/빔관리 신규 착수 및 TDL·HARQ·P1-P3-P2 확장, UL Eigen-BF, P0-1/P0-2c/P0-3(표준 LDPC+세그멘테이션+rate matching), Polar 표준 정합화 4단계, PHY 리뷰 기반 보강(PHY-01~07), `PHY_UNIT_VALIDATION_PLAN.md` §5 단위 테스트 확장 |

### 날짜 미상

`Massive MIMO UL Rx Beamforming — Eigen 기반 구현 완료` — 2026-05-24 원본 `CLAUDE.md`에 이미 완료 상태로 기록돼 있던 항목으로 정확한 완료일이 원문에 없다. 임의로 특정 월에 배치하지 않고 여기 별도로 남긴다: 업링크 수신 빔포밍에서 채널 공간 공분산 행렬 추정 → EVD(고유값 분해) → Eigenbeam 추출 → UL 수신 결합. 관련 표준: 3GPP TS 38.214 (UL MIMO), Rel-17 UL 공간 다중화.

---

## 최근 항목 (요약)

- **2026-09-18**: PUSCH UL 4포트 코드북 전체 TPMI(28/22/7/5) 및 TDL/HARQ 확장. 회귀 128/128, 수치 테스트 20/20. → [`history/2026-09.md`](history/2026-09.md) 최하단
- **2026-09-17**: PUSCH UL 4포트 코드북 flat MIMO — rank 1~4 TPMI 부분집합, 4Tx/4Rx, 단일 TB 레이어 매핑, 회귀 127/127, 수치 테스트 20/20. → [`history/2026-09.md`](history/2026-09.md) 최하단
- **2026-09-17**: PDSCH CL_32PORT OLLA 구현 — 고상관 2,000회/pass에서 BLER 0.9985→0.1075(목표 0.10), 회귀 123/123, 수치 테스트 19/19. → [`history/2026-09.md`](history/2026-09.md) 최하단
- **2026-09-17**: PDSCH CL_8PORT OLLA 구현 — 고상관 2,000회/pass에서 BLER 0.9590→0.1075(목표 0.10), 회귀 121/121, 수치 테스트 18/18. → [`history/2026-09.md`](history/2026-09.md) 최하단
- **2026-09-17**: 설정 입력·라우팅 검증 및 회귀 스크립트 거짓 통과 수정 — 회귀 119/119, 수치 테스트 17/17. → [`history/2026-09.md`](history/2026-09.md) 최하단
- **2026-09-11**: `PHY_UNIT_VALIDATION_PLAN.md` §5 3단계 "코딩·rate matching"(`test_ldpc.c`/`test_polar.c` 확장) + "추정·검출"(`test_channel_estimation.c`/`test_mimo_detection.c` 신규) — `run_numeric_tests.sh` 12/12, ASan/UBSan 클린. → [`history/2026-09.md`](history/2026-09.md) 최하단
- **2026-09-10**: `PHY_REVIEW_2026-09-10.md` 기반 PHY-01~06 보강(PUCCH 설정-실행 불일치, 빔관리 비교기준 재명명, 수치 하네스 영구화 6건 결함 수정, SEED 재현성, 문서 동기화) + §5 2단계 단위테스트(CRC/QAM/OFDM/DFT, 행렬/공간상관) + 빔관리 P1-P3-P2를 TDL/HARQ로 결합. → [`history/2026-09.md`](history/2026-09.md)
- **2026-09-03~09-09**: P0-2c(다중 코드블록 세그멘테이션, 50개 함수 전체 확장), Polar 표준 정합화 Phase 1-4(CA-SCL), 빔관리 P1→P3→P2, OLLA SM_4X4, PUSCH UL SM_2X2 flat+HARQ, MU-MIMO K=2→4. → [`history/2026-09.md`](history/2026-09.md)
- **2026-09-01~09-02**: OLLA/MU-MIMO/빔관리 신규 착수, UL Eigen-BF(1/2/4-Tx, TDL/HARQ/랭크적응), LDPC BP 디코더 edge-message 교정, NR BG1/BG2 QC-LDPC(P0-1), 표준 rate matching(P0-3). → [`history/2026-09.md`](history/2026-09.md)

현재 진행 중인 작업은 `tasks/todo.md` 참조.

---

## 🔄 원본 CLAUDE.md 업데이트 이력 (2026-08-02 분리 이전, 참고용)

> 2026-08-02 분리 당시 원본 `CLAUDE.md`에서 그대로 이관된 압축 변경 로그. 위 월별 파일의 서술형 항목과 내용이 겹치되(특히 07-13~08-03), 2026-05-24 최초 작성 관련 3건은 다른 곳에 없는 유일한 기록이라 보존한다.

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
| 2026-08-02 | `phy_lab/personal/`을 기준본으로 신규 작성 — 기존 `CLAUDE.md`(2026-05-24~2026-07-15 누적본)를 상시 컨텍스트/이력/작업/교훈으로 분리. 원본 백업: `docs/analysis/CLAUDE.md.original-20260802.bak` |
| 2026-08-02 | git 히스토리 재조정 — 분리 작업(위 항목) 커밋 전 `origin/develop`이 7커밋(4x4 MIMO/CSI-RS 코드북/RI+PMI/ULPC, 7/21~7/24) 앞서 있던 것을 발견, stash→fast-forward pull→CLAUDE.md 수동 병합으로 정리. 원격에만 있던 07-21~07-22 항목은 이 문서로 이관 |
| 2026-08-02 | PHY LLS: CL_4PORT 공간상관(Kronecker, XPOL 2x2 블록) 추가 — 동일편파 상관만으로는 ρ→1에서도 rank-1이 거의 선택 안 됨을 확인(교차편파 다이버시티 변형이 계속 유리, 물리적으로 타당) |
| 2026-08-02 | 방향 확인(사용자 명시): NTN보다 기존 LLS 완성도(채널×기능 조합 공백 메우기) 우선 — `tasks/todo.md` A/B/C 그룹으로 재정리 |
| 2026-08-02 | PHY LLS: PBCH/PDCCH에 FLAT_FADING/TDL 추가(genie-aided CSI) — 완성도 작업 1단계, AWGN 전용이던 마지막 두 채널 해소. 구현 중 `qam_demap_llr_mmse()` 오용(mmse_equalize 선행 누락으로 SNR 무관 BLER floor) 발견·수정 |
| 2026-08-27 | PHY LLS: CL_4PORT XPD 누설 상관(`SPATIAL_CORR_XPOL`) + UL CLPC 시변 PL(`UL_PC_PL_VAR_*`) 추가. 검증 중 `codebook.c` RI/PMI 선택기의 2×2 Gramian 부동소수점 결함과 rank-1/rank-2 코드북 전력 정규화 불일치(rank-2가 +3dB 전력 우위) 2건 발견 — 둘 다 사용자 확인 후 수정 완료. 재검증 결과 R1선택률이 SNR/상관도에 따라 물리적으로 타당하게 동작함(완전 rank-1 채널에서 R1선택률=100% 확인) |
| 2026-09-03 | `lab/HARNESS_ANALYSIS.md` L-02 반영 — `CLAUDE.md`에 "검증(회귀 테스트) 범위 원칙" 섹션 신규 추가. 국소 수정마다 `regression_test.sh` 전체(당시 87개)를 매번 도는 대신 금일 변경 관련 case만 먼저 도는 targeted를 기본값으로, 전체 회귀는 사용자 명시 요청/merge·release 전으로 한정 |
