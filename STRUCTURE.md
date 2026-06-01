# 5G PHY LLS — 코드 구조 및 설계 가이드

---

## 1. 디렉토리 구조

```
inseokiki/
├── PHY/                    ← LLS (Link Level Simulator)
│   ├── src/                ← C 소스
│   ├── include/            ← 헤더
│   ├── config/             ← 시뮬 설정 파일
│   ├── build/              ← 오브젝트 파일 (빌드 산출물)
│   ├── c_Makefile
│   └── lls_sim_c           ← 실행 바이너리
│
├── BER/                    ← 독립 BER 툴 (PHY 코드 미사용)
│   ├── src/
│   ├── include/
│   ├── config/
│   ├── Makefile
│   └── ber_sim             ← 실행 바이너리
│
└── 3gpp/                   ← 3GPP 표준 문서
```

### 빌드 방법

```bash
# LLS 시뮬레이터
cd PHY && make -f c_Makefile

# BER 툴
cd BER && make
```

---

## 2. PHY/ — LLS 모듈 구성

### 모듈별 역할

| 파일 | 역할 |
|------|------|
| `config_parser.c` | 설정 파일 파싱 + MCS 기반 파라미터 자동 결정 |
| `mcs_table.c` | 3GPP TS 38.214 MCS 테이블 3종 |
| `modulation.c` | QAM 변조 / 복조 / LLR 계산 |
| `channel.c` | AWGN / Flat Fading 채널 |
| `channel_estimation.c` | LS 추정 + 보간 + ZF / MMSE 등화 |
| `ofdm.c` | OFDM 변조 / 복조 (FFT/IFFT) |
| `dmrs.c` | DMRS 파일럿 시퀀스 및 위치 인덱스 |
| `crc.c` | CRC-24A / CRC-24C 생성 및 검사 |
| `ldpc.c` | LDPC 인코더 / 디코더 (데이터 채널) |
| `polar.c` | Polar 인코더 / 디코더 (제어 채널) |
| `polar_rate_match.c` | Polar Rate Matching / Dematching |
| `pbch.c` | PBCH 시뮬레이션 루프 |
| `pdcch.c` | PDCCH 시뮬레이션 루프 (Blind Decoding 포함) |
| `pdsch.c` | PDSCH 시뮬레이션 루프 (w/o DMRS, w/ DMRS) |
| `csi_rs.c` | CSI-RS 채널 추정 시뮬레이션 |
| `srs.c` | SRS 채널 사운딩 시뮬레이션 |
| `utils.c` | 난수 생성 등 공통 유틸 |
| `main.c` | 진입점 — 채널 타입별 분기 |

### 실행 흐름

```
main()
  └─ config_parser_load()
       └─ calc_derived()        ← MCS 룩업 → modulation, codeRate 자동 결정
                                ← 채널 타입별 코딩 방식 강제
  └─ 채널 타입 분기
       ├─ PDSCH  → run_pdsch_simulation()
       │           run_pdsch_dmrs_simulation()
       ├─ PBCH   → run_pbch_simulation()
       ├─ PDCCH  → run_pdcch_simulation()
       ├─ CSIRS  → run_csirs_simulation()
       ├─ SRS    → run_srs_simulation()
       └─ NONE   → run_legacy_sim()
```

---

## 3. 설정 파라미터 설계

### 핵심 원칙: MCS가 단일 진입점

`MODULATION`, `CODE_RATE`를 config에 직접 쓰지 않는다.  
`MCS_INDEX` + `MCS_TABLE`만 지정하면 나머지는 자동 결정된다.

```
config 파일
  MCS_INDEX = 10
  MCS_TABLE = TABLE1
       │
       ▼ (calc_derived 내부)
  get_mcs_entry(10, TABLE1)
       │
       ├─ cfg->modulation = "16QAM"
       ├─ cfg->codeRate   = 0.3320
       └─ spectralEff     = 1.328 bits/RE
```

### 채널 타입별 코딩 방식 자동 강제

| 채널 | 코딩 | 변조 | 근거 |
|------|------|------|------|
| PBCH | POLAR (강제) | QPSK (강제) | TS 38.212 §7.3.3 |
| PDCCH | POLAR (강제) | QPSK (강제) | TS 38.212 §7.3.2 |
| PDSCH | LDPC (강제) | MCS 테이블 | TS 38.212 §7.2 |
| NONE / 기타 | config 유지 | MCS 테이블 | — |

### MCS 테이블 3종 (TS 38.214)

| 테이블 | 표준 참조 | 변조 범위 | 용도 |
|--------|-----------|-----------|------|
| TABLE1 | Table 5.1.3.1-1 | QPSK ~ 64QAM (MCS 0~28) | 기본 |
| TABLE2 | Table 5.1.3.1-2 | QPSK ~ 256QAM (MCS 0~27) | 고효율 |
| TABLE3 | Table 5.1.3.1-3 | QPSK ~ 64QAM (MCS 0~28) | 저SE (RedCap 등) |

---

## 4. 데이터 전달 구조

### 4-1. 설정 데이터 — 구조체 값 복사 후 const 포인터 전달

```c
/* config_parser.h */
typedef struct {
    int    bandwidthMHz;
    int    mcsIndex;
    char   modulation[64];
    double codeRate;
    char   physicalChannel[64];
    ...
} L1Config;

/* 사용 패턴 */
ConfigParser parser;
config_parser_load(&parser, "sim_config.txt"); // &parser: 쓰기 포인터
L1Config cfg = config_parser_get(&parser);     // 값 복사 (이후 독립)

run_pdsch_simulation(&cfg);   // const L1Config * → 읽기 전용 전달
```

**설계 의도:**
- 시뮬 함수는 설정을 바꾸면 안 되므로 `const` 강제
- 값 복사로 parser 변경이 cfg에 영향 없음

### 4-2. 심볼 버퍼 — in / out 포인터 쌍 분리

```c
typedef struct { double re, im; } cx_t;

/* 호출자가 버퍼 소유, 함수는 채우기만 */
void qam_modulate(const int  *bits,   // 읽기 전용 입력
                  int         nbits,
                  const char *mod,
                  cx_t       *syms);  // 쓰기 출력 (호출자 버퍼)

void awgn_add_noise(const AWGNChannel *ch,
                    const cx_t *in,   // 읽기 전용
                    int n, int nfft,
                    cx_t       *out); // 쓰기 출력
```

**버퍼 소유권:**
```
run_pdsch_simulation()      ← malloc / free 책임
    │
    ├─ qam_modulate   (... syms)      // syms  빌려줌
    ├─ awgn_add_noise (... rxsyms)    // rxsyms 빌려줌
    └─ ldpc_decode    (... decoded)   // decoded 빌려줌
```

### 4-3. 리소스 그리드 — 1D 평탄 배열 + 인덱스 배열

```c
cx_t *tx_grid  = malloc(active * sizeof(cx_t)); // active = num_rb * 12
int  *pilot_pos = malloc(num_pilots * sizeof(int));
int  *data_pos  = malloc(num_data   * sizeof(int));

dmrs_pilot_indices(num_rb, pilot_pos); // 파일럿 RE 위치 인덱스
dmrs_data_indices (num_rb, data_pos);  // 데이터 RE 위치 인덱스

// 그리드 배치
for (int p = 0; p < num_pilots; p++)
    tx_grid[pilot_pos[p]] = dmrs_sym[p];

// 채널 통과 후 파일럿 RE만 추출
for (int p = 0; p < num_pilots; p++)
    rx_pilots[p] = rx_grid[pilot_pos[p]];
```

**설계 의도:**
- 2D(RE × 심볼)를 1D로 펼쳐 인덱스 계산 단순화
- 위치 정보(인덱스 배열)와 데이터(심볼 배열) 분리 → 재사용 가능

### 4-4. 코덱 상태 — 구조체 + init / free 패턴

```c
typedef struct {
    int    info_size;
    int    coded_size;
    double codeRate;
    /* 내부 패리티 행렬 (동적 할당) */
} LDPCCodec;

/* 생성 → 사용 → 해제 */
LDPCCodec ldpc;
ldpc_init  (&ldpc, K, codeRate);      // 내부 행렬 malloc
ldpc_encode(&ldpc, input, output);    // ldpc 읽기 전용 참조
ldpc_decode(&ldpc, llr, 25, decoded);
ldpc_free  (&ldpc);                   // 내부 malloc 해제
```

동일 패턴: `PolarCodec`, `OFDMCtx`, `AWGNChannel`, `FlatFadingChannel`

**C에서 객체지향 흉내:**
```
C++ class  →  C struct + init() + free()
생성자     →  ldpc_init()
소멸자     →  ldpc_free()
멤버 함수  →  ldpc_encode(&ldpc, ...)
```

---

## 5. 변수 스코프 원칙

### 전역 변수 없음

| 데이터 종류 | 위치 | 이유 |
|-------------|------|------|
| 시뮬 버퍼 (bits, syms, llr) | 함수 로컬 `malloc` | 함수마다 크기 다름, 독립성 필요 |
| 설정값 (L1Config) | 값 복사 후 `const *` 전달 | 읽기 전용, 여러 함수 공유 |
| 코덱 상태 (LDPCCodec 등) | 로컬 구조체 | 초기화 비용 크지만 함수별 독립 |
| 노이즈 상태 (`randn` spare) | `static` 로컬 | 호출 간 상태 필요, 외부 노출 불필요 |
| **전역 변수** | **없음** | 데이터 오염, 확장성 저하 |

### 버퍼 생애주기 패턴

```c
void run_xxx_simulation(const L1Config *cfg) {

    /* ① 시뮬 전체에서 한 번만 malloc */
    cx_t *syms = malloc(nsym * sizeof(cx_t));
    ...

    /* ② SNR 루프 — 버퍼 재사용, 내용만 덮어씀 */
    for (double snr = cfg->snrStart; ...) {
        for (int trial = 0; trial < cfg->numTrials; trial++) {
            qam_modulate(..., syms);   // syms 덮어씀
            ...
        }
    }

    /* ③ 함수 끝에서 반드시 해제 */
    free(syms);
}
```

**trial마다 malloc/free 하지 않는 이유:**
- 동적 할당은 OS 시스템 콜 → 수만 번 반복 시 병목
- 버퍼 크기가 trial마다 같으므로 재사용이 안전

---

## 6. BER/ — 독립 툴 설계

### 모듈 구성

```
BER/
├── src/main.c    ← 시뮬 루프 + 경량 config 파서 (인라인)
├── src/qam.c     ← Gray-coded QAM 변조/복조 + 이론 BER
└── src/awgn.c    ← Box-Muller AWGN 생성
```

PHY/ 코드에 의존하지 않고 3개 파일로 자급자족.

### QAM 정규화 (단위 평균 심볼 전력)

```
k비트 PAM 정규화 계수 = sqrt(2 * (4^k - 1) / 3)

  k=1 (QPSK)   : sqrt(2)    심볼 ±1/√2 per component
  k=2 (16QAM)  : sqrt(10)   ±1/√10, ±3/√10
  k=3 (64QAM)  : sqrt(42)   ±1/√42 ... ±7/√42
  k=4 (256QAM) : sqrt(170)  ±1/√170 ... ±15/√170
```

### Eb/N0 → 잡음 분산 변환

```
Es/N0 = Qm × Eb/N0                    (선형)
sigma = sqrt(1 / (2 × Qm × Eb/N0))   (성분당 표준편차)
```

단위 전력 심볼 기준이므로 Es = 1.

### 이론 BER (Proakis & Salehi 5th Ed. Eq.5-2-79)

```
BER ≈ (4/Qm) × (1 - 1/√M) × Q(√(3·Qm/(M-1) · Eb/N0))
Q(x) = 0.5 × erfc(x / √2)
```

QPSK(M=4): 정확한 값 / 16·64·256QAM: tight 근사

---

## 7. 전체 TX→RX 데이터 흐름

```
[설정]
  L1Config (구조체 — const 포인터로 전달)

[TX]
  int[]   bits
    └─ qam_modulate()  →  cx_t[]  syms
    └─ 리소스 그리드 배치  →  cx_t[]  tx_grid

[채널]
  cx_t[]  tx_grid
    └─ awgn_add_noise()  →  cx_t[]  rx_grid

[RX]
  cx_t[]  rx_grid
    └─ ls_estimate()      →  cx_t[]  h_est     (채널 추정)
    └─ interpolate()      →  cx_t[]  h_full
    └─ mmse_equalize()    →  cx_t[]  eq_data   (등화)
    └─ qam_demap_llr()    →  double[] llr      (소프트 비트)
    └─ ldpc_decode()      →  int[]   decoded   (복호)

[집계]
  BER = bit_errors / total_bits
  BLER = block_errors / num_trials
```

각 단계는 `const in*` → `out*` 포인터 쌍으로 연결.  
버퍼는 SNR 루프 밖에서 한 번만 `malloc`, trial 반복마다 재사용.

---

*이 문서는 코드 리뷰 및 구조 파악을 위한 참조 문서입니다.*
