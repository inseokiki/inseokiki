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
- [ ] `SM_4X4`(4x4 SU-MIMO)에 TDL 변형 추가 — 현재 flat/AWGN 채널로만 동작 (SIMO_MRC/SM_2X2는 이미 TDL 버전 있음)
- [ ] `SM_4X4`에 HARQ 조합 추가 — 2x2는 HARQ+TDL 결합까지 있는데 4x4는 HARQ 자체가 없음
- [ ] `CL_4PORT`에 TDL 변형 추가 — 현재 flat/AWGN 채널로만 동작
- [ ] `CL_4PORT`에 HARQ 조합 추가
- [ ] PUSCH에 HARQ 재전송 조합 추가 — DFE/turbo 등화 연구 프로토타입은 있지만 HARQ 재전송 자체가 없음
- [ ] PBCH/PDCCH에 페이딩 채널(flat/TDL) 변형 추가 — 현재 AWGN 전용 (CSI-RS/SRS는 최소 flat fading은 지원)

### B. 이번 세션에 확인된 후속 과제
- [ ] CL_4PORT 교차편파(XPD) 누설 상관 모델 추가 — 2026-08-02 검증에서 동일편파 상관만으로는 rank-1이 ρ→1에서도 거의 선택 안 됨을 확인(교차편파 다이버시티 변형이 계속 유리하기 때문, 물리적으로 타당). 진짜 rank-1 전환을 보려면 편파 간 누설(유한 XPD) 상관을 추가해야 함 — `docs/analysis/history.md` 2026-08-02 항목 참조
- [ ] UL CLPC에 채널 페이딩/이동성(시변 PL) 추가 — 현재 `run_ulpc_simulation()`은 고정 PL 기준(2026-07-22), 시변 경로손실 시나리오로 확장

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

_(최근 완료 이력은 `docs/analysis/history.md` 참조 — 2026-08-02 CLAUDE.md 분리 시점 이전 이력은 전부 그쪽으로 이관됨)_
