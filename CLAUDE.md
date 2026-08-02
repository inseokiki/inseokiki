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

## 📐 답변 원칙 (Claude에게)

### 최우선 원칙
```
모든 기술 답변은 가능한 한 3GPP 표준 문서, 전공 서적, 논문에 근거하여
정확한 내용을 제공할 것. 불확실한 경우 반드시 명시할 것.
```

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

### 1. PHY Signal Processing 알고리즘 개발
- **채널 추정**: LS / MMSE / 보간 기반 추정 (DMRS, CSI-RS 활용)
- **등화 (Equalization)**: ZF / MMSE / DFE
- **MIMO 검출**: MRC / ZF / MMSE / SIC / Sphere Decoding
- **복조 / 복호화**: LDPC (데이터), Polar Code (제어), Viterbi
- **빔포밍 / 프리코딩**: Codebook 기반, SRS 기반 Reciprocity

### 2. 기지국-단말 연동 시험 (RF 연동)
- gNB ↔ UE 실제 연동 시험
- 단말 DM (Debug Message) 로그 분석 (PDSCH/PUSCH BLER, MCS, Rank, CQI/RI/PMI, RLF 원인, Handover/RRC 절차 검증)
- OTA (Over-the-Air) 시험 및 RF 환경 분석, 이상 동작 원인 분석

### 3. 표준 기반 기능 검증
- 3GPP TS 38.xxx 시리즈 기반 스펙 검토
- 기능 구현과 표준 규격 정합성 확인, Test Case 설계 및 검증

---

## 📡 자주 다루는 5G NR 기술 영역

### 물리 채널
| 채널 | 방향 | 주요 내용 |
|------|------|-----------|
| PDSCH | DL | 데이터, LDPC, DMRS Type1/2 |
| PUSCH | UL | 데이터, LDPC, DFT-s-OFDM/CP-OFDM |
| PDCCH | DL | 제어, Polar Code, CORESET |
| PUCCH | UL | UCI (CQI/RI/HARQ-ACK) |
| PBCH | DL | MIB, Polar Code, SSB |
| PRACH | UL | 랜덤 접속, ZC 시퀀스 |

### 핵심 알고리즘
- **채널 추정**: DMRS 기반 LS/MMSE + 2D Wiener 보간
- **MIMO**: SU-MIMO (최대 8 레이어), MU-MIMO, Massive MIMO (64T64R)
- **HARQ**: Chase Combining / Incremental Redundancy
- **링크 어댑테이션**: CQI → MCS 매핑, OLLA (Outer Loop Link Adaptation)
- **빔관리**: SSB/CSI-RS 기반 빔 스위핑, P1/P2/P3 절차

### DM 로그 분석 주요 지표
```
- PDSCH/PUSCH BLER (Block Error Rate)
- MCS Index (0~28), TBS (Transport Block Size)
- RI (Rank Indicator), PMI (Precoding Matrix Indicator)
- CQI (Channel Quality Indicator, 0~15)
- RSRP / RSRQ / SINR
- HARQ 재전송 횟수, RV (Redundancy Version)
- Timing Advance (TA)
- RLF 원인 코드
```

---

## 📄 문서 변환 기능

3GPP DOCX/PDF 원본을 AI가 검색·이해하기 쉬운 Markdown/텍스트로 변환할 수 있다. 사용법은 `skills/document-analysis/SKILL.md`, 실제 변환은 `scripts/document_to_markdown.sh` 참조. 원본은 항상 보존하고 변환본을 별도 파일로 생성한다(덮어쓰지 않음). 스캔 PDF는 OCR이 별도로 필요하다.

---

## 🔄 업데이트 이력

| 날짜 | 내용 |
|------|------|
| 2026-08-02 | `phy_lab/personal/`을 기준본으로 신규 작성 — 기존 `CLAUDE.md`(2026-05-24~2026-07-15 누적본)를 상시 컨텍스트/이력/작업/교훈으로 분리. 원본 백업: `docs/analysis/CLAUDE.md.original-20260802.bak`, 상세 완료 이력은 `docs/analysis/history.md`로 이관 |
| 2026-08-02 | git 히스토리 재조정 — 분리 작업(위 항목) 커밋 전 `origin/develop`이 7커밋(4x4 MIMO/CSI-RS 코드북/RI+PMI/ULPC, 7/21~7/24) 앞서 있던 것을 발견, stash→fast-forward pull→CLAUDE.md 수동 병합으로 정리. 원격에만 있던 07-21~07-22 항목은 `docs/analysis/history.md`로 이관 |
| 2026-08-02 | PHY LLS: CL_4PORT 공간상관(Kronecker, XPOL 2x2 블록) 추가 — 동일편파 상관만으로는 ρ→1에서도 rank-1이 거의 선택 안 됨을 확인(교차편파 다이버시티 변형이 계속 유리, 물리적으로 타당). XPD 누설 상관 확장은 `tasks/todo.md`에 후속 과제로 등록 |
| 2026-08-02 | 방향 확인: NTN보다 기존 LLS 완성도(채널×기능 조합 공백 메우기) 우선. `tasks/todo.md` A/B/C 그룹으로 재정리 |
| 2026-08-02 | PHY LLS: PBCH/PDCCH에 FLAT_FADING/TDL 추가(genie-aided CSI) — 완성도 작업 1단계, AWGN 전용이던 마지막 두 채널 해소. 구현 중 `qam_demap_llr_mmse()` 오용(mmse_equalize 선행 누락으로 SNR 무관 BLER floor) 발견·수정 |

---

*이 파일은 Claude와의 작업을 통해 지속적으로 업데이트됩니다. 상시 컨텍스트만 유지하고, 긴 작업 이력은 누적하지 않습니다.*
