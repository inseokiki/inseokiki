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
| `modulation.c` | QAM 변조 / 복조 / LLR 계산. `qam_soft_symbol()`은 a priori 비트 LLR → 심볼별 소프트 평균/분산(turbo 등화용), `qam_demap_llr`과 같은 PAM 테이블을 재사용해 매핑 방식과 무관하게 정확 |
| `channel.c` | AWGN / Flat Fading 채널 |
| `channel_estimation.c` | LS 추정 + 보간 + ZF / MMSE 등화 |
| `mimo.c` | SU-MIMO 2x2/4x4 채널(block-flat Rayleigh) + MRC / ZF / MMSE 검출. 검출 함수(`mimo_zf_detect`/`mimo_mmse_detect`/`mrc_combine`)는 RE 단위 순수 함수라 flat이든 TDL 등 frequency-selective든 그대로 재사용됨. `mimo_apply_tx_correlation_4x4(h, rho)`는 4x4 채널에 Tx 공간상관(Kronecker, XPOL 2x2 블록별 지수상관, 편파 간은 비상관) 적용 — `SPATIAL_CORR_TX`(MIMO_MODE=CL_4PORT 전용)로 제어. 편파 간 완전 독립 가정 때문에 동일편파 상관만으로는 rank-2(교차편파 다이버시티 변형)가 계속 유리해 rank-1 선택률이 ρ→1에서도 낮게 유지됨(실측 확인, 2026-08-02) |
| `rate_matching.c` | Circular buffer rate matching (RV 기반 k0 오프셋) + HARQ 소프트 컴바이닝 |
| `tdl.c` | TDL 주파수 선택적 페이딩 (근사 6탭 NLOS PDP, TS 38.901 표 근사치). `tdl_draw()`를 Tx-Rx 안테나 쌍마다 독립 호출하면 MIMO 공간축으로 그대로 확장 가능 (`pdsch.c`의 TDL+MIMO 조합 함수들 참조) |
| `dft_precode.c` | PUSCH Transform Precoding용 유니터리 M-point DFT/IDFT (O(M²) 직접합산) |
| `pusch.c` | PUSCH(상향) 시뮬레이션 — DFT-s-OFDM/CP-OFDM 토글, PDSCH+DMRS 체인 재사용. `run_pusch_tdl_dfe_simulation()`은 MMSE+TDL의 잔여 ISI(`alpha_d` RE별 변동)를 블록 병렬간섭제거(DFE)로 실제 제거해보는 연구용 프로토타입(PUSCH_DFE_ENABLE=1) — BLER(noDFE) vs BLER(DFE)를 같은 채널/노이즈 draw로 나란히 비교. 저SNR에서는 오류전파로 악화, 중~고SNR(약 20dB대)에서 소폭 개선, 매우 높은 SNR에서는 둘 다 무오류로 수렴(실측 확인, STRUCTURE 하단 참고). `run_pusch_tdl_turbo_simulation()`(PUSCH_TURBO_ENABLE=1)은 하드 DFE를 소프트 PIC + `ldpc_decode_soft()` extrinsic 반복교환으로 일반화 — BLER(noDFE)/BLER(hardDFE)/BLER(turbo) 3열 비교, 실측상 하드 DFE가 손해보던 SNR 구간까지 포함해 전 구간에서 turbo가 우세함을 확인 |
| `pucch_seq.c` | PUCCH F0/F1용 저PAPR base sequence(ZC 근사) + 순환시프트 |
| `pucch.c` | PUCCH(상향 제어) F0~F3 — F0/F1 시퀀스 검출, F2/F3 Polar 대체 코딩. F1/F3는 TDL 변형도 있음(genie-aided CSI — PUCCH는 DMRS/LS 추정 파이프라인이 없어 `tdl_channel_apply()`의 `h_out`을 그대로 완벽 채널로 사용); F0/F2는 AWGN 전용. F1/F3 TDL은 HARQ 결합 버전도 있음(재전송마다 채널 재드로우, PUCCH UCI는 스펙상 CRC가 없어 genie 정답 비트 일치를 종료 판정 기준으로 대체) — F3는 N=64 Polar mother codeword에 `rate_matching.c`의 범용 circular-buffer(원래 LDPC용)를 그대로 재사용해 IR/Chase RV 결합 |
| `ofdm.c` | OFDM 변조 / 복조 (FFT/IFFT) |
| `dmrs.c` | DMRS 파일럿 시퀀스 및 위치 인덱스 |
| `crc.c` | CRC-24A / CRC-24C 생성 및 검사 |
| `ldpc.c` | LDPC 인코더 / 디코더 (데이터 채널). `ldpc_decode_soft()`가 belief-propagation 변수노드 사후 LLR 전체(coded_size)를 반환 — turbo 등화의 extrinsic 계산용(`ldpc_decode()`는 이 함수의 얇은 래퍼로 리팩터링됨, 동작 동일) |
| `polar.c` | Polar 인코더 / 디코더 (제어 채널). `polar_init(N,K,E)`가 E를 받아 rate-matching shortening 위치를 강제-frozen 처리 (TS 38.212 5.4.1.1, 2026-07-13 버그 수정 — 이전엔 shortening 위치가 info bit와 겹쳐 파괴될 수 있었음) |
| `polar_rate_match.c` | Polar Rate Matching / Dematching. `polar_interleaver()`는 `polar.c`의 frozen-bit 계산과 공유하기 위해 공개 함수로 노출됨 |
| `pbch.c` | PBCH 시뮬레이션 루프. `run_pbch_fading_simulation()`(CHANNEL_MODEL=FLAT_FADING/TDL)은 PUCCH처럼 DMRS 파이프라인이 없어 genie-aided CSI 사용 — FLAT_FADING은 전체 SSB에 단일 탭, TDL은 심볼 인덱스를 RE로 취급(SCS=SCS_KHZ). `qam_demap_llr_mmse()`는 반드시 `mmse_equalize()` 결과(이미 등화된 신호)를 입력받아야 함(원시 rx 아님) — 이 규약을 지키지 않으면 위상 보정이 안 돼 SNR과 무관한 BLER floor가 생김(2026-08-02 구현 중 발견·수정) |
| `pdcch.c` | PDCCH 시뮬레이션 루프 (Blind Decoding 포함). `run_pdcch_fading_simulation()`은 실제 송신된 후보(TX candidate)만 페이딩 채널을 통과시키고, 나머지 blind-decoding 후보들은 기존과 동일하게 순수 노이즈 드로우 유지(실제 송신과 무관하므로 채널이 의미 없음) — ZF/MMSE 등화 후 TX 후보 전용 스칼라 유효 노이즈분산(`run_pucch_format3_tdl_simulation()`과 같은 평균화 단순화)을 계산해 기존 `qam_demap_llr()` 경로 그대로 재사용 |
| `pdsch.c` | PDSCH 시뮬레이션 루프 (w/o DMRS, w/ DMRS) |
| `csi_rs.c` | CSI-RS 채널 추정 시뮬레이션 |
| `srs.c` | SRS 채널 사운딩 시뮬레이션 |
| `prach.c` | PRACH(UL 랜덤 접속) — ZC 루트시퀀스(L_RA=839/139, 둘 다 소수라 정확한 공식) + 사이클릭시프트 프리앰블 + 순환상관 스윕으로 프리앰블 검출과 TA(샘플 단위) 동시 추정. SRS/PUCCH F0처럼 OFDM 그리드 없이 시퀀스 도메인 전용, 단일 루트만 지원. TDL 변형(`run_prach_tdl_simulation()`)도 있음 — 같은 ZC 샘플을 PRACH 자체 서브캐리어 간격(Δf_RA, LONG=1.25kHz 고정/SHORT=carrier SCS 재사용)의 RE 값으로 재해석하고 `tdl.c`를 재사용해 다경로 페이딩을 추가; DFT shift 정리에 의해 기존 순환시프트가 정확히 연속시간 지연과 등가임을 이용해 검출 알고리즘 자체는 무변경 |
| `utils.c` | 난수 생성 등 공통 유틸. `rand_uniform_int(n)`은 PRACH의 프리앰블/지연 추첨용으로 추가된 균등정수 RNG (기존 시드 상태 재사용) |
| `main.c` | 진입점 — 채널 타입별 분기 |

### 실행 흐름

```
main()
  └─ config_parser_load()
       └─ calc_derived()        ← MCS 룩업 → modulation, codeRate 자동 결정
                                ← 채널 타입별 코딩 방식 강제
  └─ 채널 타입 분기
       ├─ PDSCH  → run_pdsch_simulation()                  ← USE_DMRS=0
       │           run_pdsch_sm4x4_harq_simulation()        ← USE_DMRS=1, HARQ_ENABLE=1, MIMO_MODE=SM_4X4 (4x4 SM+HARQ, flat/TDL 공용, genie-aided CSI)
       │           run_pdsch_cl_4port_harq_simulation()     ← USE_DMRS=1, HARQ_ENABLE=1, MIMO_MODE=CL_4PORT (4포트 CL+HARQ, wideband PMI 고정, genie-aided CSI)
       │           run_pdsch_sm2x2_tdl_harq_simulation()    ← USE_DMRS=1, HARQ_ENABLE=1, MIMO_MODE=SM_2X2, CHANNEL_MODEL=TDL (2x2+TDL+IR/Chase)
       │           run_pdsch_simo_mrc_tdl_harq_simulation() ← USE_DMRS=1, HARQ_ENABLE=1, MIMO_MODE=SIMO_MRC, CHANNEL_MODEL=TDL (1x2 MRC+TDL+IR/Chase)
       │           run_pdsch_harq_simulation()              ← USE_DMRS=1, HARQ_ENABLE=1, 그 외 (SISO, flat 또는 TDL 무관)
       │           run_pdsch_sm4x4_simulation()             ← MIMO_MODE=SM_4X4, CHANNEL_MODEL≠TDL (4x4, MMSE, genie-aided CSI)
       │           run_pdsch_sm4x4_tdl_simulation()         ← MIMO_MODE=SM_4X4, CHANNEL_MODEL=TDL (4x4, MMSE, 주파수선택적, genie-aided CSI)
       │           run_pdsch_cl_4port_simulation()          ← MIMO_MODE=CL_4PORT, CHANNEL_MODEL≠TDL (4포트 CL, RI+PMI 적응, genie-aided CSI)
       │           run_pdsch_cl_4port_tdl_simulation()      ← MIMO_MODE=CL_4PORT, CHANNEL_MODEL=TDL (4포트 CL+TDL, wideband PMI, genie-aided CSI)
       │           run_pdsch_simo_mrc_simulation()          ← MIMO_MODE=SIMO_MRC, CHANNEL_MODEL≠TDL (1x2, MRC)
       │           run_pdsch_simo_mrc_tdl_simulation()      ← MIMO_MODE=SIMO_MRC, CHANNEL_MODEL=TDL (1x2, MRC, 주파수선택적)
       │           run_pdsch_sm2x2_simulation()             ← MIMO_MODE=SM_2X2, CHANNEL_MODEL≠TDL (2x2, ZF/MMSE)
       │           run_pdsch_sm2x2_tdl_simulation()         ← MIMO_MODE=SM_2X2, CHANNEL_MODEL=TDL (2x2, ZF/MMSE, 주파수선택적)
       │           run_pdsch_tdl_simulation()               ← MIMO_MODE=SISO, CHANNEL_MODEL=TDL
       │           run_pdsch_dmrs_simulation()              ← MIMO_MODE=SISO, CHANNEL_MODEL≠TDL (기본)
       ├─ PBCH   → run_pbch_simulation()             ← CHANNEL_MODEL=AWGN
       │           run_pbch_fading_simulation()      ← CHANNEL_MODEL=FLAT_FADING/TDL (genie-aided CSI)
       ├─ PDCCH  → run_pdcch_simulation()             ← CHANNEL_MODEL=AWGN
       │           run_pdcch_fading_simulation()      ← CHANNEL_MODEL=FLAT_FADING/TDL (TX 후보만 genie-aided 페이딩)
       ├─ CSIRS  → run_csirs_simulation()
       ├─ SRS    → run_srs_simulation()
       ├─ PUSCH  → run_pusch_harq_simulation()          ← HARQ_ENABLE=1 (AWGN/FLAT_FADING/TDL 공용, LS 채널 추정, DFT-s-OFDM 토글)
       │           run_pusch_simulation()              ← HARQ 비활성, CHANNEL_MODEL≠TDL, TRANSFORM_PRECODING로 DFT-s-OFDM/CP-OFDM 토글
       │           run_pusch_tdl_simulation()          ← HARQ 비활성, CHANNEL_MODEL=TDL, TURBO/DFE 둘 다 비활성 (ZF/MMSE 둘 다 지원)
       │           run_pusch_tdl_dfe_simulation()      ← HARQ 비활성, CHANNEL_MODEL=TDL, PUSCH_DFE_ENABLE=1 (MMSE+DFE 연구 프로토타입)
       │           run_pusch_tdl_turbo_simulation()    ← HARQ 비활성, CHANNEL_MODEL=TDL, PUSCH_TURBO_ENABLE=1 (DFE_ENABLE보다 우선, MMSE+turbo 연구 프로토타입)
       ├─ PUCCH  → run_pucch_format0_simulation()      ← PUCCH_FORMAT=0 (시퀀스 검출, AWGN 전용)
       │           run_pucch_format1_simulation()      ← PUCCH_FORMAT=1, CHANNEL_MODEL≠TDL (시퀀스+반복결합)
       │           run_pucch_format1_tdl_simulation()  ← PUCCH_FORMAT=1, CHANNEL_MODEL=TDL (반복마다 독립 채널, genie-aided MRC)
       │           run_pucch_format2_simulation()      ← PUCCH_FORMAT=2 (Polar, 짧음, AWGN 전용)
       │           run_pucch_format3_simulation()      ← PUCCH_FORMAT=3, CHANNEL_MODEL≠TDL (Polar+DFT-s-OFDM, 김)
       │           run_pucch_format3_tdl_simulation()  ← PUCCH_FORMAT=3, CHANNEL_MODEL=TDL (genie-aided ZF/MMSE)
       │           run_pucch_format1_tdl_harq_simulation() ← PUCCH_FORMAT=1, CHANNEL_MODEL=TDL, HARQ_ENABLE=1 (재전송마다 반복 옥카전 재드로우 후 누적, 무코딩이라 IR/Chase 무의미, genie bit-match로 종료 판정)
       │           run_pucch_format3_tdl_harq_simulation() ← PUCCH_FORMAT=3, CHANNEL_MODEL=TDL, HARQ_ENABLE=1 (N=64 Polar mother codeword + rate_matching.c 범용 circular buffer IR/Chase, genie bit-match로 종료 판정)
       ├─ PRACH  → run_prach_simulation()               ← PRACH_FORMAT=LONG/SHORT, CHANNEL_MODEL≠TDL (프리앰블 검출+TA 추정)
       │           run_prach_tdl_simulation()           ← PRACH_FORMAT=LONG/SHORT, CHANNEL_MODEL=TDL (RE grid + 다경로 페이딩)
       └─ NONE   → run_legacy_sim()
```

PUSCH/PUCCH 분기는 2026-07-14까지 `main.c`에 `#include`조차 없어 이 함수들
전부가 config로 도달 불가능했다 (PDSCH에서 있었던 것과 같은 종류의 배선 누락,
같은 날 함께 수정).

PDSCH 분기는 `main.c`에서 `USE_DMRS`/`HARQ_ENABLE`/`MIMO_MODE`/`CHANNEL_MODEL`
값으로 실제 라우팅된다 (2026-07-13까지는 이 배선이 빠져 있어 `run_pdsch_dmrs_simulation()`
외의 PDSCH 함수들이 config로 도달 불가능한 상태였음). `HARQ_ENABLE=1`일 때 우선순위:
SM_4X4 → `run_pdsch_sm4x4_harq_simulation()` (채널 모델 무관, genie-aided CSI),
CL_4PORT → `run_pdsch_cl_4port_harq_simulation()` (채널 모델 무관, genie-aided CSI),
SM_2X2+TDL → `run_pdsch_sm2x2_tdl_harq_simulation()`,
SIMO_MRC+TDL → `run_pdsch_simo_mrc_tdl_harq_simulation()`,
그 외(SISO 등) → `run_pdsch_harq_simulation()`.
PUSCH는 `HARQ_ENABLE=1`이면 채널 모델/파형 설정에 무관하게 `run_pusch_harq_simulation()`이 우선.

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
