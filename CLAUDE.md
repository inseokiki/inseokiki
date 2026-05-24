# CLAUDE.md — 5G NR PHY Engineer Context

> 이 파일은 Claude Code가 프로젝트 시작 시 자동으로 읽는 컨텍스트 파일입니다.
> 나의 전문 영역, 개발 환경, 업무 방식을 정의합니다.

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
- **단말 DM (Debug Message) 로그 분석**
  - PDSCH/PUSCH BLER, MCS, Rank, CQI/RI/PMI 확인
  - RLF (Radio Link Failure) 원인 분석
  - Handover, RRC 절차 검증
- OTA (Over-the-Air) 시험 및 RF 환경 분석
- 이상 동작 원인 분석 및 디버깅

### 3. 표준 기반 기능 검증
- 3GPP TS 38.xxx 시리즈 기반 스펙 검토
- 기능 구현과 표준 규격 정합성 확인
- Test Case 설계 및 검증

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

## 🚀 최근 작업 & 진행 예정

### ✅ 최근 완료
- **Massive MIMO UL Rx Beamforming — Eigen 기반 구현 완료**
  - 업링크 수신 빔포밍에서 채널 공간 공분산 행렬 추정 → EVD(고유값 분해) → Eigenbeam 추출 → UL 수신 결합
  - 관련 표준: 3GPP TS 38.214 (UL MIMO), Rel-17 UL 공간 다중화

### 🏠 사이드 프로젝트
- **PHY Link Level Simulator (LLS) 자체 개발**
  - 개인 프로젝트로 지속 개발 중
  - **개발 환경**: WSL (Windows Subsystem for Linux) + Claude Code
  - **버전 관리**: Git
  - 목표: 5G NR PHY 알고리즘 검증 및 프로토타이핑 플랫폼 구축

### 🔄 진행 예정
- **NVIDIA Aerial SDK 연동**
  - NVIDIA Aerial (GPU 가속 기반 5G NR L1 소프트웨어 스택) 도입 검토
  - cuBB (CUDA Baseband) / cuPHY 기반 PHY 가속 처리
  - CPU 기반 구현 대비 레이턴시 / 처리량 개선 목표
  - 참조: NVIDIA Aerial SDK Documentation, cuPHY API

- **NTN (Non-Terrestrial Network) 작업 검토**
  - 3GPP Rel-17/18 NTN 표준 기반 (TS 38.821, TR 38.811)
  - LEO / GEO 위성 및 HAPS 시나리오
  - 주요 고려사항: 대규모 Doppler 보상, 긴 전파 지연(RTT), TA(Timing Advance) 확장
  - Feeder link / Service link 구조 분석

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

## 🔄 업데이트 이력

| 날짜 | 내용 |
|------|------|
| 2026-05-24 | 최초 작성 — 기본 프로필 및 PHY 컨텍스트 |
| 2026-05-24 | 직급(수석연구원), 3GPP Rel-17/18/19, Massive MIMO Eigen BF 완료, Aerial/NTN 추가 |
| 2026-05-24 | 사이드 프로젝트 PHY LLS 개발 환경 추가 (WSL + Claude Code + Git) |

---

*이 파일은 Claude와의 작업을 통해 지속적으로 업데이트됩니다.*
