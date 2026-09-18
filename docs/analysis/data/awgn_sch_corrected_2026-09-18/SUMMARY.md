# AWGN 전체 지원 채널 측정

현재 구현의 10개 대표 설정, 20회 실행, 392개 지표 측정값을 수집했다. 모든 실행이 정상 종료하고 예상 SNR 행 수·유한값·확률 범위 검사를 통과했다.

## 시험 조건

- SNR: [-10, -5, 0, 5, 10, 15, 20] dB. 시뮬레이터 입력 SNR이며 Eb/N0로 변환하지 않았다.
- Seed: [12345, 67890], seed/SNR별 200회. 설정당 SNR별 총 400회.
- 공통: RB 24, SCS 30 kHz, FFT 1024, SISO. 실제 자원 사용량은 채널별 로그 참조.
- PDSCH/PUSCH: TABLE1 MCS 5/10/20, TABLE2 MCS 20으로 QPSK/16QAM/64QAM/256QAM 대표점. TABLE3 MCS 5 추가.
- HARQ: QPSK, MMSE, IR/Chase, 최대 4회 전송. OLLA: SNR별 독립 실행, 각 pass의 최종 결과만 수집.
- 전체 채널/지원 형식의 대표 경로를 포함한다. 모든 MCS·RB·UCI 크기 조합을 전수 검사한 것은 아니다.

## 범위와 구현 한계

다음 표는 수집기가 지원하는 전체 범위다. 이번 실행에서 선택한 설정은 아래 대표 지표 표와 manifest의 cases 목록에 명시했다.

| 대상 | 포함 범위 |
|---|---|
| PBCH | Polar AWGN |
| PDCCH | CSS AL 4/8/16, USS AL 1/2/4/8/16, blind decoding |
| PDSCH | coded 기준, DMRS SISO ZF/MMSE, IR/Chase HARQ, SISO OLLA |
| PUSCH | SISO CP/DFT-s, ZF/MMSE, IR/Chase HARQ |
| PUCCH | Format 0/1 UCI 1/2 bits, Format 2/3 UCI 3/11 bits |
| PRACH | SHORT/LONG, preamble detection 및 TA |
| CSI-RS / SRS | CSI-RS row 1~4, SRS comb 2/4 |
| 보조 기준 | uncoded BER 4변조, legacy OFDM NONE/LDPC/POLAR |

MIMO·beamforming 경로는 AWGN 설정을 받아도 자체 공간/페이딩 채널을 생성하므로 순수 AWGN 자료에서 제외했다. UL 4포트 코드북은 AWGN 자체를 지원하지 않는다. ULPC는 링크 잡음 측정이 아닌 전력 제어 시계열이다.

PUCCH 0/1은 근사 Zadoff–Chu 기반 시퀀스이고, 2/3은 원래 short-block 코딩 대신 Polar를 사용한 연구 모델이다. CSI-RS row 매핑도 현재 코드의 단순화된 구현 범위를 측정한다. 다수 물리채널은 RE 도메인이며 CP/시간영역 전파 효과 검증으로 해석하지 않는다. 이 자료는 현재 Tx/Rx 구현의 기준 측정이며 3GPP 적합성 인증 결과가 아니다.

## 지표 해석

- CSV 한 행은 case/seed/SNR/metric 하나다. 원본 출력 문자열은 reported_value에 보존했다.
- BER/BLER/UCI BER/preamble error는 서로 다른 모집단이므로 채널 간 하나의 평균으로 합치지 않았다.
- PDCCH ber_detected는 검출된 DCI에 한정한다. 검출 0건일 때 C 코드가 출력하는 0은 BER 성능 증거가 아니다. p_detect/p_miss/p_false_alarm을 함께 읽어야 한다. false alarm은 이 구현의 trial 분류율이며 빈 검색공간당 확률이 아니다.
- HARQ ber_final은 최종 복호 BER, bler_first/harq는 첫 전송/최종 TB 실패율, avg_tx는 TB당 전송 횟수다.
- CSI-RS/SRS MSE는 dB, TA MAE는 samples. legacy 시험의 반복 단위는 NUM_OFDM_SYMBOLS다.
- 출력 반올림 정밀도를 넘는 정확한 오류 개수나 신뢰구간을 역산하지 않았다. 관측 오류 0은 실제 오류율 0을 뜻하지 않는다.
- SNR별 같은 RNG 흐름에서 연속 측정하므로 그리드가 바뀌면 후속 SNR 결과도 달라질 수 있다. OLLA 두 pass는 서로 다른 난수 표본이며 고정 MCS BLER나 수렴 보장으로 해석하지 않는다.

## 대표 지표

아래는 seed별 출력의 산술평균이다. MSE(dB)는 선형 전력으로 평균한 뒤 dB로 변환했다. 모든 지표와 중간 SNR은 [results.csv](results.csv)에 있다.

| Case | 지표 | -10 dB | 20 dB |
|---|---|---:|---:|
| pdsch_dmrs_qpsk_mmse | bler | 1 | 0 |
| pdsch_dmrs_16qam_mmse | bler | 1 | 0 |
| pusch_tp0_16qam_mmse | bler | 1 | 0 |
| pusch_tp1_16qam_mmse | bler | 1 | 0 |
| pdsch_dmrs_64qam_mmse | bler | 1 | 0.0025 |
| pdsch_dmrs_256qam_mmse | bler | 1 | 0.9025 |
| pdsch_harq_ir | bler_harq | 1 | 0 |
| pusch_tp1_harq_ir | bler_harq | 1 | 0 |
| pdsch_harq_chase | bler_harq | 1 | 0 |
| pusch_tp1_harq_chase | bler_harq | 1 | 0 |

## 재현

저장소 루트에서 실행:

```bash
python3 PHY/tests/run_awgn_campaign.py --output /tmp/awgn-repeat --trials 200 --seeds 12345 67890 --snr-start -10 --snr-end 20 --snr-step 5 --case pdsch_dmrs_qpsk_mmse --case pdsch_dmrs_16qam_mmse --case pusch_tp0_16qam_mmse --case pusch_tp1_16qam_mmse --case pdsch_dmrs_64qam_mmse --case pdsch_dmrs_256qam_mmse --case pdsch_harq_ir --case pusch_tp1_harq_ir --case pdsch_harq_chase --case pusch_tp1_harq_chase
```

[manifest.json](manifest.json)에 소스·바이너리 SHA-256, Git 상태, 실행별 설정/로그 SHA-256이 있다. 변경 중인 작업 트리에서 측정했으므로 Git commit만으로 동일 소스를 식별할 수 없다. configs/와 logs/에 실행 입력과 원본 출력을 보존했다. 동일 플랫폼·바이너리·설정에서 재현한다.

실행 경로 근거: `PHY/src/main.c`, `pbch.c`, `pdcch.c`, `pdsch.c`, `pusch.c`, `pucch.c`, `prach.c`, `csi_rs.c`, `srs.c`. 외부 공개 측정치를 혼합하지 않았다.
