# 2026년 개발자용 AI Coding Agent 조사 및 PHY 프로젝트 추천

> 조사 기준일: 2026-09-03  
> 적용 대상: `inseokiki` 5G NR PHY Link-Level Simulator  
> 목적: 최근 개발자용 AI agent와 MCP 생태계를 비교하고, 이 프로젝트에 적합한 운용 방식을 결정한다.
>
> **참고 자료 — 현행 진입 경로 아님**: 이 문서는 조사 기준일(2026-09-03) 시점의 도구 비교이며
> `CLAUDE.md`/`AGENTS.md`가 참조하는 상시 지침이 아니다. 실제 채택된 현재 운용 방식(어느
> agent를 언제 쓰는지, spec-lookup/log-parser 서브에이전트 등)은 `CLAUDE.md`/`AGENTS.md`
> 본문과 `.claude/agents/`가 기준이며, 이 문서와 어긋나면 그쪽을 따른다. 도구 생태계 사실이
> 오래됐다는 이유로 이 문서 내용을 검증 없이 갱신하지 않는다.

---

## 1. 핵심 결론

`inseokiki`에는 한 제품에 모든 일을 맡기는 것보다 다음 구성이 가장 적합하다.

```text
주 구현 agent: Claude Code 또는 Codex
독립 검증 agent: 구현에 사용하지 않은 다른 계열 agent
Google 계열 보조: Antigravity
확장 도구: 로컬 3GPP 검색 + 결정론적 테스트
MCP: 외부 시스템/구조화 데이터가 필요할 때만 제한적으로 사용
```

가장 추천하는 실제 조합은 다음과 같다.

```text
Claude Code ── 구현 및 장기 리팩터링
Codex       ── 독립 code review, 수치 검증, 테스트
Antigravity ── 제3 검토, 문서 중심 분석, 병렬 agent 작업
사람        ── 표준 원문 확인 및 merge 결정
```

두 agent가 같은 작업트리를 동시에 수정하면 안 된다. 구현과 검증은 별도 `git worktree` 또는 별도 branch에서 진행한다.

---

## 2. Google 도구에 대한 정정

이전 조사에서 Google의 coding agent를 `Gemini CLI` 중심으로 설명한 것은 불완전했다.

### 정확한 구분

| 구분 | 역할 |
|---|---|
| Gemini | 기반 모델 계열 |
| Gemini CLI | 기존 오픈소스 터미널 coding agent |
| Google Antigravity | Google의 주력 agentic development platform |
| Antigravity IDE | IDE와 Agent Manager가 결합된 개발 환경 |
| Antigravity 2.0 | 여러 local agent와 장기 작업을 관리하는 독립 command center |
| Antigravity CLI (`agy`) | Antigravity agent harness를 사용하는 터미널 인터페이스 |

즉, **Google의 현재 핵심 코드 agent 제품을 하나만 지목하면 Antigravity가 맞다.**

Gemini CLI가 사라진 것은 아니지만, 현재 Antigravity CLI 문서는 Gemini CLI에서 extensions, skills, settings를 가져오는 migration 경로를 별도로 제공한다. Antigravity CLI와 Antigravity 2.0은 같은 agent harness와 공통 설정을 사용한다.

### Antigravity의 특징

- Editor View와 agent-first Manager surface
- editor, terminal, browser를 사용하는 자율 agent
- background/long-running task
- 여러 local agent 병렬 관리
- plan, diff, screenshot, browser recording 등의 Artifact
- Artifact에 직접 피드백
- skills, agents, rules, MCP, hooks를 plugin으로 패키징
- terminal sandbox
- headless/CI 실행
- Antigravity CLI와 GUI 사이 conversation 이동
- Gemini 외 일부 다른 모델 선택 가능

`inseokiki`에서는 다음 용도로 적합하다.

- TS 38.211/212/214 문서를 함께 놓고 긴 규격 비교
- Claude/Codex와 다른 계열의 제3 의견
- 여러 독립 검증 작업 병렬 실행
- 구현 계획, diff, test 결과를 Artifact로 검토
- 장기적으로 PHY validation 작업을 background agent로 자동화

단, 표준 문서를 많이 읽는다고 해석이 자동으로 정확해지는 것은 아니다. 반드시 spec version, clause, page를 결과에 남겨야 한다.

공식 자료:

- Google Developers Blog, *Build with Google Antigravity*: https://developers.googleblog.com/en/build-with-google-antigravity-our-new-agentic-development-platform/
- Antigravity CLI Overview: https://antigravity.google/docs/cli/overview/
- Antigravity CLI Features: https://www.antigravity.google/docs/cli/features
- Antigravity CLI announcement: https://www.antigravity.google/blog/introducing-google-antigravity-cli

---

## 3. 주요 AI coding agent 비교

| Agent | 형태 | 강점 | 약점 | PHY 적합성 |
|---|---|---|---|---:|
| Claude Code | CLI/IDE/Web | 설계·구현, 긴 작업, skills/hooks/subagents | 자기 구현의 가정에 고착될 수 있음 | 매우 높음 |
| Codex | App/CLI/IDE/Cloud | 저장소 감사, 디버깅, 테스트, review, sandbox | OpenAI 생태계 의존 | 매우 높음 |
| Google Antigravity | App/IDE/CLI | agent orchestration, Artifact, 병렬 작업, Google 모델 | 플랫폼이 넓어 초기 구성이 복잡할 수 있음 | 높음 |
| Cursor | AI IDE/CLI/Cloud | 편집 UX, 빠른 반복, 다양한 모델 | IDE 중심, background agent 보안 고려 필요 | 중상 |
| GitHub Copilot | IDE/CLI/Cloud PR agent | Issue→PR→CI, GitHub 통합과 조직 정책 | 개인 연구·로컬 실험에는 다소 무거움 | 중 |
| Aider | 오픈소스 CLI | BYOK, git 중심, repo map, 가벼움 | 고급 orchestration은 직접 구성 필요 | 중 |
| Cline/Roo Code | VS Code extension/CLI | provider 선택, modes, MCP, 높은 사용자 제어 | 설정과 승인 관리 부담 | 중 |
| OpenHands | 오픈소스 agent platform/SDK | self-host, Docker, 여러 모델, 자동화 | 개인 단일 repo에는 운영 복잡도 큼 | 현재 낮음 |

### Claude Code

적합한 역할:

- NR LDPC/Polar 구현
- 여러 파일을 건드리는 구조 변경
- `pdsch.c`, `pusch.c` 공통 pipeline 리팩터링
- 장기 task와 문서 이력 관리

권장 구성:

- `CLAUDE.md`: 항상 필요한 프로젝트 사실
- `.claude/rules/`: path별 제약
- `.claude/skills/`: 반복 작업 절차
- hooks: 빌드, formatter, test, 위험 명령 차단
- subagents: 독립적인 검색이나 보조 분석

Anthropic은 `CLAUDE.md`를 약 200줄 이하로 유지하고, 절차는 skill, 결정론적 자동화는 hook으로 옮길 것을 권장한다.

공식 자료:

- https://claude.com/blog/steering-claude-code-skills-hooks-rules-subagents-and-more

### Codex

적합한 역할:

- 구현자의 설명을 신뢰하지 않는 독립 diff review
- 3GPP 표준과 코드 불일치 탐색
- numerical invariant 검사
- sanitizer 및 regression 실행
- 대형 코드베이스 흐름 추적
- 어려운 문제를 test-driven loop로 개선

이 프로젝트에서는 단순 코드 생성보다 **검증 agent**로서 가치가 특히 높다.

공식 자료:

- https://learn.chatgpt.com/docs
- https://learn.chatgpt.com/use-cases
- https://learn.chatgpt.com/docs/extend/mcp?surface=cli

### Google Antigravity

적합한 역할:

- agent-first Manager를 통한 병렬 작업
- 긴 규격과 코드의 동시 분석
- Artifact 기반 검토
- GUI/IDE/CLI 사이 작업 전환
- background validation

기존 Gemini CLI만 새로 도입하기보다 Antigravity/Antigravity CLI를 우선 평가하는 편이 현재 Google의 제품 방향과 맞다.

### Cursor

적합한 역할:

- IDE에서 직접 코드를 보며 짧게 반복
- 함수 단위 수정
- inline completion과 agent edit 결합
- 다양한 모델을 한 UI에서 비교

Cursor background agent는 remote machine에서 자동으로 명령을 실행할 수 있으므로 prompt injection과 code exfiltration 위험을 고려해야 한다.

공식 자료:

- https://cursor.com/docs
- https://docs.cursor.com/background-agent

### GitHub Copilot Coding Agent

적합한 workflow:

```text
GitHub Issue -> cloud coding agent -> PR -> CI -> review
```

팀 개발이나 GitHub 중심 운영에는 강하지만, 현재처럼 로컬에서 표준을 연구하고 실험하는 개인 PHY 프로젝트에서는 우선순위가 낮다.

공식 자료:

- https://docs.github.com/en/copilot/concepts/agents/copilot-cli/about-copilot-cli
- https://docs.github.com/en/copilot/how-tos/copilot-cli/customize-copilot/add-mcp-servers

### Aider

적합한 경우:

- 모델과 API 비용을 직접 통제
- 특정 vendor 종속 최소화
- 사람이 매 변경을 적극 검토
- git 중심 pair programming

공식 자료:

- https://aider.chat/docs/

### Cline/Roo Code

적합한 경우:

- VS Code를 유지하면서 agent 기능 추가
- model provider를 직접 선택
- Ask/Architect/Code/Orchestrator 등 mode 분리
- MCP별 승인과 권한을 세밀하게 조정

Roo Code는 mode별 tool group과 model을 다르게 지정할 수 있어 실험적인 multi-model workflow에 유용하다.

공식 자료:

- https://docs.cline.bot/cli/cli-reference
- https://roocodeinc.github.io/Roo-Code/basic-usage/using-modes/

### OpenHands

적합한 경우:

- self-hosted agent server
- Docker/VM 격리
- 여러 저장소 상시 관리
- webhook, Slack, GitHub 기반 자동화
- agent SDK를 직접 제품에 내장

현재 개인 PHY repo 하나만 관리한다면 도입 비용이 이득보다 클 가능성이 높다.

공식 자료:

- https://docs.openhands.dev/sdk/index

---

## 4. inseokiki 권장 운용 방식

### 방식 A: Claude 구현 + Codex 검증

가장 추천한다.

```text
worktree/feature-nr-ldpc
  └─ Claude Code가 구현

원본 또는 review worktree
  └─ Codex가 diff와 테스트를 독립 검토

Antigravity
  └─ 필요할 때 3GPP 문서/설계의 제3 검토
```

Claude 요청 예:

```text
TS 38.212 기준 NR LDPC Phase 1을 구현하라.
먼저 관련 clause, 설계, 변경 파일, bit-exact 검증 방법을 제안하라.
승인 전에는 코드를 수정하지 마라.
```

Codex 요청 예:

```text
Claude가 만든 diff를 구현자의 설명과 독립적으로 검토하라.
correctness와 3GPP 불일치를 우선하고,
파일/라인/재현 절차/누락 테스트를 보고하라.
코드는 수정하지 마라.
```

### 방식 B: Codex 구현 + Claude 설계 검토

다음 작업에는 이 방식도 적합하다.

- compiler/sanitizer 오류 수정
- dispatch 통합
- 반복 코드 제거
- 공통 simulation engine 구축
- test harness 정비

### Antigravity를 추가하는 시점

다음 요구가 생길 때 추가한다.

- 여러 agent를 한 화면에서 관리하고 싶을 때
- Claude/Codex 작업을 별도의 제3 모델로 검토할 때
- 3GPP 문서 분석과 코딩 task를 병렬화할 때
- background task와 Artifact 검토가 필요할 때
- Gemini CLI에서 더 통합된 Google agent 환경으로 이동할 때

---

## 5. MCP에 대한 판단

MCP는 agent의 추론 능력을 높이는 기술이 아니다. 외부 데이터와 실행 기능을 agent가 호출할 수 있는 표준 tool interface로 제공한다.

따라서 다음 조건에서만 사용한다.

- 외부 시스템에 접근해야 함
- 여러 agent client에서 같은 tool을 재사용해야 함
- 입력/출력을 구조화해야 함
- 인증과 권한을 명시적으로 관리해야 함

### 1순위: 로컬 3GPP 문서 검색 도구

처음부터 MCP로 만들 필요는 없다. 먼저 CLI로 구현하고 안정화한 뒤 Claude, Codex, Antigravity가 공동 사용해야 할 때 MCP로 승격한다.

권장 interface:

```text
search_spec(spec, version, query)
get_clause(spec, version, clause)
compare_clauses(specs, query)
```

필수 반환 정보:

- spec 번호
- version/release
- clause
- page
- 짧은 원문
- 로컬 PDF 경로
- checksum

표준 검증은 vector similarity보다 출처 추적성이 중요하다.

### 2순위: PHY validation CLI 또는 MCP

```text
build_phy()
run_regression()
run_sanitizers()
run_unit_test(module)
run_snr_sweep(config, seed)
compare_curves(baseline, candidate)
check_invariants(scope)
```

출력은 JSON으로 구조화한다.

```json
{
  "passed": 74,
  "failed": 0,
  "warnings": 0,
  "seed": 12345,
  "commit": "<sha>",
  "artifacts": []
}
```

agent가 이미 shell을 사용할 수 있으므로, 단일 repo에서는 MCP보다 `scripts/phy_verify.sh`와 skill 조합이 더 단순하다.

### 3순위: GitHub read-only MCP

허용 후보:

- Issue 읽기
- PR/diff 읽기
- CI 결과 조회
- commit 검색

초기 차단 후보:

- merge
- branch 삭제
- force push
- release 발행
- secret 수정

### 불필요하거나 중복되는 MCP

- filesystem MCP
- shell MCP
- 일반 git MCP
- 범용 memory MCP
- 출처가 불명확한 web-search MCP
- 다른 coding agent를 무조건 호출하는 agent-wrapper MCP

Claude Code, Codex, Antigravity는 파일, shell, git, subagent 기능을 자체 제공하므로 중복 tool은 agent의 선택지만 불필요하게 늘린다.

---

## 6. MCP보다 먼저 도입할 것

### Skill

예:

```text
nr-conformance-review
phy-regression
numerical-invariant-check
release-handoff
```

`nr-conformance-review` 권장 절차:

1. 관련 spec/version/clause 확인
2. 코드 경로 추적
3. 표준과 구현 차이 분류
4. 구현 정의와 correctness defect 구분
5. noise/power normalization 검사
6. unit/regression test 확인
7. 결과에 근거와 재현 절차 기록

### Hook

반드시 실행되어야 하는 것은 모델 지시문보다 hook으로 강제한다.

- C 파일 수정 후 formatter
- commit 전 전체 build
- compiler warning 발생 시 실패
- LDPC 수정 시 LDPC unit test
- PDSCH/PUSCH 수정 시 dispatch regression
- 위험 명령 차단

### AGENTS.md/CLAUDE.md

루트 지침에는 항상 필요한 사실만 둔다.

- 프로젝트 목표
- 빌드/test 명령
- 작업 전 git 확인
- 표준 우선 원칙
- 기존 변경 보존
- 상세 문서 index

긴 작업 절차와 이력은 skill, `tasks/`, `docs/analysis/`로 분리한다.

---

## 7. 보안 원칙

- 인터넷에서 찾은 MCP를 `npx -y`로 즉시 실행하지 않는다.
- source를 검토하고 version/hash를 고정한다.
- read-only와 최소 권한을 기본값으로 사용한다.
- workspace 밖 filesystem 접근을 금지한다.
- API token을 repo나 MCP 설정 파일에 평문 저장하지 않는다.
- destructive/write tool은 별도 승인 대상으로 둔다.
- remote MCP가 소스 코드나 3GPP 문서를 외부로 전송하는지 확인한다.
- agent와 MCP를 가능한 한 sandbox에서 실행한다.
- `--dangerously-skip-permissions` 계열 옵션을 일상 설정으로 사용하지 않는다.

MCP는 임의 데이터 접근과 코드 실행 경로가 될 수 있다. 공식 MCP 명세도 사용자 동의, 데이터 통제, tool 실행 승인과 명확한 권한을 핵심 원칙으로 둔다.

참고:

- https://modelcontextprotocol.io/specification/2024-11-05/index
- https://docs.github.com/en/copilot/concepts/mcp-management

---

## 8. 최종 도입 제안

### 지금 적용

1. Claude Code와 Codex를 구현/검증 역할로 분리
2. 별도 worktree 사용
3. `nr-conformance-review` skill 작성
4. build/test/warning hook 구성
5. 로컬 3GPP clause 검색 CLI 제작
6. 모든 수치 실험에 seed와 commit hash 기록

### 단기 평가

7. Google Antigravity를 제3 review 및 병렬 문서 분석용으로 시험
8. Gemini CLI를 새로 주력 도구로 구성하기보다 Antigravity CLI 우선 평가
9. 3GPP CLI가 안정화되면 MCP로 승격

### 현재 보류

10. OpenHands self-hosting
11. 다수의 범용 MCP 설치
12. agent가 자동 merge/push/release하도록 허용

최종 추천 문장:

> `Claude Code + Codex 교차검증 + Google Antigravity 보조 + 로컬 3GPP 검색 + 결정론적 검증 hook`이 현재 `inseokiki`에 가장 적합하다.

