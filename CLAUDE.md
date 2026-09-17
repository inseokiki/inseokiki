# CLAUDE.md — 5G NR PHY 개인 프로젝트 (Claude Code 진입 파일)

> 이 파일은 Claude Code가 개인 5G PHY 프로젝트 시작 시 자동으로 읽는 상시 컨텍스트 파일입니다.
> Codex용 진입 파일은 `AGENTS.md` — 원칙은 동일하며 도구별로 문서만 분리되어 있습니다.
> 상세 개발 이력은 여기 두지 않습니다 — `docs/analysis/history.md` 참조.
> 코드 구조/모듈 설계는 `STRUCTURE.md` 참조.
> 현재/향후 작업은 `tasks/todo.md`, 과거 실수와 교훈은 `tasks/lessons.md` 참조 — 세션 시작 시 `tasks/lessons.md`를 먼저 검토할 것.

---

## 👤 엔지니어 프로필

| 항목 | 내용 |
|------|------|
| 직급 | 수석연구원 |
| 전문 분야 | 5G NR PHY (Physical Layer) |
| 포지션 | 기지국 (gNB) PHY 개발 엔지니어 |
| 표준 기반 | 3GPP Release 17 / 18 / 19 (5G NR Advanced) |
| 주요 관심 | Signal Processing 알고리즘 설계 및 최적화 |

---

## 🛠️ 개발 환경

### 언어 및 툴
- **주 언어**: C / C++ (PHY 알고리즘 구현, 실시간 처리)
- **스크립트 / 검증**: Python (알고리즘 프로토타이핑, 데이터 분석)
- **시뮬레이션**: MATLAB (초기 알고리즘 설계, 파형 분석)

### 개발 도메인
- gNB PHY 레이어 C/C++ 구현
- Signal Processing 알고리즘 (채널 추정, 등화, MIMO 검출 등)
- 실시간 성능 최적화 (레이턴시, 처리량)
- 3GPP 표준 스펙 기반 기능 구현

### 이 프로젝트: PHY Link Level Simulator (LLS)
- 5G NR PHY 알고리즘 검증/프로토타이핑용 개인 프로젝트, 지속 개발 중
- 개발 환경: WSL(Ubuntu) + Claude Code, 버전 관리는 Git
- 코드 구조는 `STRUCTURE.md` 참조

---

## 🔀 Git 작업 시작 절차 (필수)

사용자가 "시작하자", "작업하자" 등으로 작업 시작을 알리면, 코드를 만지기 전에 **항상 먼저** 다음을 자동으로 실행할 것(사용자가 매번 요청하지 않아도 됨):

```bash
git fetch origin
git status
git log --oneline develop..origin/develop   # 원격이 앞서 있으면 확인
```

원격에 로컬에 없는 커밋이 있으면(다른 세션/기기에서 작업했을 가능성) 바로 알리고, 병합/리베이스 방향을 사용자에게 먼저 확인한다 — 임의로 진행하지 않는다. 이 절차를 건너뛰어 히스토리가 갈라진 사고 이력이 있음 — 상세는 `tasks/lessons.md` 참조.

---

## 🧪 검증(회귀 테스트) 범위 원칙

`lab/HARNESS_ANALYSIS.md` L-02(2026-09-03) 반영. 작은/국소적 코드 수정에서 `PHY/regression_test.sh` 전체(현재 95개 케이스, 2026-09-10 갱신)를
매번 돌리는 게 기본값이 아니다 — **기본은 금일 변경한 파일·기능과 직접 관련된 case만 골라 실행(targeted)**.

- 공용 API·config parser·LDPC/modulation/channel 같은 공유 계층을 건드렸어도, 곧바로 전체 회귀로 확대하지 않는다.
  실제 변경된 인터페이스의 호출부를 찾아 영향받는 채널·모드의 대표 case(고정 seed, 적은 SNR 포인트/trial)만 먼저 돌린다.
- targeted 실행에서 예상 밖 회귀가 나오면 관련 그룹부터 단계적으로 범위를 넓힌다 — 이때도 자동으로 전체 95개로
  점프하지 않는다.
- 전체 회귀는 다음 경우에만 실행한다: (1) 사용자가 명시적으로 전체 검증을 요청, (2) merge/release 전 최종 확인.
- 이 원칙은 검증 자체를 생략해도 된다는 뜻이 아니다 — 범위를 좁히는 것이지 "검증했다고 주장하려면 실행 근거가
  있어야 한다"는 CLAUDE.md 검증 기준은 그대로 적용된다.

---

## 📐 답변 원칙 (Claude에게)

### 최우선 원칙
```
모든 기술 답변은 가능한 한 3GPP 표준 문서, 전공 서적, 논문에 근거하여
정확한 내용을 제공할 것. 불확실한 경우 반드시 명시할 것.
```

**적용 범위**: 완화되는 것은 절차(spec-lookup 서브에이전트 위임, 표준 문서 확인을 매번
강제하는 것)뿐이며, 정확성 기준 자체는 절대 완화되지 않는다. Makefile/빌드 시스템/git
정리/CLAUDE.md·스킬 설정 같은 3GPP 표준과 무관한 순수 엔지니어링 작업에는 이 절차를
강제하지 않는다 — 단, 이런 작업 중에도 3GPP 표준·파라미터·수식을 실제로 언급하게 되면
그 부분은 여전히 실제 근거가 있어야 하고, 근거를 확인하지 못했으면 반드시 "확인 안 됨/
추정"으로 명시한다. 표준에 없는 내용을 표준인 것처럼 지어내는 것은 이 완화와 무관하게
항상 금지.
(2026-09-03 확인 — 매 사소한 변경마다 spec 확인 절차를 거치는 오버헤드만 줄이려는 것이지,
근거 없는 내용을 만들어도 된다는 뜻이 아님을 사용자가 명시적으로 재확인)

### 구체적 지침
1. **표준 문서 우선 참조**
   - 3GPP TS 38.211 (물리 채널 및 변조)
   - 3GPP TS 38.212 (채널 코딩)
   - 3GPP TS 38.213 (물리 계층 제어 절차)
   - 3GPP TS 38.214 (물리 계층 데이터 절차)
   - 3GPP TS 38.104 / 38.141 (기지국 RF 요구사항)

2. **전공 서적 참조 우선순위**
   - Proakis & Salehi, *Digital Communications* (5th Ed.)
   - Tse & Viswanath, *Fundamentals of Wireless Communication*
   - Dahlman et al., *5G NR: The Next Generation Wireless Access Technology*
   - Björnson et al., *Massive MIMO Networks*
   - Goldsmith, *Wireless Communications*

3. **수식 표기**
   - 수식은 LaTeX 형식으로 명확히 표기
   - 파라미터 정의를 수식과 함께 명시
   - 단위 반드시 표기 [Hz, dB, dBm, bps/Hz 등]

4. **코드 작성 시**
   - C/C++ 기본, 필요 시 Python 병행 제공
   - 실시간 처리 고려 (메모리 효율, 연산량)
   - 표준 기반 파라미터 값 하드코딩 금지 → 상수/설정값으로 분리
   - 복잡한 알고리즘은 단계별 주석 필수

5. **불확실한 내용 처리**
   - 표준에 명확히 정의되지 않은 내용은 "구현 정의(Implementation-specific)"임을 명시
   - 추측성 답변 시 반드시 "추정" 또는 "일반적 관행" 명시
   - 표준 버전(Release)에 따라 달라지는 내용은 버전 명시

---

## 🔬 주요 업무 영역

1. **PHY Signal Processing 알고리즘 개발**: 채널추정(LS/MMSE/보간), 등화(ZF/MMSE/DFE), MIMO 검출(MRC/ZF/MMSE/SIC), 복호(LDPC/Polar), 빔포밍/프리코딩(Codebook, SRS reciprocity).
2. **기지국-단말 연동 시험(RF 연동)**: gNB↔UE 실제 연동, 단말 DM 로그 분석(BLER/MCS/Rank/CQI/RI/PMI/RLF/Handover), OTA 시험.
3. **표준 기반 기능 검증**: 3GPP TS 38.xxx 스펙 검토, 구현-표준 정합성 확인, Test Case 설계.

이 프로젝트(LLS)에서 실제로 구현된 채널/모드/알고리즘의 현재 범위는 `STRUCTURE.md`가 기준 문서다 — 위 목록은 엔지니어 배경 참고용 일반 영역 나열이며 이 코드베이스의 실제 지원 여부를 나타내지 않는다.

---

## 📄 문서 변환 기능

3GPP DOCX/PDF 원본을 AI가 검색·이해하기 쉬운 Markdown/텍스트로 변환할 수 있다. 사용법은 `skills/document-analysis/SKILL.md`, 실제 변환은 `scripts/document_to_markdown.sh` 참조. 원본은 항상 보존하고 변환본을 별도 파일로 생성한다(덮어쓰지 않음). 스캔 PDF는 OCR이 별도로 필요하다.

---

## 🔄 업데이트 이력

이 파일 자체의 변경 이력(분리/재구조화 등 메타 변경 포함)은 `docs/analysis/history.md` 하단
"🔄 원본 CLAUDE.md 업데이트 이력" 표에 이어서 기록한다(2026-09-11 문서 정리 세션에서 이관).

---

*이 파일은 Claude와의 작업을 통해 지속적으로 업데이트됩니다. 상시 컨텍스트만 유지하고, 긴 작업 이력은 누적하지 않습니다.*
