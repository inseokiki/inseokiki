# Lessons (5G NR PHY 개인 프로젝트)

> `CLAUDE.md`/`AGENTS.md`의 "작업 기록 원칙"에 따른 교훈 기록 파일.
> 세션 시작 시 먼저 검토할 것 — 같은 실수를 반복하지 않기 위함.

---

## 교훈 목록

### 2026-07-15 — 작업 시작 전 git fetch/status 확인 누락으로 히스토리가 갈라진 사고
**무엇**: 다른 세션/기기에서 `PHY/common` + `PHY/lls_sim` + `PHY/ber_sim` 구조로 재구조화한 커밋들이 원격(`origin/develop`)에만 push되어 있었는데, 로컬에서 이를 모른 채 기존 평면 구조(`PHY/src`/`PHY/include`)로 한참 더 작업을 진행한 뒤 push 시점에야 뒤늦게 발견 — 히스토리가 크게 갈라짐.
**왜**: 작업 시작 시 `git fetch origin && git status`를 먼저 실행해 원격이 앞서 있는지 확인하는 절차가 없었음. 여러 세션/기기를 오가며 작업하는 구조상, 이 확인 없이는 로컬이 최신이라고 가정하는 것 자체가 위험함.
**적용**: 기능 자체(PUCCH/PRACH/PUSCH/MIMO/HARQ 등)는 로컬 WSL 작업이 최신이라고 판단해, 원격의 재구조화 히스토리는 `origin/archive/common-lls-sim-refactor` 브랜치로 보존하고 로컬 기준으로 `develop`을 force-push해 정리함. 이후 이 저장소의 `PHY/src`/`PHY/include` 평면 구조가 기준. 이 사고를 계기로 "작업 시작 시 항상 먼저 git fetch/status 확인" 규칙을 `CLAUDE.md`/`AGENTS.md`의 표준 절차로 고정함 — 매번 사용자가 요청하지 않아도 자동으로 수행할 것.

<!--
기록 형식 예시:

### YYYY-MM-DD — 짧은 제목
**무엇**: 어떤 실수/교정이 있었는지, 또는 어떤 접근이 맞다고 확인됐는지
**왜**: 사용자가 왜 그렇게 지적/확인했는지 (근거, 과거 사고 이력 등)
**적용**: 앞으로 언제/어떻게 이 교훈을 적용할지
-->
