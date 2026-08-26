# Todo (5G NR PHY 개인 프로젝트)

> `CLAUDE.md`/`AGENTS.md`의 "작업 기록 원칙"에 따른 세션별 작업 계획/진행/결과 기록 파일.
> 새 작업 시작 시: 아래에 체크 가능한 항목으로 계획 작성 → 진행하며 체크 → 완료 후 "완료" 섹션에 요약 추가.
> 완료된 기능의 상세 구현/실측 결과는 여기 남기지 않고 `docs/analysis/history.md`에 기록한다.

---

## 진행 중

_(현재 진행 중인 작업 없음)_

---

## 다음 후보

> 우선순위 원칙(2026-08-02, 사용자 확인): NTN 같은 새 영역 확장보다 **기존 LLS의 완성도**(이미 있는 채널/기능들이 빠짐없이 조합되어 동작하는 것)를 우선한다.

### A. 조합 공백 메우기 (기존 기능이 서로 안 엮이는 부분)
- [x] `SM_4X4`(4x4 SU-MIMO)에 TDL 변형 추가 — genie-aided CSI, 2026-08-02 완료
- [x] `SM_4X4`에 HARQ 조합 추가 — flat/TDL 모두 지원, genie-aided, 2026-08-03 완료
- [x] `CL_4PORT`에 TDL 변형 추가 — Wideband PMI (H_avg 기반), genie-aided, 2026-08-03 완료
- [x] `CL_4PORT`에 HARQ 조합 추가 — flat/TDL 모두 지원, 시도0 PMI 고정, 2026-08-03 완료
- [x] PUSCH에 HARQ 재전송 조합 추가 — TDL/flat/AWGN 지원, LS est., 2026-08-03 완료

### B. 이번 세션에 확인된 후속 과제
- [x] CL_4PORT 교차편파(XPD) 누설 상관 모델 추가 — `SPATIAL_CORR_XPOL`, Kronecker R_pol⊗R_ant 확장, 2026-08-27 완료. 부작용으로 `codebook.c` RI/PMI 선택기의 기존 버그(2건) 발견 — 상세는 아래 신규 항목과 `docs/analysis/history.md` 참조
- [x] UL CLPC에 채널 페이딩/이동성(시변 PL) 추가 — `UL_PC_PL_VAR_STD_DB`/`UL_PC_PL_VAR_CORR`, Gauss-Markov(AR1), 2026-08-27 완료
- [ ] **(신규, 사용자 확인 필요)** CL_4PORT rank-1/rank-2 코드북 전력 정규화 불일치 — `codebook_type1_sp_4port_rank1()`/`rank2()` 둘 다 컬럼당 `norm=0.5`(‖열‖²=1)를 써서 rank-2 총 송신전력(2)이 rank-1(1)의 2배(+3dB)가 됨. RI 선택기가 이 불공정한 전력 우위 때문에 rank-2를 구조적으로 선호할 가능성 — XPD 누설 상관을 0.9999+까지 올려 채널이 사실상 완전 rank-1이 되는 극단 케이스에서도 rank-1 선택률이 0%로 남는 것으로 발견(2026-08-27). CL_4PORT를 쓰는 기존 시뮬레이션 전체(BLER 실측치, "R1선택률=0%" 2026-08-02 결론 포함)에 영향을 줄 수 있는 근본적인 수정이라 사용자 확인 후 착수
- [x] `codebook.c` RI/PMI 선택기 2×2 Gramian 역산의 catastrophic cancellation 방어 — `det`가 이론상 항상 `>= N0²`로 양수인데 부동소수점 뺄셈 오차로 음수/근사영이 될 수 있어 `a0/a1`이 허수적으로 1 근처까지 치솟는 결함을 발견·수정(2026-08-27, 상기 XPD 극단값 검증 중 발견). 회귀 48/48 통과, 위 정규화 이슈와는 별개

### C. 새 영역 확장 (완성도 작업보다 낮은 우선순위)
- [ ] MU-MIMO 확장 — 현재 SU-MIMO(SM_2X2/SM_4X4)까지만 구현
- [ ] OLLA (Outer Loop Link Adaptation) — 현재 CQI→MCS 매핑만 있고 ACK/NACK 이력 기반 폐루프 조정 없음
- [ ] 빔 관리 (SSB/CSI-RS 기반 P1/P2/P3 빔 스위핑) — CSI-RS 채널추정은 있지만 빔 스위핑 절차 자체는 없음
- [ ] CSI 보고 확장 — 현재 Type I SP 4포트 코드북만 있고 Type II/8포트 이상은 없음
- [ ] Massive MIMO (64T64R) — 현재 4x4가 최대

## 보류/결정 사항

- **NVIDIA Aerial SDK 연동**: 보류 (사용자 명시적 결정, 2026-07-14) — 연동하지 않기로 함
- **NTN (Non-Terrestrial Network)**: 보류 (사용자 명시적 결정, 2026-08-02) — 기존 LLS 완성도를 우선하기로 함, 새 영역 확장은 나중

---

## 완료

_(상세 구현/실측 결과는 `docs/analysis/history.md` 참조 — 2026-08-02 CLAUDE.md 분리 시점 이전 이력은 전부 그쪽으로 이관됨)_

- [x] PBCH/PDCCH에 페이딩 채널(FLAT_FADING/TDL) 변형 추가 (2026-08-02) — LLS 완성도 작업 1단계, 상세는 `docs/analysis/history.md` 참조
- [x] Group A 조합 공백 전체 완료 (2026-08-03) — SM_4X4+TDL, SM_4X4+HARQ, CL_4PORT+TDL, CL_4PORT+HARQ, PUSCH+HARQ
- [x] P0 정합성 수정 완료 (2026-08-03) — TABLE3 fallback 제거, config validation 추가, STRUCTURE.md 라우팅 갱신, 48-case 회귀 테스트 스크립트(`PHY/regression_test.sh`) 신규 작성·전체 통과
- [x] Mac → Tailscale → Windows 4060 PC의 WSL2 원격 개발 경로 구축 (2026-08-03) — WSL OpenSSH 포트 `22299`, Tailscale 전용 연결, SSH 키 인증, Mac 별칭 `KANG_HOME`; 상세는 `docs/analysis/history.md` 참조
