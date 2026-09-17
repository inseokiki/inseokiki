# Todo (5G NR PHY 개인 프로젝트)

> `CLAUDE.md`/`AGENTS.md`의 "작업 기록 원칙"에 따른 세션별 작업 계획/진행/결과 기록 파일.
> 새 작업 시작 시: 아래에 체크 가능한 항목으로 계획 작성 → 진행하며 체크 → 완료 후 "완료" 섹션에 요약 추가.
> 완료된 기능의 상세 구현/실측 결과는 여기 남기지 않고 `docs/analysis/history.md`(월별 파일로 분리됨)에 기록한다.

---

## 진행 중

- [ ] AWGN 환경 전체 시뮬레이션 데이터 정리(Tx/Rx 모듈 검증용) —
  사용자 요청(2026-09-02), "어느정도 시험되면"이라는 조건부 — 현재는
  아직 시점이 아님. 착수 시 정확한 범위(어떤 채널/모드를 포함할지,
  출력 형식)를 먼저 확인할 것.
- [ ] `test_ldpc.c`/`test_polar.c`/`test_mumimo.c`의 UT-06(독립 참조
  벡터) 미해결 — 2026-09-10 세션에서 한계를 주석으로만 명시, 실제
  외부/독립 참조 벡터는 아직 추가 안 됨(위 "완료" PHY-03 항목 참조).
- [ ] CSI 보고 확장 — Type I SP 4-port(2026-08-31)/8-port(2026-08-31)는
  완료, Type II 코드북은 아직 미구현(구 "다음 후보" 목록에 묻혀있던
  항목을 2026-09-11 문서 정리 세션에서 여기로 이동 — 착수된 적 없음).
- [ ] TDL의 Doppler/시간상관 지원 — Phase 1(정확 프로파일, 아래 "완료"
  참조)에서 의도적으로 미룬 항목. 지금은 매 트라이얼/HARQ attempt에서
  채널이 block-flat으로 고정(재추첨만 attempt마다) — 진짜 시간축
  변화(Doppler spectrum, Jakes 등)가 없다. 착수 전 설계 필요(TDL_MAX_TAPS
  구조는 유지 가능하지만 "한 번 draw = 한 trial 내내 고정"이라는 현재
  53개 호출부 전체의 기존 가정을 깨는 작업이라 Phase 1보다 훨씬 큼).
- [ ] TDL의 MIMO 공간상관 — Phase 1에서 의도적으로 미룬 항목. 지금은
  안테나쌍마다 `tdl_draw()`를 독립 호출한 뒤(예: 4×4는 16번) 별도
  Kronecker 상관(`mimo_apply_tx_correlation_*`)을 사후에 곱하는 구조 —
  진짜 3GPP 클러스터/레이 기반으로 결합된 MIMO 채널이 아니다. 이
  프로젝트 MIMO+TDL 조합 함수 전체(다수)의 재설계 수준이라 착수 전
  설계 필요, Phase 1/2보다 훨씬 큼.
- [ ] 실제 waveform(시간영역 OFDM) 경로 — 현재 다수 PDSCH/PUSCH/TDL
  시뮬레이션은 RE/QAM 심볼 도메인 모델이라 CP 부족 ISI, 시간영역
  다중경로 convolution, CFO/phase noise/sampling offset, PA
  비선형성 등이 빠져있다. `tasks/lls_analysis_handoff.md` P2-3
  (2026-08-03)에서 지적된 뒤 착수된 적 없음 — 기존 빠른 RE-domain
  경로를 유지한 채 `ABSTRACTION_LEVEL=RE|WAVEFORM` 병렬화가 제안된
  방향. 2026-09-11 문서 정리 세션에서 미완료로 재확인, 여기로 이동.

---

## 완료

- [x] UL_CB_4PORT ACK 조기 종료·다중 CB HARQ 검증 — 2026-09-18.
  실제 복호/CRC를 사용하는 8개 조합을 추가하여 32개 통과.
  CB별 버퍼 주소·내용 보존을 검사하고 ACK 무시, CB 버퍼 혼용,
  CB 재조립 오프셋 오류를 임시 주입했을 때 검출 여부 확인.

- [x] UL_CB_4PORT RE별 LLR/HARQ 배선 회귀 테스트 — 2026-09-18.
  rank 1~4 × flat/TDL × 비-HARQ/IR/Chase의 24개 조합 확인.
  평균 분산 재도입·재전송 버퍼 초기화·잘못된 IR RV의 세 결함을
  임시 주입하면 모두 실패함을 확인. 수치 테스트 드라이버에 등록.

- [x] UL_CB_4PORT 확장 경로의 RE별 LLR 신뢰도 보완 — 2026-09-18.
  TDL/flat HARQ에서 검출 잡음분산을 RE별로 전달. 관련 5개 실행 조합과
  modulation 수치 테스트 통과. TDL 비-HARQ 100회 BLER 0.47→0.27;
  조건 및 HARQ 비교의 난수열 제약은 월별 이력 참조.

> 상세 구현/실측 결과·재현 조건·버그 원인은 전부 `docs/analysis/history.md`(및 그 아래
> `history/2026-07.md`/`2026-08.md`/`2026-09.md`)로 이관되어 있다. 아래는 무엇이 언제
> 끝났는지 찾기 위한 1~2줄 요약이며, 상세는 각 항목의 날짜로 history.md에서 찾는다.

- [x] UL_CB_4PORT 전체 TPMI 및 TDL/HARQ 확장 — 2026-09-18.
  rank별 28/22/7/5개 TPMI, flat/TDL 및 HARQ 조합 지원.
  회귀 128/128, 수치 테스트 20/20.

- [x] PUSCH UL 4포트 코드북 flat MIMO — 2026-09-17. TS 38.211
  §6.3.1.5의 TPMI 부분집합(rank 1~4), 4Tx/4Rx, 하나의 TB를 선택
  레이어에 분산하고 DMRS LS/MMSE 복호. 회귀 127/127, 수치 테스트 20/20.

- [x] OLLA를 CL_32PORT로 확장 — 2026-09-17. rank 1~4 유효 SNR,
  채널 기준 RI/PMI 선택, 선택 rank의 capacity를 MCS에 반영.

- [x] PDSCH OLLA를 CL_8PORT로 확장 — 2026-09-17. RI/PMI 선택과 같은
  용량식을 유효 SNR 계산에 재사용하고, 8Tx/4Rx rank-1/2 시계열 경로를
  배선. 고상관 2,000회 실행에서 BLER 0.9590→0.1075(목표 0.10), 전체
  회귀 121/121, 수치 단위 테스트 18/18.

- [x] 설정 입력·실행 라우팅 검증 및 회귀 스크립트 파일명 수정 — 2026-09-17.
  누락된 설정 파일, 잘못된 숫자/키/BW-SCS/채널·모드를 거부하고, `/` 포함
  라벨의 설정 생성 실패와 TDL-D 거짓 통과를 해결. 회귀 119/119 통과.

- [x] OLLA를 CL_4PORT로 확장 — 2026-09-15. 착수 전 사용자와 두 설계
  갈림길 확인: (1) OLLA 오프셋은 RI/PMI 선택 자체에 영향 안 줌(순수
  채널 기준 그대로, 다른 OLLA 모드들의 "MIMO 검출손실 미반영" 철학과
  동일선상), (2) 선택된 rank의 프리코딩 이득은 MCS 선택에 반영(rank-1/
  rank-2가 다른 MCS를 받을 수 있음). `codebook.c`에 신규
  `codebook_type1_sp_4port_effective_snr_db()` — RI/PMI 선택기 내부
  capacity 공식(rank1_capacity_bps/rank2_capacity_bps로 공용 추출,
  선택기 자체 동작 무변경, 기존 회귀/단위테스트로 무변경 확인)을 이미
  선택된 후보 하나에 대해 재계산해 유효 per-layer SNR[dB]로 변환.
  신규 `run_pdsch_olla_cl_4port_simulation()`(SM_4X4 OLLA 뼈대 + CL_4PORT
  Adaptive 시나리오 검출 로직 결합) — 결합 ACK(선택된 rank 레이어 전부
  성공), Tx 공간상관 지원(CL_4PORT 본연의 기능이라 유지). `config_parser.c`
  OLLA 화이트리스트/`main.c` dispatch에 CL_4PORT 추가. **실측**: 개루프
  BLER=0.758(목표 0.10과 크게 이탈) → OLLA 폐루프 BLER=0.102로 정확히
  수렴 확인(SNR=10dB, i.i.d. 4x4, 2000트라이얼). 고 Tx 공간상관(0.9)
  조합에서 평균 rank≈1.8로 rank-1/rank-2 두 분기 모두 실행됨을 확인,
  이 경우도 OLLA가 정상 수렴. `regression_test.sh` 신규 2건(기본 스모크
  + 고상관 rank-1 분기 커버) + 기존 "OLLA+CL_4PORT는 미지원" expect_fail을
  CL_8PORT로 교체(CL_4PORT는 이제 지원되므로), 103/104(무관한 기존 버그
  유지, 케이스 수 102→104). CL_8PORT/CL_32PORT는 후속으로 분리 등록.
  상세는 `docs/analysis/history.md` 참조.
- [x] TDL을 TS 38.901 정확 프로파일로 교체 (Phase 1: delay/power/LOS,
  Doppler·공간상관은 후속 — 위 "진행 중" 참조) — 2026-09-15. 사용자와
  범위 합의(Phase 1만, TS 38.901 원문은 공식 아카이브에서 이 세션이
  직접 curl) 후 `3gpp.org/ftp/Specs/archive/38_series/38.901/38901-h10.zip`
  다운로드, `3gpp/38901-h10/38901-h10.docx`로 저장(기존 3gpp/ 컨벤션과
  동일). Table 7.7.2-1~5(TDL-A/B/C/D/E)는 LDPC/Polar 표와 달리 OLE/수식
  이미지 없이 순수 텍스트라 zipfile+regex로 직접 추출, 재추출본과
  bit-exact 일치를 프로그램적으로 재확인(탭 수 A=23/B=23/C=24/D=13+LOS/
  E=14+LOS, D/E의 K-factor 13.3dB/22dB는 표 자체의 note 텍스트에서 확인
  — 두 dB값의 차이와 정확히 일치함도 대수적으로 검증). §7.7.3 delay
  scaling 수식(7.7-1)은 원문에서 이미지라 픽셀 확인은 못했고 주변 설명
  텍스트(정규화delay×원하는delay spread)로만 확인 — 기존 코드가 이미
  이 형태를 가정하고 있어 구조 변경 없음. 신규 `tdl_tables.c`/`.h`
  (`ldpc_tables.c` 컨벤션, 로직 없이 데이터만). `tdl.h`의 `tdl_channel_
  init()`에 `TDL_PROFILE` 문자(A~E) 인자 추가(호출부 41곳 전부 기계적
  갱신) — `tdl_draw()`/`tdl_freq_response()`/`tdl_channel_apply()` 시그니처는
  그대로라 나머지 호출부는 무수정. TDL-D/E는 tap0에 LOS(Rician) 평균
  성분 추가(위상은 `atan2(randn(),randn())`로 매 draw마다 재추첨 — "매
  호출=새 realization"이라는 이 프로젝트 기존 관례에 맞춘 구현 선택,
  스펙이 강제하는 건 아님, `tdl.h`에 명시). 새 설정키 `TDL_PROFILE`
  (기본값 A, 사용자 선택) + config validation(A~E 아니면 `CFG_ERR`).
  **실측 스모크**: 5개 프로파일 전부 PDSCH SISO ZF에서 정상적인
  waterfall BLER 확인, TDL-D/E(LOS)가 순수 NLOS A/B/C보다 훨씬 빠르게
  수렴(물리적으로 타당 — Rician 강한 지배경로). `regression_test.sh`에
  신규 2건(TDL_PROFILE=D 스모크로 LOS 분기 커버, 잘못된 TDL_PROFILE
  값 `expect_fail`) 추가, 101/102(무관한 기존 버그 1건 유지, 케이스 수
  100→102). `run_numeric_tests.sh` 17/17(`test_link_adaptation`이
  `config_parser.o`를 링크해서 새 `tdl_tables.o` 의존성 누락으로 잠깐
  링크 실패했던 것 수정). 상세는 `docs/analysis/history.md` 참조.
- [x] `PHY/src/ber_sim.c` 정리(삭제, 사용자 확인 후) — 2026-09-14.
  `c_Makefile`에 전혀 포함 안 된 고아 파일이었고, 자체 `int main()`이라
  있는 그대로는 `c_Makefile` 편입 자체가 불가능(메인 `lls_sim_c`도
  자기 `main()`이 있어 심볼 충돌) — 편입하려면 별도 빌드 타겟 신설이나
  `PHYSICAL_CHANNEL=...` 디스패치 함수로 재작성이 필요했다. 조사 중
  `main.c`에 이미 동일 기능(비부호화 QAM BER, `PHYSICAL_CHANNEL=BER` →
  `run_ber_sim()`, main.c 내부 정의)이 배선돼 있어 완전히 중복임을 확인
  — 편입해도 새로 얻는 게 없어 삭제로 결정. `config/ber_config.txt`
  (이 파일 전용 설정)와 `config_parser.c`/`.h`의 `numBits`/`NUM_BITS`
  필드(이 파일만 참조, 다른 곳에서 미사용 확인)도 함께 제거. 별도
  프로젝트인 `BER/`(독립 BER 툴, `PHY` 코드 미사용)와는 무관 — 안 건드림.
  `make -f c_Makefile` 클린 재빌드, `run_numeric_tests.sh` 17/17,
  `regression_test.sh` 99/100(무관한 기존 버그) 유지.
- [x] `PHYSICAL_CHANNEL=ULPC` `regression_test.sh` 커버리지 추가 — 2026-09-14.
  이전엔 CLI 스모크 실행으로만 확인했을 뿐 정식 회귀 케이스가 0개였음.
  5개 케이스 신규: 고정 PL 스모크, 시변 PL(Gauss-Markov, `UL_PC_PL_VAR_*`
  — 이전엔 전혀 실행 경로를 안 타던 브랜치) 스모크, PL=150dB에서
  P_CMAX 클램핑 분기가 실제로 발동하는지(출력 텍스트 "P_CMAX 클램핑
  발생" 매칭) 확인하는 동작 케이스, `UL_PC_PL_VAR_STD_DB`/`UL_PC_PL_
  VAR_CORR` 범위 밖 값 2건에 대한 `expect_fail`(기존 `config_parser.c`
  검증 로직 자체는 있었지만 회귀로 확인된 적 없었음). TPC 결정/f(i)
  누산기의 수치 정확성은 이미 `test_link_adaptation.c` 단위테스트가
  커버해서 이번 케이스들은 숫자 재검증이 아니라 CLI 배선·설정검증
  회귀에 집중. `regression_test.sh` 99/100(신규 5건 전부 통과, 유일한
  실패는 무관한 기존 PUCCH 라벨 버그) — 전체 케이스 수 95→100.
- [x] RE별 effective noise variance 기반 LLR — pdcch.c/pucch.c/pbch.c 검토
  — 2026-09-14(같은 세션 후속). `run_pbch_fading_simulation()`의 ZF 분기
  (`env=N0*mean(1/|h|²)`)와 `run_pdcch_fading_simulation()`의 TX 후보
  ZF/MMSE 분기(둘 다 평균)를 RE별 배열로 교체 — PDCCH는 blind-decoding
  후보 루프 안에서 TX 후보만 `qam_demap_llr_re()`, 나머지 후보는 순수
  AWGN이라 기존 스칼라 유지. PBCH A/B(TDL, ZF, SEED=12345, NUM_TRIALS=5000)
  결과 -8dB에서 BLER 0.9448(평균) → 0.3616(RE별), 0dB에서 0.1770 →
  0.0008 — PDSCH SISO 사례와 유사한 규모의 개선. `run_pucch_format2_
  simulation`/`_format3_simulation`(AWGN 전용, TDL 없음)과 `_format3_tdl_
  simulation`/`_tdl_harq`(DFT-s-OFDM 필수 — PUSCH의 tdl_dfe/turbo와
  동일하게 post-IDFT 평균이 이미 정확)는 검토 후 대상 아님으로 확인,
  코드 변경 없음. `run_pdcch_fading_simulation()`의 기존 "Parseval-
  approximate for TDL" 주석은 근거가 부정확했음을 확인해 갱신(Parseval은
  총 에너지 보존을 보장할 뿐 심볼별 LLR 신뢰도와는 무관). `run_numeric_
  tests.sh` 17/17, `regression_test.sh` 94/95(무관한 기존 버그) 유지.
  상세는 `docs/analysis/history.md` 참조.
- [x] RE별 effective noise variance 기반 LLR — 2026-09-14.
  `modulation.c`에 `qam_demap_llr_re()`(RE별 noise_var[] 배열 버전) 신규 +
  `test_modulation.c` 검증 2건. `pdsch.c` TDL 계열 19개 함수(26개 호출부:
  SIMO_MRC/SM_2X2/SM_4X4/CL_4·8·32PORT/EIGEN_16PORT/BEAM_MGMT 전 조합 ×
  TDL/HARQ) + `pusch.c` 10개 함수(12개 호출부: SISO/SM_2X2/UL_EIGEN_BF
  1·2·4TX × TDL/HARQ)의 `nv_sum[l]/=n` 평균화를 정확한 RE별 배열로 교체.
  `pusch.c`의 DFT-s-OFDM 전용 2개 함수(tdl_dfe/tdl_turbo, 6개 호출부)는
  의도적으로 미전환 — 검토 결과 post-IDFT 도메인에서는 평균값 자체가
  이미 정확한 공식(모든 RE 잡음이 IDFT로 균등 혼합됨)이라 대상 아님.
  `run_pdsch_tdl_simulation()`(SISO+TDL+ZF) 첫 전환 A/B에서 16dB SNR
  BLER 0.7383(평균, 사실상 error floor) → 0.0020(RE별)로 실측 — 당초
  예상한 "calibration 저하" 수준이 아니라 실제 error floor였음이 처음
  드러남. `run_numeric_tests.sh` 17/17, `regression_test.sh` 94/95(유일한
  실패는 무관한 기존 스크립트 버그, 별도 등록) 유지. `pdcch.c`/
  `pucch.c`/`pbch.c`의 동일 패턴은 이번 범위 밖 — "진행 중"에 별도 등록.
  상세는 `docs/analysis/history.md` 참조.
- [x] TBS를 TS 38.214 §5.1.3.2 표준 절차로 교체(`tbs.c`/`tbs.h`/`test_tbs.c` 신규) —
  2026-09-14. PDSCH/PUSCH 전 `run_*` 함수(37+18곳)의 `tbsz=(int)(max_dbits*cr)` 직접
  곱셈을 Table 5.1.3.2-1 기반 Step 2-4 quantization으로 교체, N_RE 산출(Step 1)은
  프로젝트 기존 방식 유지(스코프 밖, `tbs.h`에 명시). `run_numeric_tests.sh` 17/17,
  `regression_test.sh` 94/95(나머지 1건은 무관한 기존 스크립트 버그, 위 "진행 중" 참조).
- [x] SU-MIMO codebook 기하 검증(`test_codebook_geometry.c` 신규) — 2026-09-11. `PHY_UNIT_VALIDATION_PLAN.md` §2 "SU-MIMO 검출·EVD·codebook" 행 전체 완료(EVD/검출/RI-PMI/기하 전부 커버). 4/8/32-port 전 rank 코드워드 4544개 전수 단위노름·직교성 확인(<1e-9). `run_numeric_tests.sh` 16/16, ASan/UBSan 클린.
- [x] `PHY_UNIT_VALIDATION_PLAN.md` §5 3단계 "HARQ·적응 상태" 그룹(마지막 그룹) — TB·CB 격리(`test_ldpc.c` 확장)/RI·PMI(`test_codebook_ri_pmi.c` 신규)/OLLA·ULPC(`test_link_adaptation.c` 신규, `ul_power_ctrl.c`의 `tpc_decide()`를 `ulpc_tpc_decide()`로 노출)/빔관리(`test_beam_mgmt.c` 신규) — 2026-09-11. §5 3단계 전체 완료. "재전송 상한"만 구조적으로 분리 불가로 통합 테스트 커버리지 유지(상세는 `docs/analysis/history.md`). `run_numeric_tests.sh` 15/15, ASan/UBSan 클린.
- [x] `PHY_UNIT_VALIDATION_PLAN.md` §5 3단계 "코딩·rate matching"(`test_ldpc.c`/`test_polar.c` 확장) + "추정·검출"(`test_channel_estimation.c`/`test_mimo_detection.c` 신규) — 2026-09-11. `run_numeric_tests.sh` 12/12, ASan/UBSan 클린.
- [x] `PHY_UNIT_VALIDATION_PLAN.md` §5 2단계 잔여분 — 기초 행렬(`herm4x4_eig`)·채널 공간상관 직접 단위 테스트 — 2026-09-10. `run_numeric_tests.sh` 10/10.
- [x] `PHY_UNIT_VALIDATION_PLAN.md` §5 2단계 — CRC/QAM-LLR/OFDM/DFT-precode 직접 단위 테스트 — 2026-09-10. `run_numeric_tests.sh` 8/8.
- [x] `lab/PHY_REVIEW_2026-09-10.md` 검토 기반 PHY-01~06 보강 — 2026-09-10. PUCCH 설정-실행 불일치, 빔관리 비교기준 재명명, 수치 하네스 결함 6건(UT-01~04 코드 수정) 중 4건 수정, SEED 재현성 신설, 문서 동기화. 타겟 회귀 18/18 + 수치 테스트 4/4.
- [x] 빔 관리 P1->P3->P2를 TDL/HARQ와 결합 — 2026-09-10. `regression_test.sh` 92/92.
- [x] MU-MIMO K>2 확장(K=2→4, Nt=4) — 2026-09-09. `regression_test.sh` 90/90 유지.
- [x] Polar 코드 표준 정합화 Phase 1-4(신뢰도 시퀀스/입력 인터리빙/PC 비트/CA-SCL 디코더) + PBCH/PDCCH/PUCCH/main.c 전 채널 연결 — 2026-09-04. `regression_test.sh` 90/90 유지.
- [x] 빔 관리 P1 -> P3(UE Rx 빔 정제) -> P2(gNB Tx 빔 재정제) — 2026-09-03. 구현 중 버그 2건 발견·수정. `regression_test.sh` 90/90.
- [x] OLLA를 SM_4X4로 확장 — 2026-09-03. `regression_test.sh` 88/88.
- [x] PUSCH UL SM_2X2에 flat+HARQ 추가 — 2026-09-03. `regression_test.sh` 87/87.
- [x] P0-2c 최종 완료 — `nr_sch` 다중 코드블록 세그멘테이션을 HARQ 있는 나머지 14곳(pdsch.c 9 + pusch.c 5)으로 확장 — 2026-09-03. `regression_test.sh` 87/87.
- [x] P0-2c 후속 — 다중 코드블록 세그멘테이션을 HARQ 없는 나머지 35개 함수로 확장 — 2026-09-03. `regression_test.sh` 87/87.
- [x] P0-2c — TS 38.212 §5.2.2 다중 코드블록 세그멘테이션 + CRC24B, §5.4.2.1 `E_r` 배분, §5.5 concatenation(모듈+파일럿 통합) — 2026-09-03. K'=B'/C 비정수분할 반올림 규칙 미확정, `exit(1)` 유지가 사용자 확인된 정책. `regression_test.sh` 80/80.
- [x] TS 38.212 §5.4.2.1 표준 rate matching(P0-3) — 2026-09-02. `regression_test.sh` 78/78.
- [x] NR BG1/BG2 QC-LDPC 이식(P0-1) — 2026-09-02. 단일 코드블록 범위, 세그멘테이션은 P0-2c 후속. `regression_test.sh` 78/78.
- [x] LDPC BP decoder edge-message 교정 — 2026-09-02. self-feedback 구조를 edge별 extrinsic으로 교정. `regression_test.sh` 78/78.
- [x] UL Eigen-BF를 K=4(최대 4레이어)로 확장 — 2026-09-02. `regression_test.sh` 78/78.
- [x] UL Eigen-BF(2-Tx)에 랭크 적응 추가 — 2026-09-01. `regression_test.sh` 74/74.
- [x] UL Eigen-BF(2-Tx)를 TDL/HARQ로 확장 — 2026-09-01. `regression_test.sh` 74/74.
- [x] UL Eigen-BF(1-Tx)를 TDL/HARQ로 확장 — 2026-09-01. 채널 모델 설계 결함 발견·수정(안테나별 독립 TDL → 공유 클러스터 게인 모델). `regression_test.sh` 72/72.
- [x] UL 2계층(SM_2X2) 수신 빔포밍(Eigen-BF, 비-코드북) — 2026-09-01. `regression_test.sh` 70/70.
- [x] UL SIMO 수신 빔포밍(Eigen-BF, 비-코드북, 전력반복법) — 2026-09-01. `regression_test.sh` 68/68.
- [x] PUSCH UL SM_2X2에 HARQ 추가(TDL 전용) — 2026-09-01. `regression_test.sh` 66/66.
- [x] 빔 관리를 TDL/HARQ로 확장 — 2026-09-01. TDL이 빔 순위에 영향 없음을 대수적으로 증명. `regression_test.sh` 65/65.
- [x] MU-MIMO를 TDL/HARQ로 확장(K=2 고정) — 2026-09-01. `regression_test.sh` 62/62.
- [x] OLLA를 SIMO_MRC/SM_2X2로 확장 — 2026-09-01. 블록-플랫 페이딩에서 MCS 테이블 하한(floor) 발견(버그 아님). `regression_test.sh` 60/60.
- [x] PUSCH UL SU-MIMO(SM_2X2) 추가 — 2026-09-01. DL/UL 구조적 비대칭 해소. `regression_test.sh` 56/56.
- [x] STRUCTURE.md 전면 갱신 — 문서 드리프트 해소 — 2026-09-01. 문서 전용 변경.
- [x] LLS 완성도 점검 — HARQ dispatch가 여러 MIMO_MODE를 무시하고 조용히 SISO로 떨어지던 정확성 결함 발견·수정 — 2026-09-01. `regression_test.sh` 52/52.
- [x] 빔 관리(SSB/CSI-RS 기반 P1 절차) 신규 착수 — 2026-09-01. `regression_test.sh` 50/50.
- [x] MU-MIMO(Zero-Forcing Beamforming) 신규 착수 — 2026-09-01. Nt=4, K=2, 평탄 페이딩. `regression_test.sh` 49/49.
- [x] OLLA(Outer Loop Link Adaptation) 신규 착수 — 2026-09-01. Shannon+3dB gap 근사가 이 프로젝트 LDPC 코덱엔 낙관적임을 실측 확인(버그 아님). `regression_test.sh` 48/48.
- [x] PRG-평균 타깃 전용 MMSE(Wiener) 추정기 — 2026-09-01. subband/wideband 평균 타깃 재유도, DFT 대비 NMSE 개선(20dB 1.11e-3 vs 1.75e-3). `regression_test.sh` 48/48.
- [x] CL_4/8/32PORT TDL에 채널추정(LS/MMSE/DFT) 연결 — 2026-09-01. `CHAN_EST_METHOD` 공용 설정. `regression_test.sh` 48/48.
- [x] EIGEN_16PORT Subband(PRG) 프리코딩 확장 — 2026-08-31. subband가 wideband 대비 BLER 개선(20dB 0.76→0.40) 확인.
- [x] EIGEN_16PORT TDL의 DFT 채널추정 경로 성능 최적화 — 2026-08-31. 선형연산 특성 이용 사전계산, 런타임 28.2초→1.56초(약 18배).
- [x] EIGEN_16PORT에 imperfect CSI 배선(TDL + LS/MMSE/DFT 채널추정) — 2026-08-31. wideband 평균화로 LS/MMSE/DFT 간 최종 BLER 차이가 작음(설계 선택의 결과, 버그 아님).
- [x] 채널추정 확장 — MMSE(Wiener)/DFT 기반 범용 함수(`channel_estimation.c`/`.h`) 신규 — 2026-08-31. `regression_test.sh` 48/48, 기존 LS 시그니처 불변.
- [x] Massive MIMO — Type I SP 32-port 코드북(N1=4,N2=4,O1=4,O2=4,P=32), rank 1~4 — 2026-08-31. TS 38.214 Rel-18 Type I SP는 64포트 미정의 확인 후 "64안테나=32포트×2편파"로 스코프 재정의(사용자 확인). `regression_test.sh` 48/48.
- [x] CL_32PORT Tx 공간상관(Kronecker N1=N2=4 2D URA) 모델 — 2026-09-01. `regression_test.sh` 48/48.
- [x] CL_32PORT TDL/HARQ 변형 — 2026-09-01. `regression_test.sh` 48/48.
- [x] Eigen-Beamforming(SVD) 16-port, 비-코드북 개루프 방식 — 2026-08-31. 신규 4×4 Hermitian 고유분해(복소 Cyclic Jacobi) 구현, 배선 전 독립 검증(재구성 오차 최대 7.7e-15). `regression_test.sh` 48/48.
- [x] CSI 보고 확장 — Type I SP 8-port 코드북(N1=4,N2=1,O1=4,P=8) — 2026-08-27 착수, 2026-08-31 rank-2/시뮬레이션 배선/TDL·HARQ/Tx 공간상관까지 전체 완료. `regression_test.sh` 48/48.
- [x] CL_4PORT 교차편파(XPD) 누설 상관 모델(`SPATIAL_CORR_XPOL`, Kronecker R_pol⊗R_ant) — 2026-08-27. 부작용으로 `codebook.c` RI/PMI 선택기 기존 버그 2건 발견(아래 2건 참조).
- [x] UL CLPC에 채널 페이딩/이동성(시변 PL, `UL_PC_PL_VAR_STD_DB`/`UL_PC_PL_VAR_CORR`, Gauss-Markov AR1) 추가 — 2026-08-27.
- [x] CL_4PORT rank-1/rank-2 코드북 전력 정규화 불일치 수정 — 2026-08-27(사용자 확인 후). `norm` `0.5`→`0.5/√2`. 2026-08-02 "R1선택률=0%" 결론의 근본 원인이었음이 확정. `regression_test.sh` 48/48.
- [x] `codebook.c` RI/PMI 선택기 2×2 Gramian 역산의 catastrophic cancellation 방어 — 2026-08-27(XPD 검증 중 발견). `regression_test.sh` 48/48, 위 정규화 이슈와 별개 결함.
- [x] PBCH/PDCCH에 페이딩 채널(FLAT_FADING/TDL) 변형 추가 — 2026-08-02. LLS 완성도 작업 1단계.
- [x] Group A 조합 공백 전체 완료 — 2026-08-03. SM_4X4+TDL, SM_4X4+HARQ, CL_4PORT+TDL, CL_4PORT+HARQ, PUSCH+HARQ.
- [x] P0 정합성 수정 완료 — 2026-08-03. TABLE3 fallback 제거, config validation 추가, STRUCTURE.md 라우팅 갱신, 48-case 회귀 테스트 스크립트(`PHY/regression_test.sh`) 신규 작성·전체 통과.
- [x] Mac → Tailscale → Windows 4060 PC의 WSL2 원격 개발 경로 구축 — 2026-08-03. WSL OpenSSH 포트 `22299`, SSH 키 인증, Mac 별칭 `KANG_HOME`.

2026-08-02 `CLAUDE.md` 분리 이전(2026-05-24~2026-07-22) 이력은 전부 `docs/analysis/history/2026-07.md`로 이관됨.

---

## 보류/결정 사항

- **NVIDIA Aerial SDK 연동**: 보류 (사용자 명시적 결정, 2026-07-14) — 연동하지 않기로 함
- **NTN (Non-Terrestrial Network)**: 보류 (사용자 명시적 결정, 2026-08-02) — 기존 LLS 완성도를 우선하기로 함, 새 영역 확장은 나중
