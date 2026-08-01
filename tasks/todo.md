# Todo (5G NR PHY 개인 프로젝트)

> `CLAUDE.md`/`AGENTS.md`의 "작업 기록 원칙"에 따른 세션별 작업 계획/진행/결과 기록 파일.
> 새 작업 시작 시: 아래에 체크 가능한 항목으로 계획 작성 → 진행하며 체크 → 완료 후 "완료" 섹션에 요약 추가.
> 완료된 기능의 상세 구현/실측 결과는 여기 남기지 않고 `docs/analysis/history.md`에 기록한다.

---

## 진행 중

_(현재 진행 중인 작업 없음)_

---

## 다음 후보

- [ ] NTN (Non-Terrestrial Network) 작업 검토 — 3GPP Rel-17/18 NTN 표준(TS 38.821, TR 38.811) 기반, LEO/GEO/HAPS 시나리오, 대규모 Doppler 보상·긴 RTT·TA 확장, Feeder link/Service link 구조 분석
- [ ] 공간 상관 채널에서 CL_4PORT rank-1 선택 거동 검증 — iid Rayleigh에서는 R1선택률 0%로 확인됨(2026-07-22), 상관 채널에서 rank-1이 실제로 선택되는지 실측 필요
- [ ] UL CLPC에 채널 페이딩/이동성(시변 PL) 추가 — 현재 `run_ulpc_simulation()`은 고정 PL 기준(2026-07-22), 시변 경로손실 시나리오로 확장
- [ ] MU-MIMO 확장 — 현재 SU-MIMO(SM_2X2/SM_4X4)까지만 구현

## 보류/결정 사항

- **NVIDIA Aerial SDK 연동**: 보류 (사용자 명시적 결정, 2026-07-14) — 연동하지 않기로 함

---

## 완료

_(최근 완료 이력은 `docs/analysis/history.md` 참조 — 2026-08-02 CLAUDE.md 분리 시점 이전 이력은 전부 그쪽으로 이관됨)_
