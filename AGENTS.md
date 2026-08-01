# AGENTS.md — 5G NR PHY 개인 프로젝트 (Codex 진입 파일)

> 이 파일은 Codex가 이 프로젝트에서 세션 시작 시 참조하는 규칙 파일입니다.
> Claude Code용 진입 파일은 `CLAUDE.md` — 아래 원칙들은 두 파일 간 서로 모순되지 않도록 동일하게 유지합니다.
> 현재/향후 작업은 `tasks/todo.md`, 과거 실수와 교훈은 `tasks/lessons.md`, 상세 개발 이력은 `docs/analysis/history.md`, 코드 구조는 `STRUCTURE.md` 참조.

---

## 프로젝트 개요

| 항목 | 내용 |
|------|------|
| 전문 분야 | 5G NR PHY (Physical Layer), 기지국(gNB) PHY 개발 |
| 표준 기반 | 3GPP Release 17 / 18 / 19 (5G NR Advanced) |
| 프로젝트 성격 | PHY Link Level Simulator (LLS) — 개인 알고리즘 검증/프로토타이핑 |
| 주 언어 | C / C++ (PHY 알고리즘), Python(검증/분석), MATLAB(초기 설계) |

---

## Git 작업 시작 절차 (필수)

작업을 시작하기 전 **항상 먼저** 다음을 실행할 것:

```bash
git fetch origin
git status
git log --oneline develop..origin/develop
```

원격에 로컬에 없는 커밋이 있으면 바로 알리고, 병합/리베이스 방향을 사용자에게 먼저 확인한다 — 임의로 진행하지 않는다. 이 절차를 건너뛰어 히스토리가 갈라진 사고 이력이 있음 — 상세는 `tasks/lessons.md` 참조.

---

## 답변 원칙

### 최우선 원칙
모든 기술 답변은 가능한 한 3GPP 표준 문서, 전공 서적, 논문에 근거하여 정확한 내용을 제공한다. 불확실한 경우 반드시 명시한다.

### 구체적 지침
1. **표준 문서 우선 참조**: TS 38.211(물리채널/변조), 38.212(채널코딩), 38.213(제어절차), 38.214(데이터절차), 38.104/141(RF 요구사항)
2. **전공 서적 참조 우선순위**: Proakis & Salehi *Digital Communications*, Tse & Viswanath *Fundamentals of Wireless Communication*, Dahlman et al. *5G NR*, Björnson et al. *Massive MIMO Networks*, Goldsmith *Wireless Communications*
3. **수식**: LaTeX 표기, 파라미터 정의 명시, 단위 필수 표기
4. **코드**: C/C++ 기본(필요 시 Python 병행), 표준 기반 파라미터 하드코딩 금지(상수/설정값 분리), 복잡한 알고리즘은 단계별 주석
5. **불확실한 내용**: "구현 정의(Implementation-specific)" 또는 "추정"임을 명시. Release 버전에 따라 달라지는 내용은 버전 명시

---

## 작업 기록 원칙

- 코드를 수정하면 `tasks/todo.md`에 진행 상황을 반영한다.
- 실수를 교정받거나 비자명한 접근이 확인되면 `tasks/lessons.md`에 무엇/왜/적용 형식으로 기록한다.
- 완료된 기능 구현/실측 결과는 `docs/analysis/history.md`에 누적한다 — 이 파일(`AGENTS.md`)이나 `CLAUDE.md`에는 상시 컨텍스트만 유지하고 긴 이력을 쌓지 않는다.
