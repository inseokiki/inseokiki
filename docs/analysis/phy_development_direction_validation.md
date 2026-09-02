# PHY 개발 방향 타당성 검증 및 개선 요청

> 작성일: 2026-09-02  
> 대상: `inseokiki/PHY`  
> 목적: 현재 PHY Link-Level Simulator의 기술적 완성도와 개발 방향을 검토하고, 향후 작업의 우선순위를 결정하기 위한 Claude 작업 지시서

---

## 1. Claude에게 요청하는 작업

이 문서를 출발점으로 현재 코드를 직접 다시 확인한 뒤 다음을 수행한다.

1. 아래 지적 사항이 실제 코드에 근거한 것인지 재검증한다.
2. 추가 결함이나 잘못된 가정이 있으면 파일·라인·근거와 함께 보완한다.
3. 신규 MIMO/Beam Management 기능 추가보다 기반 정확성 개선이 먼저인지 판단한다.
4. 수정에 착수하기 전, 단계별 구현 계획과 검증 방법을 사용자에게 제시한다.
5. 사용자의 명시적 승인 전에는 대규모 리팩터링이나 코딩 체인 교체를 수행하지 않는다.
6. 기술 판단은 가능한 한 3GPP TS 38.211, 38.212, 38.213, 38.214 및 TR 38.901에 근거한다.

주의: 현재 작업트리에 약 1만 줄 규모의 미커밋 변경과 신규 파일이 존재한다. 기존 변경은 사용자 작업이므로 삭제·초기화·덮어쓰지 않는다.

---

## 2. 요약 결론

현재 PHY의 개발 방향은 **5G PHY 알고리즘 학습 및 상대 비교용 샌드박스**로는 타당하다. 그러나 **3GPP NR 준거 Link-Level Simulator**로 보기에는 기반 계층의 정확성이 부족하다.

잠정 평가:

| 평가 항목 | 점수 | 판단 |
|---|---:|---|
| 알고리즘 프로토타이핑 | 7/10 | 다양한 알고리즘을 빠르게 비교하는 용도로 유효 |
| 구조적 확장성 | 4/10 | 시뮬레이션 함수 복제와 대형 파일로 변경 위험 증가 |
| NR 표준 적합성 | 3/10 | 코딩, rate matching, Polar, TDL 등이 표준과 다름 |
| BLER/MCS 결과 신뢰성 | 2~3/10 | 절대 성능이나 NR 임계값으로 해석하기 어려움 |

따라서 당분간 OLLA, Massive MIMO, MU-MIMO, Beam Management의 기능 폭을 넓히기보다 다음 기반을 먼저 정비하는 것이 권장된다.

- 채널 코딩 정확성
- 표준 rate matching
- 자원 매핑과 TBS 계산
- 표준 채널 모델
- 정량 검증 체계
- 공통 simulation pipeline

---

## 3. 확인된 주요 문제

### P0-1. 현재 LDPC는 3GPP NR LDPC가 아님

관련 코드:

- `PHY/src/ldpc.c:12-80`
- `PHY/src/pdsch.c:38-45`
- `PHY/src/pdsch.c:164-172`

현재 `build_H()`는 정보 비트의 column weight를 3으로 정하고 modulo 방식으로 자체 parity-check matrix를 생성한다. `ldpc_init()`은 목표 code rate로부터 parity bit 수를 직접 계산한다.

현재 구현에 없는 주요 NR 절차:

- BG1/BG2 선택
- lifting size `Zc` 선택
- QC-LDPC base graph 확장
- code-block segmentation
- code-block CRC
- filler/null bit 처리
- 표준 puncturing/repetition
- 표준 rate matching과 code-block concatenation

실제 NR에서는 MCS target code rate로 임의 parity-check matrix를 새로 만드는 것이 아니라, 표준 LDPC mother code를 생성하고 rate matching으로 전송 비트 수 `E`를 맞춘다.

영향:

- 현재 PDSCH/PUSCH BLER는 NR LDPC 성능이 아니다.
- MCS별 threshold와 OLLA convergence point를 NR 결과로 해석할 수 없다.
- 서로 다른 알고리즘을 동일 surrogate codec에서 상대 비교하는 용도로만 제한적으로 사용할 수 있다.

표준 근거:

- 3GPP TS 38.212 §5.2: CRC
- 3GPP TS 38.212 §5.3.2: LDPC coding
- 3GPP TS 38.212 §5.4.2: LDPC rate matching
- 3GPP TS 38.212 §6.2, §7.2: UL-SCH/DL-SCH processing

### P0-2. LDPC BP decoder의 메시지 표현이 잘못되었을 가능성이 높음

관련 코드:

- `PHY/src/ldpc.c:103-146`

현재 `v2c`는 edge 수가 아니라 variable 수인 `nv`만큼 할당된다. variable-node update에서 각 variable의 전체 posterior를 계산한 뒤 이를 연결된 모든 check node가 동일하게 다시 읽는다.

정상적인 belief propagation에서는 edge별 메시지가 필요하다.

```math
q_{v\rightarrow c}
= L_{\mathrm{ch},v}
+ \sum_{c'\in N(v)\setminus c} r_{c'\rightarrow v}
```

즉, 목적 check node `c`에서 온 메시지는 `q(v→c)` 계산에서 제외되어야 한다. 현재 구조는 이 제외가 없어 self-feedback을 발생시킬 수 있다.

추가 문제:

- syndrome 기반 early stopping 없음
- decoder 반환값이 항상 0
- parity-check 만족 여부를 decoder 내부에서 검증하지 않음
- numerical clipping 정책에 대한 시험 없음
- 메모리 할당 실패 처리 없음

Claude는 이 부분을 독립적인 작은 parity-check matrix와 hand-calculated BP iteration으로 먼저 검증한다.

### P0-3. HARQ rate matching이 표준 방식이 아님

관련 코드:

- `PHY/include/rate_matching.h:10-27`
- `PHY/src/rate_matching.c`

현재 구현은 RV별 시작점을 circular buffer의 균등 1/4 지점으로 정한다.

```text
k0 = round(rv / 4 * Ncb)
```

NR의 `k0`는 BG와 `Zc`, `Ncb`, RV에 따라 결정된다. filler bit skip 및 bit selection도 표준 절차를 따라야 한다.

영향:

- IR/Chase 성능 차이
- HARQ BLER gain
- 평균 전송 횟수
- OLLA와 HARQ의 결합 결과

위 수치들은 실제 NR HARQ 성능으로 볼 수 없다.

### P0-4. Polar coding이 NR bit-exact 구현이 아님

관련 코드:

- `PHY/src/polar.c:21-78`
- `PHY/src/polar.c:109-153`
- `PHY/src/polar_rate_match.c`

현재 reliability order는 Bhattacharyya approximation으로 실행 시 생성한다. NR은 TS 38.212에서 정의하는 reliability sequence와 channel-specific interleaving/rate-matching 규칙을 사용한다.

또한 decoder는 단순 SC decoder다. 학습용으로는 가능하지만 실제 PBCH/PDCCH 성능 기준선으로는 제한적이다.

확인할 항목:

- 표준 reliability sequence 적용 여부
- CRC attachment와 RNTI scrambling
- input/interleaving 규칙
- puncturing, shortening, repetition
- PBCH/PDCCH별 `n_max`, `I_IL`, `n_PC`, `I_BIL`
- CRC-aided SCL 도입 필요성

### P1-1. TDL 채널이 TR 38.901의 TDL-A/B/C/D/E가 아님

관련 코드:

- `PHY/src/tdl.c:9-26`
- `PHY/include/tdl.h`

현재 구현은 6개 고정 tap과 자체 delay/power profile을 사용한다. 이는 TR 38.901의 정확한 TDL profile이 아니다.

필요 개선:

- TDL-A/B/C/D/E profile 선택
- LOS/NLOS 구분
- RMS delay spread scaling
- Doppler 및 시간 상관
- MIMO spatial correlation 또는 CDL 연계 여부 결정
- channel normalization 검증

### P1-2. 일부 massive-MIMO/beam-management TDL 모델이 빔 선택 문제를 지나치게 단순화함

관련 코드:

- `PHY/src/pdsch.c:7493-7512`
- `PHY/src/pusch.c`의 UL Eigen-BF TDL 경로

일부 경로는 다음과 같은 형태를 사용한다.

```math
H[k] = g[k]H_{\mathrm{spatial}}
```

모든 안테나와 후보 빔에 동일한 scalar `g[k]`가 곱해지므로 주파수에 따라 빔 순위가 변하지 않는다. 이 모델은 고정 공간 signature에서 frequency-selective scalar fading만 평가할 때는 일관적이다.

하지만 다음 연구에는 부적합하다.

- multipath angular spread
- frequency-dependent best beam
- beam squint
- wideband/subband beam selection
- channel aging과 beam failure

따라서 결과 표기에서 `shared-cluster scalar TDL approximation`임을 명확히 밝히거나, 목적에 맞는 clustered spatial channel을 별도 구현해야 한다.

### P1-3. 주요 물리채널 경로가 실제 time-domain OFDM을 통과하지 않음

관련 코드:

- `PHY/src/main.c:76-165`
- `PHY/src/ofdm.c`

`ofdm_modulate()`와 `ofdm_demodulate()`는 주로 legacy simulation에서만 호출된다. PDSCH/PUSCH의 많은 경로는 RE-domain에서 직접 다음 계산을 사용한다.

```math
y[k] = H[k]x[k] + n[k]
```

RE-domain LLS 자체는 타당한 단순화다. 다만 다음을 검증하지 않는다는 한계를 문서와 출력에 명시해야 한다.

- CP와 delay spread 관계
- time-domain convolution
- ICI/ISI
- CFO와 phase noise
- timing offset
- waveform windowing
- PRACH guard time와 실제 waveform
- RF impairment

권장 명칭은 `RE-domain abstract LLS` 또는 이에 준하는 표현이다.

### P1-4. TBS와 자원 할당이 표준 절차와 다름

관련 예:

- `PHY/src/pdsch.c:164-172`
- 여러 `run_pdsch_*`, `run_pusch_*` 함수의 `tbsz = max_dbits * cr` 계산

현재 다수 경로가 사용 가능한 data bit 수에 code rate를 곱해 TBS를 정한다. 이는 TS 38.214의 TBS determination 절차와 다르다.

검토할 항목:

- 할당 PRB 수
- OFDM symbol 수
- DMRS RE overhead
- PTRS/CSI-RS/예약 RE 제외
- number of layers
- scaling factor
- `N_info` quantization
- small/large TBS 분기

MCS와 BLER를 연구하려면 표준 TBS 계산이 필수다.

---

## 4. 테스트 및 품질 상태

> **2026-09-02 재검증 시 갱신**: 이 문서 작성 이후 같은 날 UL Eigen-BF가
> K=4(최대 4레이어)로 확장되어 아래 수치가 바뀌었다 — `bash
> regression_test.sh`는 현재 **78/78** 통과(신규 케이스 4건 추가),
> `PHY/src/pusch.c`는 현재 약 **4,060줄**(K=4 확장분 반영), `run_*`
> 시뮬레이션 함수는 현재 총 **68개**. compiler warning 5건(아래 목록)은
> 재검증 시점에도 동일하게 재현됨 — 나머지 최초 점검 결과는 그대로 둔다.

2026-09-02 점검 결과:

- `make -f c_Makefile`: 성공
- `bash regression_test.sh`: 74/74 통과
- `make -B -f c_Makefile`: 성공, compiler warning 5건

발생한 경고:

- `PHY/src/pdsch.c`: `total_err_1st` 미사용 3건
- `PHY/src/pucch.c`: `total_err_1st` 미사용 2건

현재 회귀 테스트의 주된 검증 범위:

- 잘못된 config가 거부되는지 확인
- 각 dispatch 경로가 crash 없이 실행되는지 확인
- 출력에 특정 문자열이 존재하는지 확인

제한점:

- 기본 trial 수가 대부분 20
- BER/BLER 값에 대한 assertion 없음
- 이론값 또는 reference implementation 비교 없음
- noise-free bit-exact round-trip 검증 없음
- sanitizer/valgrind 시험 없음
- deterministic seed 기반 재현성 시험 없음

따라서 현재 74개 테스트는 **dispatch/smoke regression**으로 보는 것이 정확하다. 성능 정확성을 증명하지는 않는다.

---

## 5. 긍정적으로 평가되는 부분

다음 설계와 개발 습관은 유지할 가치가 있다.

- config validation 및 미지원 조합 차단
- 과거 silent wrong-dispatch를 찾아 회귀 테스트로 방지한 접근
- Genie와 estimated CSI/P1 경로를 분리한 비교 구조
- 코드북 전력 normalization 검증
- Gram determinant catastrophic cancellation 방어
- MIMO detector와 channel estimator의 비교적 독립적인 함수화
- 구현 정의와 근사 모델을 코드 및 문서에 기록하는 습관
- 물리적 불변조건과 극단값으로 버그를 찾는 방식

기존 MIMO, codebook, beamforming, channel-estimation 코드를 폐기할 필요는 없다. 신뢰 가능한 coding/channel/pipeline 아래에 재배치하는 방향이 적절하다.

---

## 6. 권장 개발 우선순위

### Phase 0. 결과의 의미와 기준선 고정

- 현재 결과를 `non-standard surrogate codec` 기반 결과로 명시
- 절대 BLER/MCS threshold를 NR 성능으로 주장하지 않음
- 변경 전 대표 config와 결과를 baseline artifact로 저장
- RNG seed를 config로 고정 가능하게 변경

완료 기준:

- 같은 seed/config에서 결과 재현
- 모든 결과 파일에 모델과 근사 가정 표시

### Phase 1. 현재 자체 LDPC decoder 교정

- edge별 `v2c`, `c2v` 메시지 구조 적용
- extrinsic update 구현
- syndrome check와 early stopping
- decoder status/iteration count 반환
- 작은 행렬에 대한 수작업/독립 reference 비교
- zero-noise round-trip test

완료 기준:

- 모든 생성 codeword의 syndrome이 0
- noise-free decode 100% 성공
- iteration별 syndrome이 안정적으로 수렴
- ASan/UBSan 오류 없음

### Phase 2. NR LDPC 및 표준 rate matching

- BG1/BG2 table
- base graph selection
- `Zc` selection
- code-block segmentation 및 CB CRC
- LDPC encoding
- filler bit 처리
- sub-block/circular-buffer rate matching
- RV별 표준 `k0`
- code-block concatenation

완료 기준:

- 외부 reference와 encoder/rate-matcher bit-exact 일치
- RV 0/1/2/3 모두 검증
- puncturing/repetition/filler case 포함

가능하면 처음부터 전체 simulator에 연결하지 말고 독립 `nr_sch` 모듈과 unit test로 완성한 뒤 통합한다.

### Phase 3. Polar chain 정합화

- 표준 reliability sequence
- PBCH/PDCCH/UCI별 parameterization
- CRC/interleaving/rate matching bit-exact 검증
- 필요 시 CA-SCL decoder 추가

완료 기준:

- 표준 또는 신뢰 가능한 reference vector와 일치

### Phase 4. 공통 simulation pipeline 리팩터링

현재 규모(2026-09-02 재검증 기준):

- `PHY/src/pdsch.c`: 약 7,876줄
- `PHY/src/pusch.c`: 약 4,060줄(같은 날 UL Eigen-BF K=4 확장 이후 갱신, 작성 시점엔 약 3,270줄)
- `run_*` simulation 함수: 총 68개(작성 시점엔 약 65개)

권장 pipeline:

```text
TB generation
  -> channel coding
  -> rate matching
  -> scrambling/modulation
  -> layer mapping/precoding
  -> resource mapping
  -> channel
  -> channel estimation/equalization
  -> demapping
  -> HARQ combining
  -> decoding
  -> metrics
```

SISO, SIMO, SU-MIMO, MU-MIMO, codebook, Eigen-BF, Beam Management는 전체 pipeline을 복제하지 않고 다음 policy만 교체하도록 한다.

- precoder selection
- channel generation
- channel estimation
- detector
- HARQ channel evolution

완료 기준:

- 동일 SISO config에서 리팩터링 전후 결과가 동일 seed로 일치
- dispatch 조건과 실행 함수의 중복 감소
- noise normalization을 한곳에서 관리

### Phase 5. 정량 검증 체계

최소 추가 시험:

1. QPSK AWGN BER 대 이론값
2. QAM constellation/mapping bit-exact test
3. FFT/IFFT round-trip
4. CRC known vector
5. LDPC/Polar noise-free round-trip
6. LDPC syndrome 검사
7. rate matching과 dematching round-trip
8. codebook 모든 후보의 전력 정규화
9. ZF-BF residual interference
10. Genie 성능이 estimated CSI보다 나쁘지 않음
11. HARQ final BLER가 first-transmission BLER보다 나쁘지 않음
12. 충분한 표본에서 SNR 증가에 따른 BER/BLER 감소

통계 시험은 고정 trial 수만 사용하지 말고 최소 error-count stopping 또는 confidence interval을 적용한다.

### Phase 6. 표준 채널과 PHY 현실성 확장

- TDL-A/B/C/D/E
- RMS delay spread scaling
- Doppler/time correlation
- 모든 핵심 경로에 일관된 DMRS/CSI estimation 적용
- TS 38.214 TBS determination
- 필요 시 선택적 time-domain OFDM 경로

그 이후에 다음 기능을 확장한다.

- OLLA SM_4X4/CL_XPORT
- MU-MIMO K>2
- Type-II CSI
- Beam Management P2/P3
- UL Eigen-BF 3/4 layer

---

## 7. Claude가 먼저 답해야 할 질문

구현을 시작하기 전에 아래 형식으로 사용자에게 보고한다.

1. 위 P0 항목 중 코드로 확인된 사실
2. 잘못되었거나 과장된 지적이 있는지
3. 새로 발견한 correctness defect
4. 가장 먼저 수정할 한 가지와 그 이유
5. 변경 대상 파일
6. bit-exact 또는 수치 검증 방법
7. 기존 결과와 API에 미치는 영향
8. 작업을 작은 커밋으로 나누는 방법

권장 첫 작업 후보는 **현재 자체 LDPC BP decoder의 edge-message 교정과 독립 unit test 추가**다. 단, 최종 목표가 NR conformance라면 자체 LDPC를 오래 개선하기보다 NR LDPC 모듈로 교체하는 비용과 비교해서 결정해야 한다.

---

## 8. 개발 방향 최종 제안

현재 방향을 다음과 같이 전환한다.

```text
기존: 기능 확장 중심 NR-like LLS
전환: 검증 가능한 NR coding + 공통 simulation kernel
이후: MIMO/Beam/OLLA 기능 재확장
```

기존 결과의 적절한 표현:

> 본 결과는 simplified LDPC/Polar 및 implementation-defined channel model을 사용하는 RE-domain PHY prototype의 상대 비교 결과이며, 3GPP NR conformance 또는 절대 BLER 성능을 의미하지 않는다.

핵심 원칙:

> 기능 수보다 결과의 의미와 재현 가능성을 우선한다.

---

## 9. 참고 표준

- 3GPP TS 38.211: NR; Physical channels and modulation
- 3GPP TS 38.212: NR; Multiplexing and channel coding
- 3GPP TS 38.213: NR; Physical layer procedures for control
- 3GPP TS 38.214: NR; Physical layer procedures for data
- 3GPP TR 38.901: Channel model for frequencies from 0.5 to 100 GHz

---

## 10. Claude 재검증 결과 (2026-09-02)

Section 1의 지시에 따라 위 P0/P1 항목의 파일·라인 근거를 실제 코드와
대조했다. 코드 수정은 아직 하지 않았다 — Section 1 item 5("사용자의
명시적 승인 전에는 대규모 리팩터링이나 코딩 체인 교체를 수행하지
않는다")에 따라 이 절은 검증/보고까지만 수행한다.

### 10.1 P0 항목 중 코드로 확인된 사실

전부 실제 코드와 정확히 일치함을 확인했다(라인 번호까지 포함):

- **P0-1**: `ldpc.c:12-73`의 `build_H()`가 `cw=3` 고정 column weight와
  modulo 배치로 자체 parity-check을 만듦, BG/`Zc`/lifting 구조 전혀
  없음(`ldpc.h`의 `LDPCCodec` 구조체에도 해당 필드 없음) — 확인.
- **P0-2**: `ldpc.c:103-147`의 `v2c`가 edge가 아니라 variable
  개수(`nv`)만큼만 할당되고, variable-node update가 전체 posterior
  `sum = llr[v] + Σ c2v`를 모든 연결된 check가 그대로 재사용 — 목적
  check를 제외하지 않는 self-feedback 구조 확인. `ldpc_decode_soft()`는
  early-stopping 없이 `max_iter` 고정 반복 후 `return 0;`을 조건 없이
  실행 — 확인. (부가 발견: `ldpc.h:36`의 `ldpc_decode()` 주석이
  "Returns 0 on success"라고 조건부처럼 적었지만 실제로는 성공 여부와
  무관하게 항상 0을 반환 — 문서·구현 불일치, 아래 10.3 참조.)
- **P0-3**: `rate_matching.c:10-12`의 `rv_start_offset()`이 정확히
  `k0=round(rv/4*ncb)` — 확인. 다만 `rate_matching.h:10-13`이 이미
  "Simplified circular-buffer rate matching (TS 38.212 5.4.2 in
  spirit)... implementation-defined simplification"이라고 자체
  라벨링하고 있음 — CLAUDE.md의 "구현 정의 명시" 관례를 코드 수준에서
  이미 따르고 있었다는 점은 문서에 없던 완화 요인.
- **P0-4**: `polar.c:21-73`의 `generate_frozen_bits()`가 Bhattacharyya
  재귀(`nz[2j]=2z[j]-z[j]²`)로 실행 시 신뢰도 순서를 만듦, TS 38.212
  표 5.3.1.2-1 고정 시퀀스 아님 — 확인. `polar.c`/`polar.h`에 SCL이나
  RNTI 관련 코드 없음(순수 SC) — 확인. 단, TS 38.212 §5.4.1.1
  shortening 강제-frozen 로직(`polar.c:37-49`)은 존재(2026-07-13
  버그 수정 이력과 일치). RNTI CRC masking은 PBCH 대상이 아니라
  PDCCH에서 `attach_crc_rnti()`/`check_crc_rnti()`(CRC24C)로 이미
  구현돼 있음(`pdcch.c:135,179` 등) — 이 항목은 문서의 "확인할 항목"
  체크리스트 중 하나였고 이미 존재함을 확인.

### 10.2 P1 항목 중 코드로 확인된 사실

- **P1-1**: `tdl.c:12-13`이 6개 고정 tap(delay_ns_ref/power_db)과
  자체 `scale = delay_spread_ns/30.0` 스케일링을 사용, TDL-A/B/C/D/E
  프로파일 선택 없음 — 확인. 파일 헤더 주석 자체가 "approx. 6-tap
  NLOS profile"이라고 이미 명시하고 있는 것도 확인.
- **P1-2**: `pdsch.c:7490-7513`(빔 관리 TDL)의 주석이 공유-스칼라
  TDL 모델과 그 한계(빔 순위가 RE 무관하게 항상 동일함)를 이미
  "분석적으로 확인한 사실"로 상세히 문서화하고 있음 — 이 항목은 새
  발견이 아니라 이미 코드 주석 수준에서 명시적으로 다뤄진 내용.
  `pusch.c`의 UL Eigen-BF TDL 경로도 동일한 패턴(같은 세션에서
  "1-Tx TDL 설계 결함 발견·수정" 이력으로 이미 검증됨).
- **P1-3**: 전체 소스에서 `ofdm_modulate`/`ofdm_demodulate` 호출은
  `main.c:131,134` 단 한 곳뿐(레거시 단순 OFDM AWGN 루프) — 나머지
  모든 `run_pdsch_*`/`run_pusch_*`(68개 중 67개)는 RE-domain에서
  직접 `y=Hx+n`을 계산 — 확인.
- **P1-4**: `pdsch.c:164-167`을 포함해 대다수 `run_*` 함수가
  `tbsz=(int)(max_dbits*cr)` 직접 곱셈으로 TBS를 정함(TS 38.214
  `N_info` 양자화·근접 TBS 테이블 절차 없음) — 확인. 다만
  `run_pdsch_simulation()`(가장 기본형, `pdsch.c:38`)은 이 패턴이
  아니라 `cfg->tbSize`를 그대로 사용 — 문서가 인용한 "여러
  `run_pdsch_*`/`run_pusch_*` 함수" 표현과는 맞지만, `pdsch.c:38-45`를
  P0-1의 근거로도 겸용한 것은 다소 부정확 — 그 줄은 TBS를 config에서
  가져오는 코드일 뿐 LDPC `build_H()` 자체와는 무관.

### 10.3 잘못되었거나 과장된 지적

기술적 판단 자체가 틀린 항목은 없었다. 다만 두 가지 사소한 부정확함:

- P0-1 근거 목록의 `pdsch.c:38-45`는 LDPC 자체가 아니라 TBS 소스
  코드라 P0-1보다는 P1-4의 근거에 더 가깝다(위 10.2 참조).
- Section 4의 "74/74"/"pusch.c 약 3,270줄"/"약 65개 함수"는 문서
  작성 이후 같은 날 진행된 UL Eigen-BF K=4 확장으로 78/78 / 약
  4,060줄 / 68개로 바뀌었다 — 본문 Section 4/6에 갱신 표시함.

### 10.4 새로 발견한 결함

- `ldpc.h:36`의 `ldpc_decode()` 독스트링 "Returns 0 on success"가
  실제 무조건 0 반환 동작과 불일치(위 10.1 참조) — 사소하지만
  독스트링이 실제보다 더 신뢰할 수 있는 것처럼 오인시킬 수 있음.
  Phase 1(BP decoder edge-message 교정) 작업 시 syndrome 기반 반환값
  구현과 함께 이 독스트링도 정정하는 것이 자연스러움.

### 10.5 가장 먼저 수정할 한 가지와 이유

> **완료(2026-09-02)**: 아래 10.5~10.9에 정리한 계획대로 사용자 승인
> 후 착수해 완료함. `ldpc.c`/`ldpc.h` 수정, 손계산 트리 그래프 +
> 브루트포스 marginal과의 bit-exact 대조, 프로젝트 자체 noise-free
> round-trip(K=8/64/400) 전부 통과, 68개 `run_*` 호출부 반환값 미참조
> 확인, 수정 전/후 BLER 비교(12dB에서 0.900→0.806, 일관된 방향의 개선)
> 로 검증. 회귀 78/78 유지, 경고 없음. 상세는
> `docs/analysis/history.md`의 "LDPC BP decoder edge-message 교정
> (2026-09-02)" 항목 참조. 다음 단계(P0-1 NR mother-code 이식 또는
> P0-3 표준 rate matching)는 `tasks/todo.md`에 후속 과제로 등록—
> 착수 전 사용자 확인 필요.

문서의 제안(P0-2, BP decoder edge-message 교정)에 동의한다. 이유:

- P0-1(NR LDPC 자체 교체)이나 P0-3(표준 rate matching)은 P0-2가
  틀린 상태로는 정확도를 검증할 기준선이 없다 — edge-message 구조부터
  옳아야 이후 단계(NR mother code 이식, 표준 k0)의 결과를 신뢰할 수
  있다.
- P0-2는 `ldpc.c` 내부 함수 시그니처(`ldpc_decode`/`ldpc_decode_soft`)
  를 바꾸지 않고도 구현만 교정할 수 있어 다른 68개 `run_*` 함수를
  전혀 건드리지 않는다 — 영향 범위가 가장 좁다.
- 독립적인 손계산 작은 H 행렬로 수렴 여부를 검증할 수 있어(문서
  Section 1 item 6과도 부합) 검증 비용이 낮다.

### 10.6 변경 대상 파일(1단계 한정)

- `PHY/src/ldpc.c`: `ldpc_decode_soft()` 내부 구조를 variable-count
  배열(`v2c[nv]`)에서 edge-count 배열(`H_nnz`개)로 교체, syndrome
  early-stopping과 실제 성공/실패 반환값 추가.
- `PHY/include/ldpc.h`: `ldpc_decode()` 독스트링을 실제 반환 의미에
  맞게 정정.
- 신규 `PHY/tests/` 또는 스크래치 하네스: 손계산 가능한 작은
  parity-check 행렬(예: (7,4) Hamming류)로 zero-noise round-trip과
  syndrome 수렴을 독립 검증 — 이 프로젝트에 아직 별도 unit test
  디렉터리가 없으므로 배치 위치는 착수 전 별도 확인 필요.

### 10.7 검증 방법

- Noise-free round-trip: 임의 정보비트 → encode → LLR을 매우 큰
  양/음수(확실한 부호)로 변환 → decode → 원래 비트와 100% 일치.
  syndrome `H·decoded^T`가 매 반복 후 0으로 수렴하는지 로그.
- 독립적으로 손으로 구성한 작은 H(예: 7×3 또는 그보다 작은 행렬)에서
  BP 반복을 손계산과 대조.
- 이후 `qam_demap_llr`/`ldpc_encode`를 통과하는 기존 `run_pdsch_*`
  회귀가 78/78 그대로 유지되는지(구조 교체가 기존 동작을 깨지
  않는지) 확인.

### 10.8 기존 결과·API에 미치는 영향

- `ldpc_decode`/`ldpc_decode_soft`의 시그니처는 그대로 유지 가능(반환
  값 의미만 0=항상성공 → 0=실제성공/음수또는1=미수렴으로 바뀜) —
  호출부 68개 `run_*` 함수 대부분이 반환값을 현재 무시하고 있어(직접
  확인 필요) 이 변경만으로는 기존 BLER 계산 로직에 영향이 없을 가능성이
  높으나, 반환값을 참조하는 호출부가 있는지는 착수 전 grep으로 확인
  필요.
- BLER/MCS 절대 수치 자체는 이번 1단계(edge-message 교정)만으로는
  크게 안 바뀔 수 있다(현재도 잘못된 방식으로나마 수렴은 하고 있을
  가능성) — 진짜 큰 수치 변화는 P0-1(NR mother code 교체) 단계에서
  발생할 것으로 예상되므로, 1단계 완료 후 BLER curve를 baseline과
  비교해 변화 방향을 사용자에게 보고해야 한다.

### 10.9 작업을 작은 커밋으로 나누는 방법

1. `ldpc.h` 독스트링 정정(단독, 무해).
2. edge-message(`v2c`→`H_nnz` 크기) 구조 변경 + syndrome 계산 추가,
   반환값은 아직 그대로 0(회귀 영향 없음 확인용 중간 커밋).
3. 손계산 검증 하네스 추가(별도 파일, 메인 빌드에 영향 없음).
4. syndrome 기반 조기 종료 + 실제 성공/실패 반환값 활성화(회귀 78/78
   재확인).

각 커밋 후 `make -f c_Makefile clean && make -f c_Makefile && ./regression_test.sh`
로 78/78과 clean build를 재확인한다.

---

## 11. P0-1 이행 완료 (2026-09-02)

Section 10.5의 완료 표시(P0-2) 이후, 같은 세션에서 P0-1(NR BG1/BG2
QC-LDPC 이식)까지 EnterPlanMode 계획 승인 후 완료했다. 상세는
`docs/analysis/history.md`의 "NR BG1/BG2 QC-LDPC 이식 — P0-1 완료
(2026-09-02)" 항목 참조. 요약:

- **자체 modulo 기반 surrogate LDPC를 실제 TS 38.212 BG1/BG2 QC-LDPC로
  교체** — 공개 API(`ldpc_init`/`ldpc_encode`/`ldpc_decode`/
  `ldpc_decode_soft`) 시그니처 무변경, 53개 이상 호출부 전부 무수정.
- 표 전사(~4100개 shift 정수)는 로컬 스펙 docx에서 프로그램적으로
  추출(手전사 없음), TS 38.212 §5.3.2 원문을 3gpp-server MCP로 직접
  읽어 가장 위험했던 미확정 항목(circulant shift 방향)을 확정.
- 추출된 표에서 인코딩에 필요한 "core 4×4 + 나머지 대각 항등" 구조를
  **실측으로 확인**(문헌 암기 아님) 후 이를 그대로 이용하는 구조화
  인코더 구현.
- 범위: 단일 코드블록만(세그멘테이션은 P0-2c 후속), 표준 rate
  matching은 P0-3 후속(`rate_matching.c` 무변경).
- 설계 검토 중 발견해 사용자 승인 후 함께 처리한 부수 이슈 2건: (1)
  ~40개 직접 호출 함수의 `coded_size`가 mother-code 길이로 커져
  BLER이 MCS 목표율이 아닌 mother-code 성능을 반영하게 되는 변화
  (P0-3 전까지 유지 확정), (2) HARQ mother-rate 호출부 14곳의 BG
  선택 기준 버그(실제 목표율 대신 고정 1/3을 넘기던 것) 수정.
- 검증: 표 자가검증(중복 없음/전체 커버/구조 확인) + BG/`Zc` 선택
  경계 전수 + 인코더 syndrome 1145회 + 기존(무변경) BP 디코더로
  noise-free round-trip + AWGN waterfall 확인 + 컷오버 후 회귀 78/78
  + HARQ mother-rate 수정 실측 확인. 전부 통과.

다음 후보(P0-2c 세그멘테이션, P0-3 표준 rate matching)는
`tasks/todo.md`에 등록, 착수 전 사용자 확인 필요.

---

## 12. P0-3 이행 완료 (2026-09-02)

Section 11(P0-1) 완료 직후, 같은 세션에서 "P0-3 진행해" 승인 후
EnterPlanMode로 범위를 재조사(51개 함수로 그룹 A/B 분리 확인)하고
계획 승인을 받아 완료했다. 상세는 `docs/analysis/history.md`의
"TS 38.212 §5.4.2.1 표준 rate matching 이식 — P0-3 완료 (2026-09-02)"
항목 참조. 요약:

- LDPC를 쓰는 51개 `run_pdsch_*`/`run_pusch_*` 함수 중 HARQ 14곳은
  균등 1/4-버퍼 k0(비표준)만 표준 공식으로 교체, 나머지 37곳은
  rate matching 자체를 신규 추가(이전엔 mother codeword를 그냥
  truncate — P0-1 이후 BLER이 mother-code 성능을 반영하던 원인).
- k0 공식(Table 5.4.2.1-2)은 3gpp-server MCP의 `get_image`로 수식
  이미지를 직접 열어 확인(이전 spec-lookup 서브에이전트의 웹 교차
  검증 결과와 일치 재확인).
- 신규 `nr_rate_matching.c`(LDPC 전용) — 기존 `rate_matching.c`
  (PUCCH F3 Polar HARQ 공유)는 무변경.
- 대량 기계적 적용(51개 함수)은 pdsch.c/pusch.c 담당 fork 2개에 병렬
  위임 후 직접 통합 재검증, fork가 구조 판단을 이유로 남겨둔 2곳
  (`run_pdsch_simulation`, `run_pusch_tdl_turbo_simulation`)은 직접
  처리 — 특히 turbo 함수는 iteration간 extrinsic 재선별을 위해
  `nr_ldpc_rate_match_select_soft()`(double 버전)를 신규 설계.
- 검증: k0 손계산 일치, select/combine round-trip, HARQ/비-HARQ
  스팟체크 다수(BLER 단조 감소, 물리적 타당성), turbo `ITERS=1` 시
  path1과 bit-for-bit 정확히 일치하는 기존 불변조건 재확인. 회귀
  78/78, clean 빌드 유지.

검토 문서의 P0-1/P0-2/P0-3 전부 완료. 남은 항목은 P0-2c(다중
코드블록 세그멘테이션)뿐 — `tasks/todo.md`에 후속 과제로 등록,
착수 전 사용자 확인 필요.

---
