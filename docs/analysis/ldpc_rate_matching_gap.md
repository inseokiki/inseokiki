# LDPC rate matching 외부 대조: 전송 경로의 두 공백

2026-09-18. AWGN 기준 데이터 수집과 다음 투두인 독립 참조 벡터 확장을
진행하면서 발견했다. **C 전송 경로 교정과 관련 검증 완료**.
아래 원인/초기 재현은 수정 전 기록이며, 교정 결과는 마지막 절에 있다.

## 재현 결과

실제 `nr_rate_matching.c`를 임시 공유 라이브러리로 빌드하고 py3gpp 0.6.0
`nrRateMatchLDPC`와 비교했다. BG1/Zc=8, BG2/Zc=72 × RV 0~3 ×
Qm 2/4/6/8 × 선택/반복 전송, 총 64개 조합이다. filler는 7 bits,
seed는 38212542다. 인코더 영향을 분리하기 위해 임의 비트열을 입력했다.

- 현재 SCH 호출 방식: 64/64 조합에서 불일치, 합계 82,354 bits.
- 실험용 Python 어댑터: 첫 2Z 제외·filler 인덱스 이동 후 동일 C 선택 함수를
  호출하고 Qm 인터리빙을 적용하면 64/64 조합이 외부 출력과 bit-exact 일치.
- 어댑터는 진단 코드에만 있다. 기존 C 시뮬레이터가 수정됐다는 뜻이 아니다.

결과: [JSON](data/ldpc_rate_matching_audit_2026-09-18.json).
독립 참조 환경에서 재현:

```bash
python PHY/tests/reference_vectors/audit_ldpc_rate_matching.py > /tmp/ldpc-rm-audit.json
```

py3gpp 0.6.0과 NumPy가 필요하다. 스크립트가 C 컴파일러를 호출하고
임시 라이브러리를 정리한다. 출력은 진단 자료이며 기존 동작의 적합성
통과 테스트가 아니다. 참조 모듈과 C 소스 SHA-256을 JSON에 보존했다.

## 원인과 근거

1. `ldpc.c`의 `coded_size`는 full mother-code 길이 68Z/52Z다.
   이것은 내부 인코더/복호기 표현으로는 사용할 수 있다. 그러나
   `pdsch.c`·`pusch.c`·`pusch_codebook_4port_sim.c`의 전송 선택 호출도
   이 길이와 시작 포인터를 그대로 사용한다. 첫 2Z puncturing 및
   66Z/50Z circular buffer로의 좌표 변환이 없다.
2. 각 CB의 선택된 비트를 `nr_seg_concat()` 후 `qam_modulate()`에
   직접 전달한다. Qm별 전송 비트 인터리빙과 대응 수신 역변환이 없다.
   이 때문에 순환 버퍼의 부분 함수 수식만 맞아도 전체 전송 비트열은
   외부 참조와 일치하지 않는다.

표준 근거는 [TS 38.212 V18.8.0](https://www.etsi.org/deliver/etsi_ts/138200_138299/138212/18.08.00_60/ts_138212v180800p.pdf)
§5.3.2(pp.19–20), §5.4.2.1(pp.29–32), §5.4.2.2(p.32)다.
§5.3.2의 출력은 첫 2Z systematic bits를 제외한 66Z/50Z 길이다.
§5.4.2.2의 permutation은 `f[j*Qm+i] = e[i*(E/Qm)+j]`다.
외부 구현은 [py3gpp v0.6.0](https://github.com/catkira/py3gpp/tree/v0.6.0)의
`nrRateMatchLDPC.py`를 사용했다.

## 수정 순서

1. decoder의 full mother-code 표현은 유지하고, SCH 전송/수신 전용
   wrapper에서 2Z offset·filler 좌표·Ncb·Qm permutation을 함께 처리한다.
   수신 결합은 처음 2Z의 LLR=0을 유지하고 알려진 filler를 별도로 처리한다.
2. BG1/BG2, RV 0~3, Qm 2/4/6/8, E<N/E>N, filler, 복수 CB의 외부
   비트 벡터를 영구 테스트로 추가한다. Rx 역인터리빙과 HARQ 누적은
   외부 expected position/LLR로 검증한다.
3. 우선 SISO PDSCH/PUSCH AWGN에 연결하고 현재 기준 CSV와 A/B 측정한다.
   오류율은 변할 수 있으므로 종전과 동일한 BLER를 회귀 조건으로 삼지 않는다.
4. MIMO·TDL·HARQ·turbo의 select/select_soft/combine 호출을 함께 전환한다.
   현재 세 simulator 파일에서 관련 호출 161곳이 검색된다. 일부는
   여러 줄에 걸친 한 호출이므로 자동 전환 후 각 모드의 targeted 검증이 필요하다.
5. 교정 후 AWGN 데이터를 별도 디렉터리에 재수집한다. 현재 기준 자료는
   수정 전 결과로 남긴다. 기존 test_ldpc의 내부 왕복 성공만으로 교정을
   검증했다고 주장하지 않는다.

현재 자료는 구현 자체의 기준 측정으로 유효하지만 표준 NR rate-matching
성능 곡선으로 해석하면 안 된다. 인코더 외부 벡터의 성공 범위와 이 전송
비트열 불일치를 구분해야 한다.

## C 교정 결과 (2026-09-18)

`nr_sch_rate_match_select/select_soft/combine`를 `nr_rate_matching.c`에
추가했다. full mother-code 표현을 유지하는 공통 iterator가 첫 2Z를
제외하고 Ncb=66Z/50Z에서 RV/filler 선택을 수행한다. 전송 비트 위치는
CB별 Qm permutation으로 변환하며 수신은 같은 매핑을 역으로 누적한다.
추가 heap allocation은 없다. low-level `nr_ldpc_*`는 명시적 버퍼 좌표
API로 남겼다.

PDSCH 102곳, PUSCH 50곳, UL 4포트 2곳, 총 실제 호출 **154곳**을
전환했다. 초기 검색의 161줄에는 주석의 함수명도 포함돼 있었다.
터보 extrinsic feedback도 같은 Qm 순서를 사용한다. 수신의 첫 2Z는
미관측 LLR=0으로 남고, filler는 기존 decoder가 알려진 0으로 처리한다.

- C 수치 테스트 3종 통과: LDPC의 C=1/C=2 무잡음 왕복, 신규 SCH 매핑,
  UL 4포트 integration(32개 단일 TB + 6개 연속 4-TB 시퀀스).
- 신규 SCH 테스트: 외부 위치 벡터 64조합, Tx bit/soft feedback/Rx scatter,
  Chase·서로 다른 RV의 IR 누적, punctured/filler 위치 보존. C=3 불균등
  E_r 배분 4조합도 외부 출력과 일치. AddressSanitizer/UBSan 통과.
- 기존 진단과 동일 BG1/Z8·BG2/Z72의 별도 64조합에서도 **수정된 C 함수가
  직접** py3gpp 출력과 일치한다. [결과 JSON](data/ldpc_rate_matching_corrected_2026-09-18.json).
  Python 어댑터만 통과한 상태에서 더 나아가 실제 C 경로를 대조했다.
- 변경 모드의 targeted 회귀 23/23 통과. 전체 회귀는 실행하지 않았다.
- PUSCH TDL/RB4/MCS10/seed12345/30회, SNR 0/10/20 dB:
  turbo 1회는 noDFE 경로와 BER/BLER가 모두 같고, turbo 3회도 유한 결과로
  완료했다. 20 dB의 BLER는 이 표본에서 0.0667→0.0333이며 일반 성능
  개선으로 단정하지 않는다.

측정 자동화와 외부 벡터 생성만 Python을 사용한다. PHY 변경 및 영구
수치 테스트는 C이고, 생성된 header 덕분에 C 테스트 실행에는 Python
패키지가 필요하지 않다. TB CRC 선택·segmentation의 외부 대조와 Polar
후속 범위는 이 교정과 별개로 남아 있다.
