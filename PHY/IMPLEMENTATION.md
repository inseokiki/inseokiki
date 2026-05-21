# 5G NR PHY Link Level Simulator — 구현 상세 문서

> 3GPP TS 38.211 / 38.212 / 38.213 / 38.214 기반  
> 작성 기준: 현재 구현 완료 기능 전체

---

## 목차

1. [프로젝트 구조](#1-프로젝트-구조)
2. [빌드 및 실행](#2-빌드-및-실행)
3. [전체 신호 처리 흐름](#3-전체-신호-처리-흐름)
4. [구현된 컴포넌트 상세](#4-구현된-컴포넌트-상세)
   - 4.1 [OFDM](#41-ofdm)
   - 4.2 [변조 (QAM)](#42-변조-qam)
   - 4.3 [CRC](#43-crc)
   - 4.4 [LDPC 코딩](#44-ldpc-코딩)
   - 4.5 [Polar 코딩](#45-polar-코딩)
   - 4.6 [채널 모델](#46-채널-모델)
   - 4.7 [DMRS 및 채널 추정](#47-dmrs-및-채널-추정)
   - 4.8 [등화기 (ZF / MMSE)](#48-등화기-zf--mmse)
   - 4.9 [MCS 테이블](#49-mcs-테이블)
   - 4.10 [물리 채널 (PBCH / PDCCH / PDSCH)](#410-물리-채널-pbch--pdcch--pdsch)
5. [시뮬레이션 모드](#5-시뮬레이션-모드)
6. [설정 파일 파라미터](#6-설정-파일-파라미터)
7. [DMRS + 채널 추정 시뮬레이션 결과 분석](#7-dmrs--채널-추정-시뮬레이션-결과-분석)
8. [향후 개발 방향](#8-향후-개발-방향)
9. [3GPP 참조 표준](#9-3gpp-참조-표준)

---

## 1. 프로젝트 구조

```
Deveolp/
├── CMakeLists.txt
├── build.sh                    # 빌드 스크립트 (g++ -std=c++17)
├── config/
│   └── sim_config.txt          # 시뮬레이션 파라미터 설정 파일
├── include/                    # 헤더 파일
│   ├── channel.h               # AWGN / Flat Rayleigh 채널
│   ├── channel_estimation.h    # LS 추정 / 선형 보간 / ZF·MMSE 등화
│   ├── config.h                # 열거형 정의 (SCS, Bandwidth 등)
│   ├── config_parser.h         # L1Config 구조체 + 파서
│   ├── crc.h                   # CRC-24A / CRC-24C / CRC-16
│   ├── dmrs.h                  # Gold 시퀀스 + DMRS Type 1
│   ├── ldpc.h                  # LDPC Encoder / BP Decoder
│   ├── mcs_table.h             # TS 38.214 MCS 테이블
│   ├── modulation.h            # QPSK~256QAM 변조·복조·LLR
│   ├── ofdm.h                  # IFFT/FFT + Cyclic Prefix
│   ├── pbch.h                  # PBCH 시뮬레이션
│   ├── pdcch.h                 # PDCCH 시뮬레이션
│   ├── pdsch.h                 # PDSCH 시뮬레이션 (일반 + DMRS)
│   ├── polar.h                 # Polar Encoder / SC Decoder
│   ├── polar_rate_match.h      # Polar Rate Matching
│   └── utils.h                 # Complex 타입 / 난수 생성
└── src/                        # 구현 파일
    ├── channel.cpp
    ├── channel_estimation.cpp
    ├── config_parser.cpp
    ├── crc.cpp
    ├── dmrs.cpp
    ├── ldpc.cpp
    ├── main.cpp
    ├── mcs_table.cpp
    ├── modulation.cpp
    ├── ofdm.cpp
    ├── pbch.cpp
    ├── pdcch.cpp
    ├── pdsch.cpp
    ├── polar.cpp
    ├── polar_rate_match.cpp
    └── utils.cpp
```

---

## 2. 빌드 및 실행

### 빌드

```bash
cd Deveolp
./build.sh
# 또는 직접
g++ -std=c++17 -O2 -Wall -o lls_sim src/*.cpp -I include
```

### 실행

```bash
./lls_sim                          # config/sim_config.txt 사용
./lls_sim config/my_config.txt     # 커스텀 설정 파일 사용
```

### 빠른 테스트 예시

```bash
# AWGN + DMRS + MMSE 등화 (20 MHz, QPSK, MCS5)
cat > /tmp/quick_test.txt << 'EOF'
BANDWIDTH_MHZ = 20
SCS_KHZ = 30
CHANNEL_MODEL = AWGN
PHYSICAL_CHANNEL = PDSCH
MCS_INDEX = 5
MCS_TABLE = TABLE1
NUM_TRIALS = 500
SNR_START = 0
SNR_END = 16
SNR_STEP = 2
USE_DMRS = 1
EQUALIZER = MMSE
EOF
./lls_sim /tmp/quick_test.txt
```

---

## 3. 전체 신호 처리 흐름

### 3.1 Legacy 모드 (PHYSICAL_CHANNEL = NONE)

```
[TX]
랜덤 비트 생성
    → LDPC/Polar 인코딩
    → QAM 변조 (QPSK~256QAM)
    → OFDM 변조 (IFFT + CP 추가)
    → AWGN 채널

[RX]
    → OFDM 복조 (CP 제거 + FFT)
    → QAM 연판정 (LLR)
    → LDPC/Polar 디코딩
    → BER / BLER 계산
```

### 3.2 PDSCH 모드 (USE_DMRS = 0)

```
[TX]
TB 비트 (tbSize bits)
    → CRC-24A 부착 (K = tbSize + 24)
    → LDPC 인코딩 (N = K / R bits)
    → QAM 변조
    → AWGN 채널 (직접 심볼 전송, OFDM 없음)

[RX]
    → QAM 연판정 LLR
    → LDPC 디코딩 (Belief Propagation, 25회 반복)
    → CRC 검증
    → BER / BLER 계산
```

### 3.3 PDSCH + DMRS 모드 (USE_DMRS = 1)

```
[TX]
TB 비트
    → CRC-24A 부착
    → LDPC 인코딩
    → QAM 변조 → 데이터 심볼 (numRB×6개)
    → 리소스 그리드 구성
         짝수 서브캐리어 (RE 0,2,4,...,10/RB) : DMRS 파일럿
         홀수 서브캐리어 (RE 1,3,5,...,11/RB) : 데이터
    → 채널 적용
         AWGN:          y[k] = x[k] + n[k]
         Flat Rayleigh: y[k] = h·x[k] + n[k],  h ~ CN(0,1)

[RX]
    → 파일럿 RE 추출
    → LS 채널 추정:       h_est[k] = y_pilot[k] / x_pilot[k]
    → 선형 주파수 보간:   h_full[k] (전체 서브캐리어)
    → 데이터 RE 추출
    → 등화 (ZF 또는 MMSE)
    → 연판정 LLR 계산
    → LDPC 디코딩
    → CRC 검증
    → BER / BLER 계산
```

---

## 4. 구현된 컴포넌트 상세

### 4.1 OFDM

**파일**: `include/ofdm.h`, `src/ofdm.cpp`  
**참조**: TS 38.211 Section 5.3

#### 동작 원리

OFDM은 주파수 영역 심볼을 시간 영역 파형으로 변환하고, Cyclic Prefix(CP)를 삽입해 ISI를 방지합니다.

```
변조 (modulate):
  주파수 영역 심볼 X[k]
    → IFFT: x[n] = (1/N) Σ X[k] · exp(j2πkn/N)
    → CP 추가: 마지막 L개 샘플을 앞에 복사

복조 (demodulate):
  수신 신호
    → CP 제거 (앞 L개 샘플 삭제)
    → FFT: X[k] = Σ x[n] · exp(-j2πkn/N)
```

#### CP 길이 (TS 38.211 Table 5.3.1-1)

| 심볼 위치 | CP 길이 (NFFT=2048 기준) |
|---|---|
| 심볼 0, 7 (슬롯당) | 160 샘플 (first CP) |
| 심볼 1~6, 8~13 | 144 샘플 (normal CP) |
| 임의 NFFT | `CP = CP_base × NFFT / 2048` |

**현재 구현**: DFT/IDFT 직접 계산 O(N²) — 시뮬레이션용으로 적합, 대규모 처리에는 FFTW 권장

---

### 4.2 변조 (QAM)

**파일**: `include/modulation.h`, `src/modulation.cpp`  
**참조**: TS 38.211 Section 5.1

#### 지원 변조 방식

| 변조 | bps (Qm) | 정규화 계수 |
|---|---|---|
| QPSK | 2 | 1/√2 |
| 16QAM | 4 | 1/√10 |
| 64QAM | 6 | 1/√42 |
| 256QAM | 8 | 1/√170 |

#### 성상도 매핑 (3GPP 재귀 공식)

```
I 성분 예시 (비트 b0, b2, b4, ...):
  QPSK:   I = (1 - 2·b0) / √2
  16QAM:  I = (1 - 2·b0) · [2 - (1 - 2·b2)] / √10
  64QAM:  I = (1 - 2·b0) · [4 - (1 - 2·b2)·(2 - (1 - 2·b4))] / √42
```

Q 성분은 홀수 인덱스 비트(b1, b3, b5, ...)로 동일한 방식 계산.  
성상도는 Gray 코딩 기반으로 인접 심볼 간 1비트 차이만 발생.

#### 연판정 복조 (Max-Log LLR)

I/Q 분리 가능한 정방 QAM의 특성을 활용해 각 비트 위치별 LLR을 독립 계산:

```
LLR(bk) = (2/σ²) × [min_{s:bk=1} dist² - min_{s:bk=0} dist²]
```

- `dist²` = 수신 심볼과 PAM 레벨 간 거리 제곱
- `σ²` = 잡음 분산 (= N₀ = 1/SNR_linear)

---

### 4.3 CRC

**파일**: `include/crc.h`, `src/crc.cpp`  
**참조**: TS 38.212 Section 5.1

#### 지원 타입

| CRC 타입 | 다항식 | 사용처 |
|---|---|---|
| CRC-24A | 0x864CFB | TB (Transport Block) |
| CRC-24C | 0xB2B117 | Code Block |
| CRC-16 | 0x1021 | 소형 블록 |

#### RNTI 스크램블링

PDCCH에서는 CRC 마지막 16비트에 RNTI를 XOR하여 특정 단말에게만 복호 가능:

```
CRC_scrambled[i] = CRC[i] XOR RNTI_bit[i]    (i = 8..23)
```

---

### 4.4 LDPC 코딩

**파일**: `include/ldpc.h`, `src/ldpc.cpp`  
**참조**: TS 38.212 Section 5.3.2

#### 패리티 검사 행렬 구성

준순환(Quasi-Cyclic) 구조로 단순화된 코드:

- **정보 비트 연결**: 각 정보 비트 i → 검사 노드 `(3i+j) mod N_check` (j=0,1,2)
- **패리티 연결**: 대각선 구조 (시스테마틱 코드)
- 가변 노드 차수(dv) = 3, 검사 노드 차수(dc) ≈ 2~3

#### BP (Belief Propagation) 디코더

Sum-Product 알고리즘, 최대 25회 반복:

```
초기화:
  q(v→c) = LLR_channel[v]    (채널 LLR로 초기화)

검사 노드 업데이트 (C-step):
  q(c→v) = 2·atanh(∏_{v'≠v} tanh(q(v'→c)/2))

가변 노드 업데이트 (V-step):
  q(v→c) = LLR[v] + Σ_{c'≠c} q(c'→v)

판정:
  decoded[v] = (q(v) < 0) ? 1 : 0
```

#### 성능 최적화 (O(N²) → O(N·dv))

이전 구현의 가변 노드 업데이트는 O(N×M×dc) = **O(N²)** 의 복잡도로 대규모 블록에서 매우 느렸음.

해결: 구성 시 전치 인접 목록 `Ht_` 사전 구축:

```cpp
// 구성 시 (1회):
Ht_[v].push_back({checkIdx, posInCheck});

// 디코딩 시 (매 반복):
for (const auto& link : Ht_[v]) {   // O(dv) ≈ O(3)
    sum += checkToVar[link.checkIdx][link.posInCheck];
}
```

**속도 향상**: 약 3,600배 (6,600 비트 블록 기준)

---

### 4.5 Polar 코딩

**파일**: `include/polar.h`, `src/polar.cpp`, `include/polar_rate_match.h`, `src/polar_rate_match.cpp`  
**참조**: TS 38.212 Section 5.3.1

#### 구현 내용

- **인코더**: 재귀적 커널 F = [[1,0],[1,1]] 행렬 적용
- **Frozen 비트 선택**: Bhattacharyya 파라미터 기반 (신뢰도 낮은 채널 위치를 0으로 고정)
- **디코더**: Successive Cancellation (SC) 알고리즘
- **Rate Matching**: TS 38.212 Table 5.4.1.1-1 기반 sub-block interleaving

#### 사용처

- PBCH, PDCCH: 제어 채널 코딩 (소형 블록)
- 일반 코딩 모드에서 `CODING = POLAR` 선택 시

---

### 4.6 채널 모델

**파일**: `include/channel.h`, `src/channel.cpp`

#### AWGN 채널

잡음 전력 계산 방식:

```
OFDM 모드 (nfft > 0):
  σ² = 1 / (NFFT × SNR_linear)
  → 서브캐리어당 SNR = Es/N₀

비-OFDM 모드 (nfft = 0):
  σ² = signal_power / SNR_linear
  → 신호 전력 직접 측정
```

각 복소 잡음 샘플:
```
n = N(0, σ²/2) + j·N(0, σ²/2)
```

#### Flat Rayleigh 페이딩 채널

채널 계수 h는 매 trial(OFDM 심볼)마다 독립적으로 샘플링:

```
h ~ CN(0, 1)   →   h = N(0, 1/√2) + j·N(0, 1/√2)

y[k] = h · x[k] + n[k]
     (k = 0, ..., 서브캐리어 수-1)
```

**특징**:
- 동일한 h가 심볼 내 모든 서브캐리어에 적용 (Block Flat Fading)
- |h|² ~ Exp(1) → 깊은 페이딩(deep fade) 발생 확률 항상 존재
- 잡음 전력: σ² = 1/SNR_linear (단위 전력 심볼 가정)

---

### 4.7 DMRS 및 채널 추정

**파일**: `include/dmrs.h`, `src/dmrs.cpp`, `include/channel_estimation.h`, `src/channel_estimation.cpp`  
**참조**: TS 38.211 Section 7.4.1.1, Section 5.2.1

#### Gold 시퀀스 생성기

3GPP TS 38.211 Section 5.2.1 표준 구현:

```
레지스터: x1 (x^31+x^3+1), x2 (x^31+x^3+x^2+x+1) — 31비트 LFSR

초기화:
  x1[0] = 1, x1[1..30] = 0
  x2[0..30] = cInit (31비트)

Nc = 1600회 사전 전진 (초기 상태 의존성 제거)

출력:
  c[i] = (x1[i] XOR x2[i])   (i = 0, 1, 2, ...)
```

#### DMRS 시퀀스

```
r[n] = (1/√2) · [(1 - 2c[2n]) + j·(1 - 2c[2n+1])]
```

- QPSK 성상도와 동일한 단위 전력 (|r[n]|² = 1)
- `cInit` = 0x12345678 (시뮬레이션 고정값, 실제 5G에서는 셀 ID·슬롯·심볼 번호로 결정)

#### DMRS Type 1 리소스 배치

```
활성 서브캐리어 (numRB × 12개) 내 배치:

RB당 배치:
  파일럿 RE: k = 0, 2, 4, 6, 8, 10  (짝수 서브캐리어, 6개/RB)
  데이터 RE: k = 1, 3, 5, 7, 9, 11  (홀수 서브캐리어, 6개/RB)

전체:
  파일럿: 6 × numRB개
  데이터: 6 × numRB개
  오버헤드: 50% (파일럿 : 데이터 = 1:1)
```

> 실제 5G에서는 파일럿 오버헤드 약 14% (1개 심볼 / 14개 심볼), 본 구현은 1개 심볼 내 배치만 고려

#### LS(Least Squares) 채널 추정

파일럿 위치에서 수신 심볼을 알려진 DMRS 심볼로 나눔:

```
h_LS[k] = y_pilot[k] / x_pilot[k]
         = (h·x_pilot[k] + n[k]) / x_pilot[k]
         = h + n[k]/x_pilot[k]
```

추정 오차 분산: `σ²_est = N₀ / |x_pilot[k]|² = N₀` (단위 전력 파일럿이므로)

#### 선형 주파수 보간

파일럿 위치의 추정값을 전체 서브캐리어로 보간:

```
파일럿 위치 kL, kR 사이의 데이터 서브캐리어 k에 대해:
  α = (k - kL) / (kR - kL)
  h_full[k] = h_est[kL] · (1-α) + h_est[kR] · α
```

- 경계 외 서브캐리어: hold (외삽 없음)
- 이진 탐색 사용 → O(N log P) 복잡도

---

### 4.8 등화기 (ZF / MMSE)

**파일**: `src/channel_estimation.cpp`, `src/modulation.cpp`

#### Zero-Forcing (ZF) 등화

채널을 완전히 역필터링:

```
ŷ[k] = y[k] / h_est[k] = x[k] + n[k]/h_est[k]

유효 잡음 분산: σ²_ZF = N₀ / |h_est[k]|²
```

**문제**: 깊은 페이딩(|h|→0)에서 잡음 증폭 → 불안정한 LLR 생성

```cpp
// 구현: 작은 |h|^2 클램핑
if (hPow < 1e-10) eq[k] = rxData[k];   // 나눗셈 방지
else              eq[k] = rxData[k] / hData[k];
```

#### MMSE (Minimum Mean Square Error) 등화

SNR 정보를 활용해 잡음 증폭을 억제:

```
MMSE 필터:  w[k] = h*[k] / (|h[k]|² + N₀)

출력 (바이어스 포함):
  y_eq[k] = w[k] · y[k]
           = α[k]·x[k] + (h*[k]/(|h[k]|²+N₀))·n[k]

바이어스:  α[k] = |h[k]|² / (|h[k]|² + N₀)
유효 잡음 분산: σ²_eff[k] = N₀·|h[k]|² / (|h[k]|²+N₀)²
```

**깊은 페이딩 처리 비교**:

| | h → 0일 때 동작 |
|---|---|
| ZF | `y/h → ∞` → 랜덤 부호의 LLR → 디코더 오류 증가 |
| MMSE | `y_eq → 0` → 모든 성상점이 원점으로 수렴 → LLR → 0 (소거, 디코더에 무해) |

#### MMSE-aware LLR 계산

바이어스 α를 반영한 성상도 스케일링:

```
LLR(bk) = (2/σ²_eff) × [min_{s:bk=1} |y_eq - α·s|² - min_{s:bk=0} |y_eq - α·s|²]
```

- ZF LLR과 수식적으로 동치이지만, 깊은 페이딩에서 수치적으로 안정
- `|h|² < 1e-10`인 서브캐리어는 LLR = 0 처리 (소거 심볼)

---

### 4.9 MCS 테이블

**파일**: `include/mcs_table.h`, `src/mcs_table.cpp`  
**참조**: TS 38.214 Table 5.1.3.1-1, 5.1.3.1-2

#### Table 1 (최대 64QAM, MCS 0~28)

| MCS | 변조 | 목표 부호율 (R×1024) | 스펙트럼 효율 (bits/RE) |
|---|---|---|---|
| 0~9 | QPSK | 120~679 | 0.234~1.327 |
| 10~16 | 16QAM | 340~658 | 1.328~2.570 |
| 17~28 | 64QAM | 438~948 | 2.566~5.555 |

#### Table 2 (최대 256QAM, MCS 0~27)

| MCS | 변조 | 목표 부호율 (R×1024) |
|---|---|---|
| 0~4 | QPSK | 120~602 |
| 5~10 | 16QAM | 378~658 |
| 11~19 | 64QAM | 466~873 |
| 20~27 | 256QAM | 682~948 |

실제 부호율: `R = targetCodeRate / 1024.0`

---

### 4.10 물리 채널 (PBCH / PDCCH / PDSCH)

**파일**: `src/pbch.cpp`, `src/pdcch.cpp`, `src/pdsch.cpp`

#### PBCH (Physical Broadcast Channel)

- **역할**: 마스터 정보 블록(MIB) 전송
- **코딩**: Polar (24비트 페이로드 + CRC-24C)
- **변조**: QPSK
- **시뮬레이션 출력**: BER/BLER vs SNR

#### PDCCH (Physical Downlink Control Channel)

- **역할**: DCI(Downlink Control Information) 스케줄링 정보 전송
- **코딩**: Polar + Rate Matching
- **변조**: QPSK
- **RNTI 스크램블링**: CRC에 RNTI XOR 적용
- **집성 레벨(AL)**: 1, 2, 4, 8, 16 CCE (설정 가능)
- **탐색 공간**: CSS (Common) / USS (UE-Specific)

#### PDSCH (Physical Downlink Shared Channel)

- **역할**: 데이터 전송 (UDP/TCP 페이로드)
- **코딩**: LDPC (BG1: A>3824 또는 R>0.67 / BG2: 나머지)
- **변조**: QPSK~256QAM (MCS 테이블로 결정)
- **CRC**: CRC-24A
- **TB 최대 크기**: 8424 bits (단일 코드 블록, CB 분할 미구현)

---

## 5. 시뮬레이션 모드

`PHYSICAL_CHANNEL`과 `USE_DMRS` 조합에 따라 동작 모드 결정:

| PHYSICAL_CHANNEL | USE_DMRS | 동작 |
|---|---|---|
| NONE | - | Legacy: OFDM + QAM + LDPC/Polar, AWGN |
| PBCH | - | PBCH BER/BLER vs SNR |
| PDCCH | - | PDCCH BER/BLER vs SNR |
| PDSCH | 0 | PDSCH + LDPC + CRC, AWGN, MCS 기반 |
| PDSCH | 1 | **PDSCH + DMRS + 채널 추정 + 등화 (ZF/MMSE)** |

---

## 6. 설정 파일 파라미터

`config/sim_config.txt` 전체 파라미터 목록:

```ini
# ── 무선 파라미터 ──────────────────────────────────────────
BANDWIDTH_MHZ = 100      # 대역폭 (5/10/15/20/25/30/40/50/60/80/100)
SCS_KHZ       = 30       # 서브캐리어 간격 (15/30/60/120)
NUM_RB        = 0        # 리소스 블록 수 (0=자동, BW+SCS 조합으로 결정)
NFFT          = 0        # FFT 크기 (0=자동)
CP_LENGTH_FIRST  = 0     # 첫 심볼 CP 길이 샘플 수 (0=자동)
CP_LENGTH_NORMAL = 0     # 일반 심볼 CP 길이 샘플 수 (0=자동)

# ── 변조·코딩 (Legacy/PBCH/PDCCH 모드용) ──────────────────
MODULATION = QPSK        # QPSK / 16QAM / 64QAM / 256QAM
CODING     = LDPC        # NONE / LDPC / POLAR
CODE_RATE  = 0.5         # 부호율

# ── 시뮬레이션 설정 ────────────────────────────────────────
NUM_OFDM_SYMBOLS = 50    # Legacy 모드 심볼 수
SNR_START = 0            # SNR 범위 시작 (dB)
SNR_END   = 25           # SNR 범위 끝 (dB)
SNR_STEP  = 3            # SNR 증가 간격 (dB)
NUM_TRIALS = 1000        # SNR 포인트당 반복 횟수

# ── 채널 모델 ──────────────────────────────────────────────
CHANNEL_MODEL = AWGN     # AWGN / FLAT_FADING

# ── 물리 채널 ──────────────────────────────────────────────
PHYSICAL_CHANNEL = PDSCH # NONE / PBCH / PDCCH / PDSCH
MCS_INDEX = 10           # MCS 인덱스
MCS_TABLE = TABLE1       # TABLE1 (64QAM) / TABLE2 (256QAM)
TB_SIZE   = 0            # TB 크기 bits (0=자동)

# PDCCH 전용
DCI_SIZE     = 39        # DCI 페이로드 크기 (bits)
RNTI         = 0x1234    # 단말 식별자
SEARCH_SPACE = CSS       # CSS / USS
PDCCH_AL     = 4         # 집성 레벨 (1/2/4/8/16)

# ── DMRS + 채널 추정 ───────────────────────────────────────
USE_DMRS  = 1            # 0=비활성화 / 1=활성화
EQUALIZER = MMSE         # ZF / MMSE
```

### 자동 계산 파라미터

| 파라미터 | 자동 계산 공식 |
|---|---|
| NUM_RB | TS 38.101 NRB 테이블 (BW, SCS 조합) |
| NFFT | NUM_RB × 12보다 큰 최솟값 (2의 거듭제곱) |
| CP_LENGTH_FIRST | 160 × NFFT / 2048 |
| CP_LENGTH_NORMAL | 144 × NFFT / 2048 |
| Sampling Rate | NFFT × SCS_KHZ × 1000 |

---

## 7. DMRS + 채널 추정 시뮬레이션 결과 분석

### 7.1 AWGN 채널에서 DMRS 동작 검증

설정: BW=20MHz, SCS=30kHz, MCS=5 (QPSK, R=0.37), 500 trials

```
채널: AWGN (H=1), 등화기: MMSE
   SNR (dB)            BER           BLER
------------------------------------------
         0.0     1.62e-01         1.0000
         4.0     4.11e-02         1.0000
         8.0     1.03e-02         0.9080   ← 워터폴 진입
        10.0     3.88e-03         0.5820
        12.0     4.69e-04         0.1020
        14.0     1.77e-05         0.0040   ← 거의 완벽
        16.0     0.00e+00         0.0000   ✓
```

DMRS 없는 순수 AWGN PDSCH 대비 **약 6~8dB 추가 SNR 필요**.

추가 SNR의 원인:
1. **50% 파일럿 오버헤드**: 서브캐리어 절반이 데이터가 아닌 파일럿 → 실질 코딩 이득 감소
2. **채널 추정 오차**: `h_est = h + n_pilot/x_pilot` → 등화 후 유효 잡음 ×2 (약 3dB)

### 7.2 Flat Rayleigh 채널 결과

설정: 동일 조건, CHANNEL_MODEL = FLAT_FADING

```
채널: Flat Rayleigh, 등화기: MMSE
   SNR (dB)            BER           BLER
------------------------------------------
         6.0     1.02e-01         0.9060
        12.0     3.22e-02         0.4860
        18.0     5.73e-03         0.1640
        24.0     1.82e-03         0.0420
        30.0     1.23e-03         0.0140   ← 여전히 1.4%
```

### 7.3 Rayleigh 채널이 느리게 수렴하는 이유

**다이버시티 차수 = 1**이기 때문:

```
AWGN에서  BER ∝ exp(-SNR)   → 급격한 waterfall
Rayleigh에서 BER ∝ 1/SNR    → 완만한 감소
```

Rayleigh 페이딩에서 `P(|h|² < ε) = 1 - exp(-ε) ≈ ε`:
- 낮은 확률이지만 항상 deep fade 발생 가능
- deep fade에서는 채널이 사실상 정보를 지움 → 코딩 이득으로 극복 불가

### 7.4 MMSE vs ZF 비교

동일 조건 (Flat Rayleigh, MCS=10, 16QAM):

| SNR (dB) | ZF BLER | MMSE BLER | 차이 |
|---|---|---|---|
| 18 | 0.556 | 0.526 | -0.030 |
| 21 | 0.334 | 0.323 | -0.011 |
| 24 | 0.182 | 0.206 | — |

**차이가 작은 이유**: Flat Fading에서는 모든 서브캐리어의 h가 동일하므로 ZF와 MMSE의 LLR이 수식적으로 거의 동치. MMSE의 실질 이득은 **주파수 선택성 페이딩**에서 서브캐리어별 h가 달라질 때 뚜렷하게 나타남.

### 7.5 실제 5G에서 Rayleigh 성능 개선 방법

현재 시뮬레이터는 Single TX / Single RX / No Retransmission 환경:

| 기법 | 다이버시티 차수 | 효과 |
|---|---|---|
| **HARQ Combining** | 최대 전송 횟수 | 재전송 시 코딩 이득 누적 |
| **2Rx 안테나 (MRC)** | 2 | BER ∝ 1/SNR² |
| **4Rx 안테나** | 4 | BER ∝ 1/SNR⁴ |
| **TDL 채널** | 주파수 탭 수 | 광대역 내 주파수 다이버시티 |
| **주파수 호핑** | 주파수 채널 수 | 독립 페이딩 채널 다중 활용 |

---

## 8. 향후 개발 방향

우선순위 순:

### 8.1 TDL/CDL 주파수 선택성 채널 (TS 38.901)

현재 Flat Fading → TDL-A/B/C 다중 탭 채널로 확장:

```
y(t) = Σ h_l(t) · x(t - τ_l) + n(t)
       l=0..L-1
```

주파수 응답: `H[k] = Σ h_l · exp(-j2πkτ_l/T_s)` → 서브캐리어별 독립 페이딩

MMSE 등화의 이득이 ZF 대비 현저히 커짐.

### 8.2 TB 분할 (Transport Block Segmentation)

현재 최대 TB = 8424 bits (단일 CB). 실제 5G에서는 대형 TB를 여러 Code Block으로 분할:
- 각 CB에 CRC-24B 부착
- 분할 기준: A > 3824 → 최대 8448 bits/CB

### 8.3 LDPC Rate Matching (TS 38.212 Section 5.4.2)

현재 단순 패딩 → 표준 Rate Matching:
- Circular buffer
- Puncturing / Repetition
- Redundancy Version (RV=0,1,2,3)

### 8.4 HARQ (Hybrid ARQ)

- Chase Combining: 동일 전송 반복 후 MRC 합산
- Incremental Redundancy: RV 변경으로 다른 패리티 비트 전송

### 8.5 FFTW 연동

현재 O(N²) DFT → FFTW3 라이브러리로 O(N log N):

```cpp
// CMakeLists.txt에 추가:
find_package(FFTW3 REQUIRED)
target_link_libraries(lls_sim fftw3)
```

### 8.6 MIMO (Multiple Input Multiple Output)

2×2 또는 4×4 안테나:
- 공간 다중화 (Spatial Multiplexing): 처리량 증가
- 다이버시티 전송 (SFBC, Alamouti): 신뢰도 향상
- MRC 수신 합산

---

## 9. 3GPP 참조 표준

| 표준 | 내용 | 구현 항목 |
|---|---|---|
| **TS 38.211** | Physical channels and modulation | OFDM CP, QAM 성상도, DMRS Type 1 시퀀스 |
| **TS 38.212** | Multiplexing and channel coding | LDPC BG1/BG2, Polar, CRC, Rate Matching |
| **TS 38.213** | Physical layer procedures for control | PDCCH 탐색 공간 |
| **TS 38.214** | Physical layer procedures for data | MCS Table 1/2, PDSCH 처리 |
| **TS 38.101** | User Equipment radio transmission | NRB 테이블 (BW × SCS) |
| **TS 38.901** | Channel models | TDL/CDL 페이딩 (미구현) |
