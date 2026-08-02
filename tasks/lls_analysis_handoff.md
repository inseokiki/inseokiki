# PHY LLS 분석 및 Claude Code 작업 인수인계

> 작성일: 2026-08-03  
> 대상: `PHY/` 5G NR Link Level Simulator  
> 목적: 현재 LLS의 구조·제약·정합성 문제를 정리하고, Claude Code가 기존 작업을 보존하면서 후속 개선을 수행할 수 있도록 작업 범위와 검증 기준을 제공한다.

---

## 1. Claude Code 작업 시작 지시

작업 전 반드시 루트의 `CLAUDE.md`, `AGENTS.md`, `STRUCTURE.md`, `tasks/todo.md`, `tasks/lessons.md`를 읽는다.

프로젝트 규칙에 따라 아래 명령을 가장 먼저 실행한다.

```bash
git fetch origin
git status
git log --oneline develop..origin/develop
```

원격에 로컬에 없는 커밋이 있으면 임의로 병합하거나 리베이스하지 말고 사용자에게 방향을 확인한다.

현재 작업 트리에는 다음 사용자 작업이 미커밋 상태로 존재한다.

- `PHY/src/main.c`
- `PHY/src/pdsch.c`
- `PHY/include/pdsch.h`
- `tasks/todo.md`
- `PHY/build/*.o`, `PHY/lls_sim_c`
- `.claude/settings.json`

이 변경을 되돌리거나 덮어쓰지 않는다. 특히 현재 `pdsch.c`에는 `SM_4X4 + TDL`, `SM_4X4 + HARQ`, `CL_4PORT + TDL` 관련 구현이 포함되어 있다.

### 권장 첫 작업 범위

우선 한 번에 전체 장기 과제를 구현하지 말고 아래 P0 항목부터 처리한다.

1. PDSCH/PUSCH의 `MCS_TABLE=TABLE3` 선택 오류 수정
2. config 값 및 미지원 조합 검증 추가
3. 관련 회귀 테스트 또는 자동 검증 스크립트 추가
4. 문서와 실제 라우팅의 불일치 정리

P0 완료 후 결과를 보고하고 P1 작업 착수 여부를 사용자에게 확인한다.

---

## 2. LLS 성격과 현재 범위

현재 `PHY/`는 3GPP 처리 흐름을 기반으로 채널·MIMO·HARQ·등화 알고리즘의 BER/BLER 경향을 비교하는 연구용 LLS다.

표준 bit-exact 또는 conformance simulator로 간주하면 안 된다. 특히 LDPC, TDL, 자원 매핑, 파형 처리에 구현 정의 단순화가 있다.

### 지원 기능

| 영역 | 현재 기능 |
|---|---|
| DL 채널 | PBCH, PDCCH, PDSCH, CSI-RS |
| UL 채널 | PUSCH, PUCCH F0~F3, PRACH, SRS, UL power control |
| 채널 모델 | AWGN, flat fading, 근사 TDL |
| MIMO | SIMO-MRC, SM 2x2, SM 4x4, closed-loop 4-port codebook |
| HARQ | PDSCH 중심 Chase/IR soft combining, 일부 PUCCH 연구 모델 |
| PUSCH 연구 기능 | DFT-s-OFDM, hard DFE, soft-PIC + LDPC turbo equalization |
| 결과 지표 | BER, BLER, 검출률, 채널추정 MSE, IQ dump |

### 주요 실행 흐름

```text
PHY/src/main.c
  -> config_parser_load()
  -> calc_derived(): RB/FFT/CP/MCS 파생
  -> PHYSICAL_CHANNEL 분기
  -> CHANNEL_MODEL / USE_DMRS / MIMO_MODE / HARQ_ENABLE 세부 분기
  -> 채널별 run_*_simulation()
```

핵심 파일:

- `PHY/src/main.c`: 최상위 라우팅
- `PHY/src/config_parser.c`: 설정 파싱 및 파생값 계산
- `PHY/config/sim_config.txt`: 실행 설정
- `PHY/src/pdsch.c`: PDSCH 조합별 시뮬레이션
- `PHY/src/pusch.c`: PUSCH, TDL, DFE/turbo 등화
- `PHY/src/ldpc.c`: 현재 단순화 LDPC
- `PHY/src/tdl.c`: 현재 근사 TDL
- `STRUCTURE.md`: 코드 구조와 구현 이력

---

## 3. 잘 구성된 부분

- `MCS_INDEX`와 `MCS_TABLE`을 진입점으로 변조차수 및 부호율을 파생하려는 설정 구조가 명확하다.
- 변조, 채널, 채널추정, MIMO, 코딩, rate matching이 모듈로 분리되어 있다.
- PDSCH는 SISO/SIMO/2x2/4x4/CL 4-port와 TDL/HARQ 조합까지 폭넓게 확장 중이다.
- PUSCH의 DFE와 turbo equalization은 개인 알고리즘 연구용으로 유용하다.
- 완벽 CSI를 사용한 경로에 `genie-aided`임을 명시해 구현 가정을 구분하고 있다.
- 2026-08-03 기준 전체 `PHY/src/*.c`는 C99 syntax 검사에 성공한다.

검사 명령:

```bash
gcc -std=c99 -Wall -Wextra -Wno-unused-parameter \
    -I PHY/include -fsyntax-only PHY/src/*.c
```

현재 오류는 없으며, `pdsch.c`와 `pucch.c`에 `total_err_1st` 미사용 경고가 총 5건 있다.

---

## 4. 확인된 문제와 개선 과제

## P0 — 즉시 수정 권장

### P0-1. MCS TABLE3 선택 오류

`pdsch.c`와 `pusch.c`의 여러 함수가 다음과 같은 이분법을 사용한다.

```c
MCSTableType tbl =
    (strcmp(cfg->mcsTableType, "TABLE2") == 0) ? MCS_TABLE2 : MCS_TABLE1;
```

이 경우 `MCS_TABLE=TABLE3`도 실제 채널 함수 내부에서는 TABLE1로 처리된다. config 출력값과 실제 시뮬레이션 파라미터가 달라질 수 있다.

권장 수정:

- `config_parser.c` 내부에만 있는 table 문자열 변환을 공용 API로 노출하거나
- `mcs_table.c/.h`에 단일 변환 함수를 두고 모든 채널이 재사용한다.
- PDSCH/PUSCH 전체 중복 코드를 기계적으로 교체한다.

완료 기준:

- TABLE1/2/3 각각 대표 MCS index를 넣었을 때 설정 출력과 실제 채널 함수의 `Qm`, target code rate가 일치한다.
- 잘못된 table 문자열은 조용히 TABLE1로 fallback하지 않고 명확한 오류를 낸다.

### P0-2. Config validation 부족

현재 파서는 대부분의 값을 읽기만 하며 잘못된 값이나 미지원 조합을 충분히 차단하지 않는다.

최소 검증 대상:

- `SNR_STEP > 0`, `SNR_END >= SNR_START`
- `NUM_TRIALS > 0`
- BW/SCS/NRB/NFFT 조합과 `12 * NRB <= NFFT`
- table별 MCS index 범위
- `EQUALIZER`가 `ZF` 또는 `MMSE`
- `SPATIAL_CORR_TX`가 `[0,1)`
- `HARQ_MAX_RETX > 0`, RV sequence가 `IR` 또는 `CHASE`
- PUCCH format 및 UCI bit 범위
- PRACH `N_CS`, root index, max delay의 유효 범위
- 채널별 지원하지 않는 `CHANNEL_MODEL`, MIMO, HARQ 조합

권장 정책:

- 잘못된 입력은 경고 후 임의 fallback하지 말고 오류와 파라미터 이름을 출력한 뒤 비정상 종료한다.
- 의도적으로 무시하는 설정은 실행 시작 시 명시적으로 출력한다.

### P0-3. 문서와 코드 불일치

현재 확인된 불일치:

- `tasks/todo.md`는 `CL_4PORT + TDL`을 미완료로 표시하지만 현재 미커밋 코드에는 구현이 있다.
- `sim_config.txt` 주석은 PDCCH fading 지원이 pending이라고 적혀 있지만 `main.c`에는 fading 분기가 존재한다.
- `STRUCTURE.md`의 PDSCH 라우팅 설명에 최근 SM_4X4 및 CL_4PORT 조합이 완전히 반영되었는지 재검토가 필요하다.

코드 동작과 실측 검증 여부를 확인한 후 문서를 갱신한다. 단순히 함수가 존재한다는 이유만으로 완료 처리하지 않는다.

### P0-4. 자동 회귀 테스트 부재

최소 테스트 세트를 추가한다.

- CRC known-answer test
- QAM mapping/demapping 무잡음 round trip
- Polar/현재 LDPC 무잡음 encode/decode
- rate matching select/combine 및 RV 동작
- AWGN uncoded BER과 이론값 비교
- CP-OFDM/DFT-s-OFDM AWGN 결과의 허용 오차 내 동등성
- MRC/ZF/MMSE 무잡음 복원
- config 조합별 `main.c` dispatch 도달성
- TABLE1/2/3 선택 회귀 테스트

테스트는 고정 seed를 지원해 재현 가능하게 한다. 통계 시험은 sample 수와 허용 오차를 명시한다.

---

## P1 — 성능 결과 신뢰도 개선

### P1-1. RE별 effective noise variance 기반 LLR

여러 TDL/MIMO 경로에서 RE별 검출 잡음분산을 평균해 하나의 `env`로 만든 뒤 모든 심볼 LLR에 사용한다.

문제점:

- 깊은 페이딩 RE와 강한 RE의 신뢰도가 동일하게 취급된다.
- 고차 QAM, TDL, 4x4 MIMO에서 LDPC 입력 LLR의 calibration이 나빠질 수 있다.

권장 개선:

- `qam_demap_llr()`의 RE별 noise variance 버전을 추가한다.
- 검출기가 반환한 `nv_re[d][layer]`를 심볼별로 직접 사용한다.
- 기존 평균 방식과 BER/BLER 및 LLR 분포를 A/B 비교한다.

완료 기준:

- AWGN/flat 경로에서 기존 결과가 허용 오차 안에서 유지된다.
- TDL 경로에서 동일 random draw 기반 A/B 결과를 기록한다.
- 결과와 해석을 `docs/analysis/history.md`에 남긴다.

### P1-2. PUSCH HARQ 조합

현재 PUSCH에는 DFE/turbo 연구 경로가 있으나 표준적인 HARQ 재전송 결합 경로가 없다.

권장 범위:

- 우선 SISO + DMRS + CP-OFDM/DFT-s-OFDM
- AWGN/flat/TDL
- Chase/IR soft combining
- 1st BLER, final BLER, average transmissions 출력

PUSCH DFE/turbo와 HARQ를 한 번에 결합하지 말고 기본 HARQ 경로를 먼저 검증한다.

### P1-3. CL_4PORT 후속 조합

- CL_4PORT + TDL 실측 및 회귀 확인
- CL_4PORT + HARQ
- 유한 XPD 및 편파 간 누설을 포함한 공간상관 모델

현재 동일편파 상관만 적용하면 교차편파 다이버시티 때문에 `rho -> 1`에서도 rank-1 선택률이 충분히 증가하지 않을 수 있다. 이는 기존 모델 가정상 물리적으로 가능한 결과이며, 단순히 RI 선택 로직의 버그로 단정하지 않는다.

---

## P2 — 표준 정합성 및 파형 현실성 개선

### P2-1. 현재 LDPC는 NR LDPC가 아님

`PHY/src/ldpc.c`는 TS 38.212의 BG1/BG2, lifting size, QC-LDPC 구조가 아니라 자체 생성한 column-weight 3 희소 패리티 검사 행렬을 사용한다.

영향:

- BLER waterfall 및 error floor가 실제 NR LDPC와 다를 수 있다.
- 현재 HARQ rate matching도 NR circular buffer의 정확한 bit selection으로 볼 수 없다.
- 상용 NR PHY 또는 표준 reference curve와 직접 비교하면 안 된다.

향후 정확한 구현 범위:

- TB CRC 및 code-block segmentation
- BG1/BG2 선택
- lifting size `Z_c`
- QC-LDPC encode/decode
- filler bits
- TS 38.212 rate matching/interleaving
- 다중 code block 및 CB CRC

이 변경은 규모가 크므로 별도 설계 문서와 단계별 known-answer test를 먼저 작성한다.

### P2-2. TDL은 TS 38.901 정확 모델이 아님

현재 TDL은 약 6 tap NLOS PDP 근사 모델이다. 정확한 TDL-A/B/C/D/E의 delay/power/LOS/Doppler/공간상관을 재현하지 않는다.

현재 결과는 `implementation-specific approximate TDL`로 표시해야 한다.

향후에는 프로파일 테이블을 설정과 분리하고 다음을 지원한다.

- TDL-A/B/C/D/E 선택
- normalized delay와 RMS delay scaling
- LOS tap 처리
- Doppler 및 시간 상관
- MIMO 안테나 공간상관

### P2-3. 다수 물리채널은 실제 waveform 경로가 아님

`ofdm_modulate()`/`ofdm_demodulate()`는 현재 주로 legacy 경로에서만 호출된다. 다수 PDSCH/PUSCH/TDL 시뮬레이션은 RE/QAM 심볼 도메인 모델이다.

따라서 다음 효과는 포함되지 않거나 제한적이다.

- CP 부족에 의한 ISI
- 시간영역 다중경로 convolution
- CFO, phase noise, sampling offset
- Doppler/time selectivity
- PA 비선형성과 PAPR
- 실제 슬롯/심볼 자원 그리드

향후 waveform 경로는 기존 빠른 RE-domain 경로를 제거하지 말고 `ABSTRACTION_LEVEL=RE|WAVEFORM`처럼 병렬 유지하는 것이 적절하다.

---

## 5. 구현 원칙

- 기술 근거는 우선 TS 38.211/212/213/214 및 TS 38.901을 사용한다.
- 정확한 표준 구현과 근사 모델을 함수명, 출력, 문서에서 구분한다.
- 표준 기반 파라미터를 함수 내부에 반복 하드코딩하지 않는다.
- 공용 변환과 validation은 한곳에 두어 채널별 분기를 일치시킨다.
- 대규모 리팩터링 전에 현재 결과를 golden baseline으로 보존한다.
- random seed를 설정 가능하게 만들어 전후 결과를 같은 draw로 비교한다.
- 통계 결과에는 trial 수, seed, MCS, TBS, 채널 모델, CSI 가정, SNR 정의를 함께 기록한다.
- 코드를 수정하면 `tasks/todo.md`에 진행 상태를 반영한다.
- 비자명한 실수나 교훈은 `tasks/lessons.md`에 무엇/왜/적용 형식으로 기록한다.
- 완료 구현과 실측 결과는 `docs/analysis/history.md`에 누적한다.

---

## 6. SNR 정의 점검 사항

현재 코드 경로별 `SNR`이 다음 중 무엇인지 일관되게 점검해야 한다.

- 심볼 에너지 기준 `E_s/N_0`
- 정보비트 기준 `E_b/N_0`
- coded-bit 기준 SNR
- RE당 수신 SNR

변조차수 `Q_m`, code rate `R`, DMRS/CP/unused RE 오버헤드를 포함하면 관계는 구현 가정에 따라 달라진다.

예:

\[
\frac{E_s}{N_0} = Q_m R \frac{E_b}{N_0}
\]

여기서 `E_b`가 정보비트 에너지이고 별도 오버헤드를 제외했을 때의 관계다. 현재 각 시뮬레이션 출력의 `SNR (dB)`가 동일한 물리량을 의미하는지 확인하고, 아니면 출력과 문서에 축 정의를 명시한다.

---

## 7. Claude Code P0 작업 완료 체크리스트

- [ ] 작업 전 Git 원격 및 dirty worktree 확인
- [ ] 기존 미커밋 PDSCH 작업 보존
- [ ] 공용 MCS table 문자열 변환 구현
- [ ] PDSCH/PUSCH의 TABLE3 fallback 제거
- [ ] config validation 및 오류 메시지 추가
- [ ] TABLE1/2/3 및 잘못된 설정 회귀 테스트
- [ ] 전체 C99 build/syntax 검사
- [ ] 짧은 smoke simulation 수행
- [ ] 기존 baseline과 의도치 않은 성능 변화 비교
- [ ] `tasks/todo.md` 업데이트
- [ ] 필요 시 `tasks/lessons.md` 업데이트
- [ ] 완료 내용과 실측을 `docs/analysis/history.md`에 기록
- [ ] 변경 파일, 검증 명령, 남은 제한을 사용자에게 보고

---

## 8. 현재 분석 결론

현재 LLS의 강점은 다양한 PHY 알고리즘을 빠르게 결합하고 비교할 수 있다는 점이다. 가장 시급한 문제는 기능 추가 자체보다 설정과 실제 실행 파라미터의 정합성, 자동 회귀 검증, LLR 신뢰도 계산이다.

단기적으로는 TABLE3 및 validation 문제를 먼저 고치고 테스트 기반을 만든 뒤, PUSCH HARQ와 RE별 LLR 개선을 진행하는 것이 적절하다. NR LDPC 및 정확한 waveform/TDL 구현은 별도 장기 과제로 분리한다.
