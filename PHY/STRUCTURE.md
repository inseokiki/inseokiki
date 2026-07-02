# 5G NR PHY Link Level Simulator

C 기반 5G NR 물리 계층 링크 레벨 시뮬레이터.  
3GPP TS 38.xxx 스펙 기반으로 PDSCH 전송부터 채널 추정, HARQ, MIMO까지 구현.

---

## 디렉터리 구조

```
PHY/
├── Makefile                  # 루트 빌드 (lls_sim + ber_sim 동시 빌드)
│
├── common/                   # lls_sim / ber_sim이 공유하는 알고리즘 모듈
│   ├── include/              # 헤더 파일 (공개 API)
│   └── src/                  # 구현 파일
│
├── lls_sim/                  # Link Level Simulator (PDSCH 전체 체인)
│   ├── Makefile
│   ├── config/               # 시뮬레이션 설정 파일
│   ├── src/                  # main.c, pdsch.c, pdcch.c, pbch.c
│   ├── plot/                 # MATLAB 플롯 스크립트 (.m)
│   └── results/              # CSV 출력 (git 제외)
│
└── ber_sim/                  # 기본 BER 시뮬레이터 (AWGN, 변복조 검증)
    ├── Makefile
    ├── config/
    └── src/
```

---

## 모듈 구성 및 역할

### common/src — 공유 알고리즘

| 파일 | 역할 |
|------|------|
| `utils.c` | 난수 생성 (`randn`, `gen_random_bits`), BER 계산 |
| `config_parser.c` | `KEY = VALUE` 형식의 설정 파일 파싱 → `L1Config` 구조체 |
| `mcs_table.c` | TS 38.214 Table 5.1.3.1-1/2 MCS 테이블 조회 |
| `crc.c` | CRC-24A / CRC-24B 생성·검증 |
| `ldpc.c` | LDPC 부호화(systematic) + Sum-Product BP 디코더 |
| `polar.c` | Polar 부호화 + SC(Successive Cancellation) 디코더 |
| `polar_rate_match.c` | Polar 레이트 매칭 (puncturing / shortening) |
| `modulation.c` | QAM 변복조 (QPSK·16QAM·64QAM·256QAM), LLR 계산 |
| `fft.c` | Cooley-Tukey FFT / IFFT |
| `ofdm.c` | OFDM 변복조 (IFFT + CP 삽입 / CP 제거 + FFT) |
| `channel.c` | AWGN 채널, Flat Rayleigh 채널 |
| `tdl_channel.c` | TDL-A/C/D 다중경로 채널 (TS 38.901), AR(1) Doppler |
| `channel_estimation.c` | LS 추정, 선형 보간, ZF/MMSE 등화, **LMMSE 추정** |
| `dmrs.c` | DMRS Gold 시퀀스 생성, Type-1 파일럿 인덱스 |
| `harq.c` | HARQ IR 소프트 버퍼 (원형 버퍼 레이트 매칭, LLR 누적) |
| `tbs.c` | TS 38.214 Sec 5.1.3.2 TBS 계산 |
| `mimo.c` | MRC (Maximal Ratio Combining), per-SC 유효 잡음 분산 |
| `csi_rs.c` | CSI-RS 시퀀스 생성 및 매핑 |
| `srs.c` | SRS 시퀀스 생성 (Zadoff-Chu 기반) |

### lls_sim/src — 시뮬레이션 진입점

| 파일 | 역할 |
|------|------|
| `main.c` | 설정 파일 로드 → `physicalChannel` 값에 따라 시뮬 선택 |
| `pdsch.c` | PDSCH 시뮬 3종 (`run_pdsch_simulation`, `run_pdsch_dmrs_simulation`, **`run_pdsch_tdl_harq_simulation`**) |
| `pdcch.c` | PDCCH + Polar 부호화 BER 시뮬 |
| `pbch.c` | PBCH + Polar 부호화 BER 시뮬 |

---

## 빌드 방법

```bash
# 전체 빌드
cd PHY && make

# lls_sim 단독 빌드
cd PHY/lls_sim && make

# 클린 빌드
make clean && make
```

빌드 결과물: `PHY/lls_sim/lls_sim`, `PHY/ber_sim/ber_sim`  
(바이너리는 git 제외)

---

## 실행 방법

```bash
cd PHY/lls_sim

# 기본 설정 (config/sim_config.txt)
./lls_sim

# 설정 파일 직접 지정
./lls_sim config/tdl_harq_config.txt
```

`PHYSICAL_CHANNEL` 설정값에 따라 실행 모드가 결정됨:

| `PHYSICAL_CHANNEL` | 실행 모드 |
|---|---|
| `TDL_HARQ` | **메인 시뮬** — TDL + HARQ IR + MIMO MRC |
| `PDSCH` | PDSCH 기본 BER/BLER |
| `PDSCH` + `USE_DMRS=1` | PDSCH + DMRS 채널 추정 |
| `PDCCH` | PDCCH + Polar 디코더 |
| `PBCH` | PBCH + Polar 디코더 |
| `BER` | 미부호화 BER (변복조 검증) |
| (없음) | Legacy OFDM 시뮬 (LDPC/Polar 선택 가능) |

---

## 메인 시뮬레이션 처리 흐름 (TDL_HARQ)

`run_pdsch_tdl_harq_simulation()` 함수 기준.

```
설정 파일 로드
    │
    ├─ MCS → 변조 방식(Qm), 부호율(R)
    ├─ TBS 계산 (TS 38.214 Sec 5.1.3.2)
    ├─ LDPC 초기화 (K = TBS + 24 CRC bits)
    ├─ HARQ 버퍼 초기화 (N_cb = LDPC coded size)
    └─ DMRS 인덱스 / 시퀀스 사전 생성

[SNR 루프] ─────────────────────────────────────────────────────────────
│
│  N0 = 1 / SNR_linear
│  LMMSE 필터 빌드 (SNR별 1회, W = R_dp·(R_pp + N0·I)⁻¹)
│  TDL 채널 초기화 (Nrx개 안테나 독립)
│
│  [Trial 루프] ──────────────────────────────────────────────────────
│  │
│  │  ① 정보 비트 생성 + CRC-24A 부착
│  │       gen_random_bits(tb, TBS)
│  │       attach_crc(tb, TBS, CRC24A, tb_crc)   → K bits
│  │
│  │  ② LDPC 부호화
│  │       ldpc_encode(tb_crc → coded)            → N_cb bits
│  │
│  │  HARQ 소프트 버퍼 초기화
│  │       harq_reset()
│  │
│  │  [HARQ 라운드 루프: rv = 0, 1, 2, 3] ─────────────────────────
│  │  │
│  │  │  ③ 레이트 매칭 (원형 버퍼에서 E bits 추출)
│  │  │       harq_rate_match(coded, N_cb, E, rv, rm_bits)
│  │  │
│  │  │  ④ QAM 변조
│  │  │       qam_modulate(rm_bits → data_syms)   → E/Qm symbols
│  │  │
│  │  │  ⑤ 리소스 그리드 구성
│  │  │       tx_grid[pilot_pos] = DMRS 심볼
│  │  │       tx_grid[data_pos]  = 데이터 심볼
│  │  │
│  │  │  [Rx 안테나 루프: m = 0..Nrx-1] ────────────────────────
│  │  │  │
│  │  │  │  ⑥ TDL 채널 적용 (새 실현 + AWGN 추가)
│  │  │  │       tdl_new_realization()
│  │  │  │       tdl_apply_cfr(tx_grid → rx_grid[m])
│  │  │  │
│  │  │  │  ⑦ 채널 추정
│  │  │  │       LS:    ls_estimate(rx_pilots / tx_pilots → h_pilots)
│  │  │  │       LMMSE: lmmse_filter_apply(h_pilots → h_data[m])
│  │  │  │       LS:    interpolate_channel(h_pilots → h_full[m])
│  │  │  │              → h_data[m] 추출
│  │  │  │
│  │  │  └──────────────────────────────────────────────────────────
│  │  │
│  │  │  ⑧ MIMO MRC 결합
│  │  │       mrc_combine(rx_data_all, h_data_all, Nrx → y_mrc, nv_eff)
│  │  │
│  │  │  ⑨ LLR 계산
│  │  │       MMSE: qam_demap_llr_persc(y_mrc, nv_eff → llr_rx)
│  │  │       ZF:   qam_demap_llr(y_mrc, nv_flat → llr_rx)
│  │  │
│  │  │  ⑩ HARQ 소프트 결합 (LLR 누적)
│  │  │       harq_combine(llr_rx, E, rv → hbuf)
│  │  │
│  │  │  ⑪ LDPC 디코딩 (Sum-Product BP)
│  │  │       ldpc_decode(hbuf → decoded)
│  │  │
│  │  │  ⑫ CRC 검사
│  │  │       check_crc(decoded, K, CRC24A)
│  │  │       성공 → HARQ 조기 종료 (break)
│  │  │
│  │  └──────────────────────────────────────────────────────────────
│  │
│  │  블록 에러 누적 / 조기 종료 (MIN_BLOCK_ERRORS 도달 시)
│  │
│  └────────────────────────────────────────────────────────────────
│
│  [AWGN 기준선 루프] (비교용, 단일 HARQ 라운드, flat 채널)
│      동일 MCS로 AWGN 직접 인가 → BLER_AWGN 계산
│
│  결과 출력: SNR, BLER_Tx1~4, BLER_AWGN, Tput_Tx1~4, Tput_AWGN, N_trials
│  LMMSE 필터 해제
│
└────────────────────────────────────────────────────────────────────────

CSV 저장 (OUTPUT_CSV = 1 일 때)
```

---

## 핵심 알고리즘 정리

### LDPC — Sum-Product Belief Propagation

```
초기화: msg_v2c[v] = 채널 LLR[v]

반복 (max_iter회):
  Check node 업데이트:
    msg_c2v[edge i] = 2·atanh( ∏_{j≠i} tanh(msg_v2c[j] / 2) )
    → 연결된 나머지 variable node 메시지의 tanh 곱을 역변환

  Variable node 업데이트:
    msg_v2c[v] = 채널LLR[v] + Σ(모든 연결 check node의 msg_c2v)

최종 판정: msg_v2c[v] < 0 → bit=1, 아니면 bit=0
```

### HARQ IR — 원형 버퍼 레이트 매칭

```
N_cb = LDPC 부호화 출력 크기 (원형 버퍼 크기)

RV별 시작 위치:
  rv=0 → 0        (체계적 비트 우선, 첫 전송 최우선)
  rv=1 → N_cb/4
  rv=2 → N_cb/2
  rv=3 → 3*N_cb/4

rate_match: out[e] = coded[(start + e) % N_cb]  ← 원형 추출
combine:    llr_buf[(start + e) % N_cb] += llr_rx[e]  ← 소프트 누적
```

### MIMO MRC

```
수신 모델: y_m[k] = h_m[k]·x[k] + n_m[k],  n_m ~ CN(0, N0)

MRC 결합:
  y_mrc[k] = Σ_m conj(h_m[k]) · y_m[k]  /  Σ_m |h_m[k]|²

유효 잡음 분산:
  nv_eff[k] = N0 / Σ_m |h_m[k]|²  → LLR 계산에 사용
```

### LMMSE 채널 추정

```
주파수 상관 모델: R(Δk) = exp(−2π·τ_rms·Δf·|Δk|)

필터 (SNR별 1회 빌드):
  W = R_dp · (R_pp + N0·I)⁻¹
    R_pp[i,j] = R(pilot_pos[i] - pilot_pos[j])  ← 파일럿 간 상관
    R_dp[i,j] = R(data_pos[i]  - pilot_pos[j])  ← 데이터-파일럿 상관

적용 (trial/round/안테나당):
  h_data[k] = Σ_j W[k,j] · h_ls[j]  ← LS 추정값을 LMMSE 필터로 스무딩
```

### TDL 채널 — AR(1) Doppler 모델

```
탭 이득 진화 (OFDM 심볼마다):
  h_tap[t+1] = ρ · h_tap[t] + √(1-ρ²) · σ_tap · CN(0,1)
  ρ = J₀(2π·fd·Tsym)   ← Jakes 스펙트럼 근사

CFR 계산:
  H[k] = Σ_tap h_tap · exp(−j·2π·k·Δf·τ_tap)

TDL-D (LOS) 첫 탭: Rician = 고정 LOS 페이저 + AR(1) NLOS 성분
```

---

## 설정 파일 주요 파라미터 (`tdl_harq_config.txt`)

| 파라미터 | 설명 | 기본값 |
|----------|------|--------|
| `MCS_INDEX` | MCS 인덱스 (0~27) | 10 |
| `MCS_TABLE` | TABLE1 (64QAM) / TABLE2 (256QAM) | TABLE1 |
| `NUM_RB` | Resource Block 수 | 25 |
| `SCS_KHZ` | 서브캐리어 간격 [kHz] | 30 |
| `TDL_MODEL` | TDL_A / TDL_C / TDL_D | TDL_A |
| `TDL_DS_RMS_NS` | RMS 지연 확산 [ns] | 30.0 |
| `DOPPLER_HZ` | 최대 도플러 주파수 [Hz] | 0.0 |
| `HARQ_MAX_ROUNDS` | 최대 HARQ 재전송 횟수 | 4 |
| `NUM_RX_ANT` | 수신 안테나 수 (1~4, MRC) | 2 |
| `EQUALIZER` | MMSE (per-SC LLR) / ZF (flat LLR) | MMSE |
| `CHANNEL_EST` | LS+interp / LMMSE | LMMSE |
| `SNR_START/END/STEP` | SNR 범위 [dB] | -4 / 14 / 2 |
| `NUM_TRIALS` | 트라이얼 수 / SNR 포인트 | 2000 |
| `MIN_BLOCK_ERRORS` | 조기 종료 임계값 (블록 에러 수) | 200 |
| `OUTPUT_CSV` | CSV 파일 저장 여부 | 1 |
| `OUTPUT_FILE` | CSV 출력 경로 | results/result_tdl_harq.csv |

---

## 출력 CSV 컬럼 구조

```
SNR_dB, BLER_Tx1, BLER_Tx2, BLER_Tx3, BLER_Tx4,
        BLER_AWGN,
        Tput_Tx1_bits_RE, ..., Tput_Tx4_bits_RE,
        Tput_AWGN_bits_RE,
        N_trials
```

- `BLER_Txn` : n번째 HARQ 라운드까지 허용했을 때의 BLER
- `BLER_AWGN` : 동일 MCS, AWGN 직접 인가 기준선
- `Tput` : `TBS × (1 - BLER) / num_data_RE` [bits/RE]

---

## 구현 상태

| 기능 | 상태 |
|------|------|
| LDPC 부호화/복호화 (BP) | ✅ |
| Polar 부호화/복호화 (SC) | ✅ |
| QAM 변복조 + LLR (QPSK~256QAM) | ✅ |
| OFDM 변복조 (FFT + CP) | ✅ |
| AWGN / Flat Rayleigh 채널 | ✅ |
| TDL-A/C/D 다중경로 채널 (TS 38.901) | ✅ |
| DMRS Type-1 채널 추정 | ✅ |
| LS 추정 + 선형 보간 | ✅ |
| **LMMSE 채널 추정** | ✅ |
| ZF / MMSE 등화 | ✅ |
| HARQ IR (원형 버퍼, RV=0~3) | ✅ |
| MIMO MRC (1~4 Rx) | ✅ |
| TBS 계산 (TS 38.214 Sec 5.1.3.2) | ✅ |
| Monte Carlo 조기 종료 | ✅ |
| AWGN 기준선 오버레이 | ✅ |
| CSV 출력 | ✅ |
| LMMSE 채널 추정 (Priority 2) | ✅ |
| SU-MIMO 공간 다중화 | 미구현 |
| PUSCH 시뮬레이션 | 미구현 |
| CDL 채널 모델 (TS 38.901) | 미구현 |
| CQI/MCS 링크 어댑테이션 (OLLA) | 미구현 |
