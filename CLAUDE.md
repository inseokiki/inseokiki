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

- **PHY LLS — DL/UL 기능 대거 확장 (2026-07-13)**
  - **MIMO**: PDSCH에 SU-MIMO 2x2 추가 — SIMO_MRC(1x2 수신 다이버시티), SM_2X2(2계층 공간다중화, ZF/MMSE). 다이버시티/검출기 이득 수치 검증 완료
  - **HARQ**: Circular buffer rate matching + IR/Chase 소프트 컴바이닝 (`rate_matching.c`). IR이 Chase보다 항상 우세함을 실측 확인
  - **TDL 주파수선택적 페이딩**: 근사 6탭 NLOS PDP (`tdl.c`, TS 38.901 표 근사치 — 정확한 표 아님, 문서화됨). 주파수 선택성 실증 확인
  - **PUSCH**: Transform Precoding (DFT-s-OFDM, `dft_precode.c`) — PAPR 저감 실증(8.02dB→0.00dB). PUSCH+TDL+ZF/MMSE 등화 조합에서 "DFT precoding은 선형 등화기와 결합 시 주파수선택적 채널에서 오히려 손해"라는 결론을 수식적으로 유도·검증 (SC-FDMA 문헌과 일치)
  - **PUCCH Format 0/1/2/3**: 시퀀스 기반 검출(F0), 반복/코히런트 결합(F1, 이득 이론과 정확히 일치 확인), 코딩된 UCI(F2/F3, Reed-Muller 대신 기존 Polar 재사용)
  - **버그 수정**: `polar.c`의 frozen-bit 선택이 rate-matching shortening 위치를 모르고 있어 info bit가 파괴되는 기존 버그 발견·수정 (PDCCH도 영향받던 버그, PUCCH F2 K별 스윕 테스트 중 발견). 수정 후 N=32/64 전 K 범위·전 rate-matching 영역에서 무손실 라운드트립 190/190 통과 확인
  - 전부 AWGN/flat fading 기준, 각 기능은 독립적으로 격리 구현 (조합 확장은 다음 과제) — 상세 설계는 `STRUCTURE.md` 참조

- **PHY LLS — 기능 조합 1단계: TDL + MIMO (2026-07-13)**
  - `run_pdsch_simo_mrc_tdl_simulation()` / `run_pdsch_sm2x2_tdl_simulation()` 추가 — Tx-Rx 안테나 쌍마다 독립 TDL tap-set을 드로우해 RE별 2x2(또는 1x2) 채널 행렬 구성. 기존 검출 함수(`mrc_combine`/`mimo_zf_detect`/`mimo_mmse_detect`)는 RE 단위 순수 함수라 수정 없이 재사용
  - SM_2X2는 flat 채널처럼 파일럿 RE 전체를 평균해 단일 `h_hat`을 구하는 방식이 주파수선택적 채널에서는 틀리므로, LS+보간(`ls_estimate`/`interpolate_channel`)을 안테나 쌍마다(4회) 수행해 RE별 채널을 얻도록 변경
  - 부수 발견: `main.c`가 `USE_DMRS=1`일 때 `MIMO_MODE`/`HARQ_ENABLE`/`CHANNEL_MODEL`을 전혀 참조하지 않아, 기존에 구현된 SIMO_MRC/SM_2X2/HARQ/TDL(SISO) 함수들이 config로 도달 불가능한 dead code였음 — 전체 배선 수정 (우선순위: HARQ > MIMO_MODE > TDL > 기본)
  - 검증: SM_2X2/SIMO_MRC 모두 SNR 증가에 따라 BLER 단조 감소, 동일 SNR에서 TDL이 flat fading 대비 항상 열화(예상된 방향)됨을 실측 확인
  - TDL+HARQ, MIMO+HARQ 조합은 미포함 (다음 단계)

- **PHY LLS — 기능 조합 2단계: TDL + MIMO(SM_2X2) + HARQ 전체 결합 (2026-07-13)**
  - `run_pdsch_sm2x2_tdl_harq_simulation()` 추가 — 2계층 공간다중화(2x2, ZF/MMSE) + 주파수선택적 TDL 채널(안테나 쌍마다 독립 tap-set, RE별 채널) + circular-buffer IR/Chase 소프트 컴바이닝을 한 함수에 결합. 레이어 A/B 각각 독립 mother LDPC 코드워드 + 독립 soft buffer, 채널은 HARQ 재전송마다 재드로우(시간 다이버시티)
  - 단순화(문서화됨): 두 레이어가 같은 재전송 occasion을 공유한다고 가정(레이어별 완전 독립 HARQ 프로세스 스케줄은 아님) — 한쪽 CRC만 통과해도 다른 쪽이 실패하면 계속 재전송
  - `main.c` dispatch에 HARQ_ENABLE 분기 세분화: MIMO_MODE=SM_2X2 && CHANNEL_MODEL=TDL이면 이 신규 함수로, 그 외 HARQ 조합은 기존 SISO `run_pdsch_harq_simulation()`으로 폴백
  - 검증: BLER(HARQ) ≤ BLER(1st) 항상 성립(재전송 이득), AvgTx가 SNR 증가에 따라 감소, HARQ 적용 시 동일 SNR에서 BLER이 HARQ 없는 SM_2X2+TDL 대비 크게 개선(20dB에서 0.93→0.29), IR이 Chase보다 우세한 기존 경향 재확인
  - SIMO_MRC+TDL+HARQ는 바로 다음 단계에서 완료 (아래 참조)

- **PHY LLS — 기능 조합 3단계: TDL + MIMO(SIMO_MRC) + HARQ (2026-07-14)**
  - `run_pdsch_simo_mrc_tdl_harq_simulation()` 추가 — 단일 코드워드(SIMO는 레이어 1개라 SM_2X2 조합보다 단순) + MRC(1x2 수신 다이버시티) + 안테나 브랜치마다 독립 TDL tap-set(매 HARQ attempt 재드로우) + circular-buffer IR/Chase. `run_pdsch_harq_simulation()`의 단일 코드워드 재전송 루프에 `run_pdsch_simo_mrc_tdl_simulation()`의 채널/추정 블록을 그대로 이식
  - `main.c` dispatch에 SIMO_MRC+TDL 분기 추가 — 이제 SIMO_MRC/SM_2X2 두 MIMO 모드 모두 TDL+HARQ까지 커버
  - 검증: BLER(HARQ)가 BLER(1st) 대비 전 SNR에서 크게 개선(10dB에서 0.957→0.093), AvgTx 단조 감소, IR이 Chase보다 우세한 경향 재확인
  - 이로써 CLAUDE.md에 있던 "기능 간 조합" 항목(TDL×MIMO×HARQ) 완료 — 남은 조합 후속과제는 PUSCH/PUCCH+TDL 확장

- **PHY LLS — TDL을 PUCCH로 확장 (Format 1/3 + 페이딩), PUSCH/PUCCH 배선 수정 (2026-07-14)**
  - 조사 결과 PUSCH는 이미 `run_pusch_tdl_simulation()`으로 TDL 구현이 끝나 있었음 — 실제 남은 작업은 PUCCH뿐이었음
  - PUCCH는 PDSCH/PUSCH와 달리 DMRS/LS 채널추정 파이프라인이 전혀 없어(F0~F3 전부 AWGN, H=1 가정), 새 파일럿 구조를 설계하는 대신 **genie-aided(완벽한 CSI)** 방식을 선택 — `tdl.c`의 `tdl_channel_apply()`가 이미 `h_out`으로 실채널을 노출하도록 설계돼 있어 이 접근과 정확히 맞아떨어짐. 채널추정 오차는 배제하고 코딩/결합 자체의 페이딩 대응력만 측정
  - `run_pucch_format1_tdl_simulation()`: 반복 심볼마다 독립 TDL 재드로우(시간 다이버시티), 기존 매치드필터 합산을 채널가중 MRC로 일반화(`conj(h)` 가중, `sum|h|^2`로 정규화) — H=1이면 기존 AWGN 버전과 동일하게 축소
  - `run_pucch_format3_tdl_simulation()`: `run_pusch_tdl_simulation()`의 TDL+DFT-precoding 등화 수식(ZF 균일노이즈분산, MMSE alpha 평균/분산 de-bias)을 그대로 재사용, LS추정 단계만 생략하고 genie `h_known`을 직접 `zf_equalize`/`mmse_equalize`에 입력
  - 부수 발견: `main.c`가 PUSCH/PUCCH를 `#include`조차 하지 않고 있어 이 채널들 자체가 (TDL 여부와 무관하게) config로 전혀 도달 불가능했음 — PDSCH에서 있었던 것과 같은 종류의 배선 누락, 이번에 함께 수정
  - 검증: F1/F3 모두 TDL에서 크래시/NaN 없이 SNR에 따라 단조 감소, 동일 SNR에서 TDL이 AWGN 대비 열화(예상된 방향), F1은 반복 횟수 늘릴수록(1→4) 페이딩 하에서도 결합 이득 유지 확인(-10dB에서 0.147→0.01), F3는 MMSE가 ZF보다 우세(예상된 방향)
  - F0/F2는 이번 범위 밖(AWGN 고정 유지)

- **PHY LLS — PRACH 추가 (UL 랜덤 접속, 프리앰블 검출 + TA 추정) (2026-07-14)**
  - `prach.c`/`prach.h`: 단일 함수 `run_prach_simulation()`. PRACH는 스펙상 본래
    목적이 초기 타이밍 획득(TA)이라, 단순 시퀀스 검출에서 그치지 않고 TA 추정까지
    포함 (사용자 확인)
  - ZC 루트시퀀스 `x_u(n)=exp(-j*pi*u*n*(n+1)/L_RA)` — L_RA(839 long/139 short)가
    둘 다 소수라 PUCCH의 길이-12 근사와 달리 **근사 없이 정확한 공식** 적용.
    프리앰블은 `x_u((n+v*N_CS) mod L_RA)` (문자 그대로의 인덱스 순환, TS 38.211
    6.3.3.1 정의 그대로)
  - 미지의 전파지연을 순환천이로 모델링(선형 컨볼루션/guard-time 버퍼 대신) —
    ZC의 이상적 순환 자기상관을 그대로 활용해, 프리앰블 인덱스와 TA를 **단일
    순환상관 스윕**(`s=0..L_RA-1`, O(L_RA²) 직접합산, `dft_precode.c`와 같은
    "FFT 미사용" 기조)으로 동시 추정: `v_hat=s_hat/N_CS`, `d_hat=s_hat mod N_CS`
  - 단순화(문서화됨): OFDM 그리드/CP/guard-time 없이 시퀀스 도메인 전용(SRS/PUCCH
    F0와 같은 기조), 단일 루트 시퀀스만 지원(스펙의 64프리앰블 다중 루트 채움은
    범위 밖), TA는 초 단위 환산 없이 샘플 단위로만 리포트
  - 부수 추가: `utils.c`에 `rand_uniform_int(n)` — 프리앰블/지연 추첨용 균등정수
    RNG (기존 시드 상태 재사용, libc `rand()`로 별도 스트림 만들지 않음)
  - 검증: SHORT(L_RA=139)/LONG(L_RA=839) 둘 다 크래시 없이 SNR 증가에 따라
    프리앰블 오검출률·TA MAE 단조 감소, `PRACH_MAX_DELAY_SAMPLES=0` 회귀 케이스에서
    고SNR로 갈수록 TA MAE→0 확인(저SNR에서의 잔류 오차는 노이즈로 인한 상관피크
    미세이탈이라는 물리적으로 타당한 현상), LONG 포맷에서 num_preambles=64로
    스펙의 표준 프리앰블 수와 우연히 일치

- **PHY LLS — MMSE+TDL+PUSCH precoding 비선형 등화(DFE) 탐색 (2026-07-14)**
  - `run_pusch_tdl_dfe_simulation()` (`pusch.c`, `PUSCH_DFE_ENABLE=1`): 기존
    `run_pusch_tdl_simulation()`의 MMSE+DFT-precoding 잔여 ISI(`alpha_d` RE별
    변동으로 IDFT 후 생기는 자기간섭, 함수 주석에 이미 유도됨)를 "노이즈에 얹기"
    대신 **실제로 계산해서 제거**하는 블록 병렬간섭제거(block PIC, 고전
    DFE(Proakis & Salehi Ch.10.3)의 순환컨볼루션용 비인과적 변형) 프로토타입
  - 핵심 수식: `y=IDFT(eq_data)=g⊛x+noise''`, `g=IDFT(alpha_d)`(대각-순환 항등식).
    1차 하드슬라이싱(`qam_demodulate`+`qam_modulate` 조합 재사용)으로 `x_hat0`
    획득 → `y_dfe[n]=y[n]-Σ_{m≠n}g[(n-m)%M]x_hat0[m]` → ISI 분산 항 제거된
    노이즈만으로 재복호. 같은 채널/노이즈 draw에 대해 DFE 유/무 BLER을 한 표에
    나란히 출력(공정 비교)
  - **실측 결과** (16QAM, MCS10, 20RB, TDL DS=300ns): 15dB에서는 DFE가 오히려
    악화(BLER 0.86→0.877, 1차 결정 오류의 전파가 이득보다 큼 — 고전 DFE의
    알려진 약점과 일치), 20~25dB에서는 소폭 개선(0.45→0.4433, 0.0667→0.0600),
    30dB 이상에서는 둘 다 무오류로 수렴. 즉 **이 채널·MCS 조합에서는 비선형
    등화의 이득이 크지 않고 SNR 문턱 이하에서 손해** — "가능성 탐색"의 실증
    결론 (300 trial 기준이라 통계적 변동 있음, 정밀 문턱값 특정은 아님)
  - ZF는 잔여 ISI가 없어(기존 함수 주석에 이미 증명) 이 실험 대상에서 제외,
    이 함수는 MMSE+TRANSFORM_PRECODING을 내부에서 강제

- **PHY LLS — Turbo 등화(반복적 소프트 등화-복호 교환) 확장 (2026-07-14)**
  - `run_pusch_tdl_turbo_simulation()` (`pusch.c`, `PUSCH_TURBO_ENABLE=1`):
    위 DFE의 하드 슬라이싱을 **LDPC의 소프트(extrinsic) 피드백**으로 대체 —
    Douillard et al. 1995 "Turbo Equalization" 개념, MMSE 소프트-PIC 등화기
    구조는 Tüchler·Koetter·Singer, *Turbo Equalization: Principles and New
    Results*, IEEE Trans. Commun. 2002 (Proakis & Salehi는 이 주제를 깊이
    다루지 않아 별도 인용 — 확인 안 된 내용 명시 원칙)
  - 신규 라이브러리 함수 2개: `ldpc_decode_soft()`(`ldpc.c`, belief-propagation
    변수노드 사후 LLR 전체 반환, 기존 `ldpc_decode()`는 이 함수의 래퍼로 리팩터링돼
    동작 동일 유지), `qam_soft_symbol()`(`modulation.c`, a priori 비트 LLR →
    심볼별 소프트 평균/분산, 기존 PAM 테이블 재사용이라 매핑 방식 무관하게 정확)
  - 반복 구조: a priori→소프트 심볼 평균/분산→(자기 자신 제외) soft-PIC로 ISI
    제거→per-symbol 노이즈분산 재계산→디코더 채널LLR로 직접 사용(PIC 구조상
    자기정보 재주입 없이 자동으로 extrinsic)→사후LLR-입력LLR=다음 라운드 a
    priori. 인터리버가 없다는 점(다른 함수들과 동일)을 한계로 명시
  - **정확성 검증**: `PUSCH_TURBO_ITERS=1`일 때 turbo 결과가 BER/BLER 소수점까지
    no-DFE와 **정확히 일치**함을 확인 — 우연이 아니라 수식적으로 증명 가능함
    (a priori=0이면 모든 심볼이 E[x]=0, Var[x]=1(균등분포)이 되고, Parseval
    항등식에 의해 어느 심볼을 기준으로 하든 `Σ_{k≠0}|g[k]|²=var_alpha`가 나와
    잔여노이즈식이 no-DFE의 `(mvar+var_alpha)/abar²`로 정확히 축소됨) — 구현
    검증용으로 함수 주석에도 기록
  - **실측 결과** (16QAM, MCS10, 20RB, TDL DS=300ns, 3회 반복): turbo가 거의
    전 SNR에서 no-DFE와 hard-DFE **둘 다**를 능가 — 특히 hard-DFE가 오류전파로
    악화됐던 15dB 구간에서도 turbo는 개선(BLER 0.900→0.883, hard-DFE는
    0.910으로 악화). 20dB(0.423→0.343), 25dB(0.053→0.030)에서도 일관되게
    최선. 소프트 반복 처리가 하드 DFE의 오류전파 약점을 실제로 완화한다는
    점을 실측으로 확인 — "가능성 탐색"에 대한 긍정적 결론(단, 300 trial
    기준 통계적 변동 있음, 인터리버 부재로 고전 문헌 대비 이득이 제한적일 수 있음)

- **PHY LLS — PUCCH F1/F3 + TDL을 HARQ와 결합 (2026-07-14)**
  - `run_pucch_format1_tdl_harq_simulation()`: F1(무코딩 ACK/NACK)의 반복+MRC
    옥카전을 재전송 루프로 감싸 attempt마다 채널 재드로우, 통계량(combined/weight)을
    attempt 간에도 누적. UCI에 채널코딩이 없어 IR/Chase(`HARQ_RV_SEQUENCE`) 구분이
    무의미함을 명시 — 매 attempt 동일 심볼 재전송이라 항상 Chase류
  - `run_pucch_format3_tdl_harq_simulation()`: F2/F3 UCI(K≤11)는 스펙상 별도 CRC가
    없어(기존 F2/F3 구현에서 이미 확인된 사실), PDSCH/PUSCH HARQ 함수들처럼
    CRC pass/fail로 재전송을 멈출 수 없음 — **genie(정답) 비트 일치**를 종료 판정
    기준으로 대체(실제 수신기에서는 불가능하지만, 이미 F1/F3 TDL에 적용 중인
    genie-aided CSI 철학과 일관). N=64 Polar mother codeword에 원래 LDPC HARQ용으로
    만든 `rate_matching.c`의 범용 circular-buffer(`rate_match_select`/`_combine`)를
    그대로 재사용 — `polar_init(..., E=N)`으로 호출해 스펙의 shortening/puncturing을
    끄고(E<N 조건 불만족 시 forced-zero 분기 비활성, `polar.c`에서 확인) 완전
    비단축 mother code로 취급, 기존 F2/F3의 spec-accurate shortening과는 다른
    HARQ 전용 경로
  - `main.c` PUCCH dispatch에 HARQ_ENABLE 분기 추가(TDL+HARQ 조합만; F0/F2는 기존과
    동일하게 AWGN 전용 유지)
  - 검증: F1은 저SNR(-22~-14dB)에서 BLER(HARQ)≪BLER(1st), AvgTx가 SNR 낮을수록 증가;
    F3는 ZF/MMSE 둘 다 정상 동작, MMSE가 ZF보다 우세(예상된 방향). F3 IR vs Chase는
    K=3처럼 코드가 이미 매우 강할 때는 통계적으로 구분 안 갈 만큼 코드가 저율(E≫N=64라
    한 occasion 안에서 이미 mother codeword 전체를 커버)이라 차이가 거의 없었으나,
    K=11(코드율 상승)에서는 IR이 Chase와 근소하게 다른(대체로 우위) 결과 확인 —
    기존 "IR이 Chase보다 우세" 경향과 일관
  - 이로써 CLAUDE.md의 "확정 순서" 1번(PUCCH F1/F3+TDL+HARQ) 완료 — 남은 다음
    단계는 PRACH의 OFDM 그리드 기반 정교화(아래 진행 예정 참조)

- **PHY LLS — PRACH를 OFDM 그리드(RE) + TDL 다경로로 정교화 (2026-07-14)**
  - `run_prach_tdl_simulation()` (`prach.c`, `PRACH_FORMAT`+`CHANNEL_MODEL=TDL`):
    기존 `run_prach_simulation()`의 ZC 시퀀스 도메인 순환시프트 지연 모델을
    유지하면서, 그 위에 `tdl.c`(기존 6탭 근사 PDP)를 재사용한 진짜 다경로
    페이딩을 얹음
  - 핵심 통찰: L_RA-포인트 순환시프트(기존 방식)는 DFT shift 정리에 의해
    tau = s/(L_RA·Δf_RA) 초의 연속시간 지연과 수학적으로 정확히 등가 —
    즉 기존 "샘플 도메인 트릭"이 사실 PRACH 자체 서브캐리어 간격(Δf_RA)의
    RE 그리드 상 지연의 물리적으로 정확한 주파수영역 표현이었음이 증명됨.
    따라서 지연/검출 알고리즘(순환상관 스윕 + N_CS zone 분해) 자체는 무변경,
    `tdl_channel_apply()`가 AWGN을 다경로 페이딩+노이즈로 대체하는 부분만 추가
  - PRACH 서브캐리어 간격 Δf_RA는 구현정의: LONG=1.25kHz 고정(TS 38.211
    Table 6.3.3.2-x의 두 옵션 중 하나, format 3의 5kHz는 범위 밖),
    SHORT=carrier SCS(`cfg->scsKHz`) 재사용(스펙의 15·2^μ kHz 계열과 일치)
  - 검증: TDL 버전은 SNR이 아무리 높아져도 검출률/TA MAE가 0으로 수렴하지
    않고 다경로 지연확산에 의한 바닥(noise floor)에 수렴함을 확인 —
    노이즈가 아니라 채널 자체가 한계인 물리적으로 타당한 현상(기존 AWGN
    전용 경로는 그대로 0으로 수렴, 회귀 없음 확인). SHORT가 LONG보다 TA
    MAE 바닥이 더 큼(SHORT의 Δf_RA=30kHz vs LONG의 1.25kHz라 샘플당
    시간간격이 훨씬 작아 같은 DS=300ns가 상대적으로 더 많은 샘플 스미어링을
    유발 — 수치적으로 타당)
  - 사용자와 사전 확인: "RE grid 매핑 + TDL 다경로 확장" 범위로 진행(대안이었던
    "지연이 CP 초과하는 시나리오"는 미채택). 이로써 CLAUDE.md의 "확정 순서"
    2번(PRACH OFDM 그리드 정교화)도 완료 — 다음 후속과제 없음, 새 항목은
    아래 진행 예정 참조

### 🏠 사이드 프로젝트
- **PHY Link Level Simulator (LLS) 자체 개발**
  - 개인 프로젝트로 지속 개발 중
  - **개발 환경**: WSL (Windows Subsystem for Linux) + Claude Code
  - **버전 관리**: Git
  - 목표: 5G NR PHY 알고리즘 검증 및 프로토타이핑 플랫폼 구축

### 🔄 진행 예정
- **PHY LLS 후속 과제**
  - PRACH 추가, PDSCH 내 TDL×MIMO×HARQ 조합, TDL→PUSCH/PUCCH(F1/F3) 확장,
    MMSE+TDL+PUSCH precoding 비선형(DFE) 등화 탐색, turbo 등화 확장 모두 완료 (위 참조)
  - 확정 순서(2026-07-14)의 두 항목 모두 완료: 1) PUCCH F1/F3+TDL을
    SIMO_MRC+TDL+HARQ처럼 HARQ와도 결합 2) PRACH를 실제 OFDM 그리드(RE)+TDL
    다경로 기반으로 정교화 (위 참조). 현재 후속 과제 없음 — 다음 작업은
    새로 논의 필요

- **NVIDIA Aerial SDK 연동 — 보류(사용자 명시적 결정, 2026-07-14): 연동하지 않기로 함**

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
| 2026-07-15 | PHY LLS: 전체 소스/헤더 파일(52개, `PHY/src/*.c` + `PHY/include/*.h`)에 박스형 파일 헤더 배너 추가 (파일명 + 한 줄 설명 + `Author: Inseok Kang`) |
| 2026-07-15 | git: `origin/develop`이 별도 세션/기기에서 `PHY/common`+`PHY/lls_sim`+`PHY/ber_sim` 구조로 재구조화된 채 갈라져 있던 것을 발견 — 기능 자체(PUCCH/PRACH/PUSCH/MIMO/HARQ 등)는 로컬 WSL 작업이 최신이라 판단해, 원격의 재구조화 히스토리는 `origin/archive/common-lls-sim-refactor` 브랜치로 보존하고 로컬 기준으로 `develop`을 force-push. 이후 세션은 이 저장소의 `PHY/src`/`PHY/include` 평면 구조가 기준임 |

---

*이 파일은 Claude와의 작업을 통해 지속적으로 업데이트됩니다.*
