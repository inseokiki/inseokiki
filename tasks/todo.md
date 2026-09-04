# Todo (5G NR PHY 개인 프로젝트)

> `CLAUDE.md`/`AGENTS.md`의 "작업 기록 원칙"에 따른 세션별 작업 계획/진행/결과 기록 파일.
> 새 작업 시작 시: 아래에 체크 가능한 항목으로 계획 작성 → 진행하며 체크 → 완료 후 "완료" 섹션에 요약 추가.
> 완료된 기능의 상세 구현/실측 결과는 여기 남기지 않고 `docs/analysis/history.md`에 기록한다.

---

## 진행 중

- [ ] OLLA를 CL_XPORT로 추가 확장 — SISO/SIMO_MRC/SM_2X2/SM_4X4는
  완료(SM_4X4는 2026-09-03, 아래 "완료" 참조). CL_XPORT(RI+PMI 적응)는
  매 트라이얼 RI/PMI 재선택과 OLLA 오프셋을 어떻게 결합할지(RI 선택
  임계값에도 반영할지, MCS만 조정할지) 추가 설계가 필요 — 단순 레이어
  수 확장(SM_4X4)과 달리 착수 전 사용자 확인 필요.
- [ ] PUSCH UL MIMO를 K>2/다중 Rx 안테나/코드북 기반(SM_4X4 상당)으로
  확장 — flat+HARQ는 완료(2026-09-03, 아래 "완료" 참조). K>2는 DL
  SM_4X4처럼 코드워드 배열로 확장 가능하지만, UL은 코드북 기반
  프리코딩(CL_4PORT 상당)이 아예 없어서(UL_EIGEN_BF는 비-코드북 Rx
  빔포밍이라 별개 축) 새 UL 코드북 설계부터 필요 — DL의 codebook.c를
  그대로 재사용할 수 있는지 UL/DL 코드북 정의가 대칭인지부터 확인
  필요.
- [ ] AWGN 환경 전체 시뮬레이션 데이터 정리(Tx/Rx 모듈 검증용) —
  사용자 요청(2026-09-02), "어느정도 시험되면"이라는 조건부 — 현재는
  아직 시점이 아님. 착수 시 정확한 범위(어떤 채널/모드를 포함할지,
  출력 형식)를 먼저 확인할 것.
- [ ] MU-MIMO를 K>2 또는 다중 Rx 안테나(진짜 다중 스트림/사용자)로
  확장 — TDL/HARQ는 완료(2026-09-01, K=2/Nt=4 고정). K>2는
  `mumimo_zf_precode()`의 2×2 폐형 역행렬을 일반 K×K 역행렬로
  교체해야 함(현재 K=2 전제로 짜여 있음).
- [ ] `PHY/src/ber_sim.c` 정리 — `c_Makefile`에 전혀 포함되지 않는
  고아 파일(2026-07-15 이후 방치, 독립 실행형 BER 툴 시도로 보임).
  STRUCTURE.md 갱신 중 발견(2026-09-01). 삭제할지 `c_Makefile`에
  편입할지 결정 필요.
- [ ] 빔 관리 P1->P3->P2(2026-09-03 완료, 아래 "완료" 참조)를 TDL/HARQ와
  결합 — 현재는 AWGN 전용(`BEAM_MGMT_RX_SWEEP=1`+`CHANNEL_MODEL=TDL`
  또는 `HARQ_ENABLE=1`은 `config_parser.c`가 명시적으로 차단 중).
  기존 `run_pdsch_beam_mgmt_tdl_simulation()`/`_harq_simulation()`과
  같은 패턴(TDL은 선택된 빔의 데이터 전송에만 적용, HARQ는 빔 선택
  자체는 트라이얼당 1회 고정)을 그대로 적용 가능할 것으로 보이나
  착수 전 확인 필요.

## 완료 (최근)

- [x] Polar 코드 표준 정합화 Phase 4(CA-SCL 디코더) + PBCH/PDCCH/PUCCH/
  main.c 전 채널 연결 — 2026-09-04 완료. CA-SCL(List size, CRC-aided
  path selection)은 3GPP 비규정 구현 선택 영역(인코더만 표준 규정,
  디코더는 이 프로젝트의 LDPC BP 디코더와 동일 지위) — Tal & Vardy
  List Decoding, Balatsoukas-Stimming LLR 도메인 경로 메트릭,
  Niu & Chen CRC-aided path selection 등 일반 문헌 알고리즘으로 구현,
  3GPP 근거 확인 절차 대상 아님. `polar_decode_scl()`을 `polar.h`에
  선언(`SCLPath`/`scl_recurse()`/`scl_pm_cost()`는 `polar.c` 내부).
  검증 중 CRC 미통과 시 최악(마지막 순회) 후보가 반환되던 버그 1건
  발견·수정(주석은 "p=0 후보 보존"이라 했지만 실제로는 매 반복
  덮어써지는 구조였음) — `p==0`일 때 별도 저장하는 방식으로 수정.
  검증(저장소 밖 스크래치 하네스): (1) SCL(L=1, CRC 미사용)이 일반
  SC와 노이즈 있는 300회 트라이얼에서 비트 단위 완전 일치(분기/pruning
  로직이 이론적으로 SC와 등가임을 증명하는 차등 테스트), (2) CA-SCL
  (L=8) BLER이 동일 SNR에서 일반 SC 대비 개선(17.4%→4.0%, K=56
  PBCH 유사 설정), (3) RNTI-masked CRC 경로선택(PDCCH 전용) 정상
  RNTI에서 500/500 정확 복호, 틀린 RNTI에서 0/500 오탐(masking이
  실제로 적용됨을 확인) — ASan/UBSan 누수·UB 없음, 전체 회귀 90/90
  유지(연결 전후 동일). 이어서 사용자 요청("다 연결해줘")으로
  PBCH(use_crc=1)/PDCCH(use_crc=1,RNTI-masked)/PUCCH 4개소·main.c
  legacy sim(use_crc=0, UCI/PUCCH는 spec상 K<=11에 별도 CRC 없어
  genie 비교 유지)까지 기존 `polar_decode()`를 전부
  `polar_decode_scl()`로 교체, list size는 `polar.h`의
  `POLAR_SCL_L=8`(구현 선택, 문헌 통상값)로 공유. PBCH AWGN 실행
  결과(SNR -6~0dB, 3000 trial)로 물리적으로 타당한 BLER 곡선 확인.
  Phase 1~3(신뢰도 시퀀스+입력 인터리빙+PC 비트)도 같은 날 완료,
  사용자 확인된 순서(1→2→3→4)대로 진행.
  **Phase 1(표)**: TS 38.212 Table 5.3.1.2-1(극성화 시퀀스 Q_Nmax,
  1024개)/Table 5.3.1.1-1(입력 인터리빙 패턴, 164개) 둘 다 로컬
  `3gpp/38212-hc0/38212-hc0.docx` 원문에서 프로그램적 추출(LDPC 표
  추출과 동일 방식) — **두 가지 독립 방법으로 교차검증**: (1) docx
  직접 파싱 결과가 3gpp-server MCP 렌더링 결과와 셀 단위로 완전 일치,
  (2) Q_Nmax 앞 32개 값이 잘 알려진 참조값과 전부 일치, (3) 두 표 모두
  C에서 실행 시점에 유효한 순열(중복/누락 없음)임을 직접 검증. 신규
  `polar_tables.h`/`.c`.
  **Phase 2(입력 인터리빙, §5.3.1.1)**: `I_IL` 플래그 기반 `Pi(k)`
  구성 — PBCH(§7.1.4)/PDCCH(§7.3.3)는 `I_IL=1`(인터리버 실제 동작),
  UCI(§6.3.1.3.1)는 `I_IL=0`(no-op) **직접 원문 이미지로 확인**(처음엔
  반대로 추정할 뻔했으나 각 채널 절을 실제로 열어 확인 후 정정).
  **Phase 3(PC 비트, §5.3.1.2)**: 5단 시프트레지스터 기반 PC 비트 값
  생성 알고리즘을 원문 이미지 20여 개로 전부 확인, `n_PC`/`n_PC^wm`
  결정 조건도 원문에서 확인(`18≤K≤25`→`n_PC=3`, `K>30`→`n_PC=0`,
  PBCH/PDCCH는 항상 `n_PC=0`). **미확인으로 남긴 것**: `26≤K≤30`
  구간이 원문(§6.3.1.3.1)에 명시적 분기 없음 — `polar_uci_npc()`가
  `n_PC=0`(K>30과 동일)을 보수적 기본값으로 적용, 주석에 명시. 디코더
  SC도 PC 비트 위치에서 (0 고정이 아니라) 시프트레지스터가 계산한
  기댓값을 강제하도록 확장(인코더와 동일한 상태 전이를 디코더 재귀가
  n=0..N-1 순서로 그대로 재현 — 이 recursion 구조의 기존 성질을 그대로
  활용, 새 알고리즘 아님).
  `polar_init()` 시그니처에 `I_IL`/`n_PC`/`n_PC_wm` 추가, `pbch.c`(2곳)/
  `pdcch.c`(4곳)/`pucch.c`(4곳)/`main.c`(레거시 경로 1곳) 총 11개 호출부
  갱신, UCI용 `polar_uci_npc()` 헬퍼 신규.
  **검증**: standalone 하네스로 PBCH/PDCCH 파라미터(I_IL=1) +
  UCI+PC비트(K=20, n_PC_wm=0/1 둘 다) + UCI 무PC(K=30) 총 5개 구성 각각
  200트라이얼 잡음 없는 라운드트립 전부 정확 일치, ASan/UBSan 클린.
  전체 clean 빌드 경고 0건(기존 5건 무관 경고만 유지),
  `regression_test.sh` **90/90 그대로 통과**. 실제 시뮬레이터로 PBCH/
  PDCCH/PUCCH F3 수동 SNR 스윕 — 셋 다 깨끗한 워터폴 확인.
  **알려진 한계**: 이 프로젝트의 PUCCH 함수 4개 전부 UCI 비트수를
  3~11로 강제 클램프하고 있어, PC 비트 경로(K∈[18,25]에서만 발동)는
  실제 시뮬레이터 어떤 config로도 아직 도달 불가능 — standalone
  하네스로만 검증됨(코드 자체는 회귀에 포함돼 있으나 실사용 경로에서
  exercised 안 됨, 명시).
- [x] 빔 관리 P1 -> P3(UE Rx 빔 정제) -> P2(gNB Tx 빔 재정제) — 2026-09-03
  완료. 사용자 확인된 설계대로 UE를 더 이상 단일/광각 안테나로 취급하지
  않고 4소자 ULA + 자체 DFT 빔 코드북(오버샘플링 x2, 8후보)을 갖는다고
  모델링(`beam_mgmt.h`/`.c` 신규: `beam_mgmt_ue_steer()` — UE 결합
  가중치, `beam_mgmt_true_channel_mimo()` — gNB(32)→UE(4) rank-1 LOS
  MIMO 채널, `beam_mgmt_p3_sweep()` — P1이 고정한 Tx 빔 위에서 UE Rx
  빔 8개 스위핑, `beam_mgmt_p2_effective_channel()` — P3가 고른 Rx
  빔으로 결합한 유효채널을 만들어 기존 `beam_mgmt_p1_sweep()`을 그대로
  재사용해 P2(Tx 재정제) 구현, 별도 함수 불필요). 신규
  `run_pdsch_beam_mgmt_p123_simulation()`(`pdsch.c`) — Genie(이상적
  상한)/No-Rx-Sweep(정제 안 함 기준선)/P3+P2 Refined 세 기준 비교.
  `BEAM_MGMT_RX_SWEEP`(신규 설정, 기본 0) + `config_parser.c` 화이트
  리스트(TDL/HARQ와의 결합은 아직 차단, 위 "진행 중" 참조), `main.c`
  dispatch 배선.
  **구현 중 발견·수정한 버그 2건**: (1) UE 측 "진짜 채널" 위상 생성에
  실수로 결합 가중치용 `1/sqrt(N_UE)` 정규화를 그대로 재사용 —
  물리적으로는 각 UE 소자가 독립적으로 같은 신호 전력을 받아야 하는데
  이 중복 정규화로 배열이득(N_UE배)이 통째로 사라짐(실행 결과 정제
  이득이 정제 안 함 기준선과 거의 같게 나와 발견). 원인 채널 생성과
  결합 가중치 생성을 별개 함수(`ue_array_manifold()` 신규 static,
  크기 정규화 없음)로 분리해 해결. (2) `beam_mgmt_p1_sweep()`의
  `inner_prod32()`가 채널에 켤레(conj)를 취하는 관례인데, P3 스윕과
  `pdsch.c`의 최종 유효채널 계산(heff_refined/heff_noswp) 3곳 모두
  이 켤레를 빠뜨림 — 전부 conj 추가로 수정. **검증**: 첫 실행에서
  실제로 이 버그들이 만든 비정상 결과(정제 후 이득이 SNR 무관 <0.15
  근처에 고정, Genie는 5dB부터 BLER=0인데 Refined는 15dB에도
  BLER≈1)를 확인하고 원인을 끝까지 추적해 수정 — 표면 결과만 보고
  넘어가지 않음. 수정 후: AvgGain_P3P2가 모든 SNR에서 AvgGain_NoSweep
  보다 뚜렷이 높고(-5dB 0.02 vs 0.007, 15dB 2.10 vs 0.99) SNR에 따라
  단조 증가, BLER_Refined도 SNR에 따라 단조 감소(1.0→0.20, 0~15dB) —
  Genie보다는 여전히 나쁜데(Tx+Rx 양쪽 실측잡음+양자화가 겹치고 UE
  코드북이 8후보로 gNB 1024후보보다 훨씬 성겨 당연히 예상되는 격차,
  이 프로젝트의 다른 Genie-vs-실측 비교들과 같은 패턴) 정제 자체의
  효과는 명확히 입증됨. ASan/UBSan 클린(malloc/free 짝 맞추기 실수
  1건을 배선 직후 자체 발견·수정 — 이전 함수 cleanup 코드 유실+신규
  함수 cleanup 중복으로 메모리 누수/double-free 위험이 있었음, ASan으로
  최종 확인). `regression_test.sh`에 positive 1건("PDSCH BEAM_MGMT
  P1-P3-P2")+negative 1건(TDL 결합 차단 확인) 추가, **90/90 통과**,
  clean 빌드 경고 없음(기존 5건 무관 경고만 유지).

- [x] OLLA를 SM_4X4로 확장 — 2026-09-03 완료. 기존
  `run_pdsch_olla_sm2x2_simulation()`의 "레이어 수 무관 동일 MCS 공유 +
  결합 ACK(전 레이어 성공해야 ACK)" 패턴을 그대로 4레이어로 확장,
  4x4 채널/검출은 `run_pdsch_sm4x4_simulation()`의 배열 기반(NL=4)
  구조 재사용. 신규 `run_pdsch_olla_sm4x4_simulation()`
  (`pdsch.c`) — 트라이얼마다 MCS가 바뀔 수 있어 LDPC/`nr_seg_compute`를
  트라이얼 안에서 매번 재초기화(SM_2X2 OLLA와 동일 관례). 새 설계
  결정 없이 두 기존 패턴을 그대로 합성. `config_parser.c`
  `olla_mm_supported`에 SM_4X4 추가, `main.c` dispatch 배선.
  **검증**: clean 빌드 경고 0건, `regression_test.sh`의 기존
  negative test 대상을 SM_4X4→CL_4PORT로 교체(CL_XPORT는 여전히
  미지원이라 negative 커버리지 유지)하고 SM_4X4용 positive test 신규
  추가 — **88/88 통과**. 수동 실행(12dB, MCS Table1): Open-Loop이
  BLER=0.90(목표 0.10)으로 크게 벗어나고 OLLA가 오프셋을 -10.56dB까지
  낮춰 BLER=0.16까지 수렴, 평균 MCS 19→6.16 — 기존 SM_2X2/SIMO_MRC
  OLLA에서 이미 확인된 "Shannon+3dB gap이 이 프로젝트 LDPC 코덱엔
  낙관적" 특성과 동일한 패턴(버그 아님, 4레이어 결합 ACK라 수렴이 더
  어려운 것도 예상대로).
- [x] PUSCH UL SM_2X2에 flat+HARQ 추가 — 2026-09-03 완료. 기존엔
  SM_2X2+TDL+HARQ만 있고 flat+HARQ 조합 함수가 없어 `config_parser.c`가
  명시적으로 CFG_ERR 차단 중이었음(DL도 동일한 공백이 있지만 이번
  요청은 PUSCH/UL 한정이라 DL은 손대지 않음). 신규
  `run_pusch_sm2x2_harq_simulation()` — 기존 `run_pusch_sm2x2_tdl_harq_
  simulation()`의 다중 코드블록 HARQ 구조(코드워드 A/B 각각 seg.C개
  영구 soft-combining 버퍼, attempt마다 두 코드워드 모두 재전송)를
  그대로 쓰고 채널만 TDL 대신 `run_pusch_sm2x2_simulation()`의 평탄
  2x2 MIMO 채널로 교체. **설계 확인**: flat 채널도 attempt마다
  재드로우(기존 `run_pdsch_mumimo_harq_simulation()`의 `is_tdl` 분기가
  flat일 때도 `mumimo_channel_draw()`를 attempt 루프 안에서 매번
  호출하는 선례를 그대로 따름 — "flat"은 RE 전체에 걸쳐 평탄하다는
  뜻이지 재전송 간 고정이라는 뜻이 아니라서, 이렇게 해야 HARQ가 실제
  시간 다이버시티 이득을 가짐). `config_parser.c`의 `pusch_mm_harq_
  supported` 화이트리스트에서 SM_2X2의 TDL 전용 제약 제거, `main.c`
  dispatch에 SM_2X2+HARQ+non-TDL 분기 추가.
  **검증**: clean 빌드 경고 0건, `regression_test.sh`의 기존 negative
  test("PUSCH SM_2X2 + HARQ + flat fading has no dedicated function")를
  positive test("PUSCH SM_2X2 HARQ flat")로 교체, **87/87 통과**(negative
  1건 제거+positive 1건 추가로 총 개수 유지). 수동 실행: C=1(NUM_RB=51,
  MCS10) BLER(1st) 1.0→0.085·BLER(HARQ) 0.635→0.0(0~15dB), AvgTx
  2.83→1.09 — 정상 waterfall. C=2(NUM_RB=261, MCS27, 균등분할)도
  BLER(HARQ) 1.0→0.6→0.02(5~25dB), AvgTx 3.00→1.90 — 물리적으로 타당.
  이 확장은 P0-2c 이후 첫 신규 기능이라 처음부터 `nr_sch` 다중
  코드블록 구조로 작성(레거시 단일-CB 코드 거치지 않음).
- [x] P0-2c 최종 완료: `nr_sch` 다중 코드블록 세그멘테이션을 HARQ 있는
  나머지 14곳(`pdsch.c` 9개: harq/simo_mrc_tdl_harq/sm2x2_tdl_harq/
  sm4x4_harq/cl_4port_harq/cl_8port_harq/cl_32port_harq/mumimo_harq/
  beam_mgmt_harq, `pusch.c` 5개: harq/sm2x2_tdl_harq/ul_eigen_bf_harq/
  ul_eigen_bf_2tx_harq/ul_eigen_bf_4tx_harq)로 확장 — 2026-09-03 완료.
  이걸로 8424비트 TBS 클램프가 있던 `run_pdsch_*`/`run_pusch_*` 50개
  함수(파일럿 1 + 비HARQ 35 + HARQ 14) 전부가 다중 코드블록을 지원.
  **설계**: 사용자와 사전 확인한 방향대로 — 트라이얼당 코드블록 C개를
  각 1회 인코딩(공유 LDPC 파라미터라 인스턴스 1개 재사용), 코드블록별
  영구 soft-combining 버퍼 C개(재전송 attempt 간 유지, 트라이얼
  시작 시에만 초기화), attempt마다 C개 코드블록을 전부 함께
  재전송(이 프로젝트는 CBGTI/부분 재전송 미모델링 — 기존 단일-CB HARQ의
  "TB 단위 ACK/NACK"를 그대로 확장), 코드블록별 CRC24B 검사 + TB
  재조립 후 CRC24A 검사를 모두 통과해야 종료.
  다시 두 fork(`pdsch.c`/`pusch.c` 파일 단위 분리)에 병렬 위임 — 이번엔
  세션 초반 반영한 CLAUDE.md "검증 범위 원칙"(targeted 기본, 전체
  회귀는 마지막에 1회)도 함께 적용.
  **부수 발견**: 이 macOS 환경엔 `flock` CLI가 없음(Linux 전제였던
  지시가 실패) — fork들이 각각 Python `fcntl.flock`/`mkdir` 기반 원자적
  락으로 대체해 빌드 디렉터리 경합을 직접 우회.
  **검증**: 두 fork 완료 후 메인 세션이 clean 전체 재빌드(경고 0건,
  기존 3건 무관 경고만 유지) + `regression_test.sh` **87/87 통과**를
  직접 재확인. `pdsch.c` 9개는 fork 자체가 이미 세션 시작 시점
  바이너리 대비 C=1 bit-for-bit 동일 확인 완료. `pusch.c` 5개는 fork가
  (병렬 pdsch.c fork가 아직 편집 중이라 링크 불가로) 검증을 못 끝내
  메인 세션이 직접 이어받음 — `git stash`로 pdsch.c만 세션 시작 시점
  커밋으로 되돌려 5개 함수 전부 C=1 출력을 캡처한 뒤 복원·재빌드해
  재실행, **5개 전부 BER/BLER/AvgTx bit-for-bit 완전 동일** 확인.
  C>1 물리적 타당성도 메인 세션이 직접 추가 확인: SISO PUSCH HARQ
  (NUM_RB=261·MCS27, C=2) BLER(HARQ) 1.0→0.0·AvgTx 3.00→2.00 정상
  수렴, UL_EIGEN_BF_2TX HARQ(랭크적응+HARQ+다중CB 동시 적용, 가장 복잡한
  pusch.c 케이스, NUM_RB=261·MCS27, C=2) BLER(HARQ)이 전 SNR에서 0.0
  (재전송으로 저SNR도 복구)·AvgTx 2.12→1.00·AvgRank=2.00 일관 — 전부
  물리적으로 타당. `pdsch.c` 쪽은 fork가 SISO HARQ/CL_32PORT HARQ(가장
  복잡한 케이스)로 C>1 검증 이미 완료.
  **남은 것**: K'=B'/C 비정수분할 시 반올림 규칙 미확정(`nr_sch.h`
  `nr_seg_compute()` 문서 주석 참조, 사용자 확인 후 `exit(1)` 유지가
  현재 정책) — 이 50개 함수 전부에 여전히 적용되는 제약. 이 시뮬레이터의
  기본 RB/MCS 조합으로는 TBS가 8448비트를 넘는 경우가 드물어(수동으로
  NUM_RB/MCS를 크게 올려야 C>1 도달) 실사용 경로에서 이 제약을 만날
  일은 현재 거의 없음.
- [x] P0-2c 후속: `nr_sch` 다중 코드블록 세그멘테이션을 HARQ 없는
  나머지 35개 `run_pdsch_*`/`run_pusch_*` 함수로 확장 — 2026-09-03
  완료. 파일럿(`run_pdsch_simulation()`, 아래 항목) 이후 남은 8424비트
  TBS 클램프가 걸린 함수 중 HARQ 없는 것 전부(`pdsch.c` 23개:
  dmrs/simo_mrc(+tdl)/sm2x2(+tdl)/sm4x4(+tdl)/tdl/cl_4port(+tdl)/
  cl_8port(+tdl)/cl_32port(+tdl)/eigen_16port(+tdl/subband)/
  olla(+simo_mrc/sm2x2)/mumimo(+tdl)/beam_mgmt(+tdl), `pusch.c` 12개:
  기본/tdl/tdl_dfe/tdl_turbo/sm2x2(+tdl)/ul_eigen_bf 1・2・4Tx(+tdl)).
  두 fork(subagent)에 파일 단위로 병렬 위임(`pdsch.c` 담당/`pusch.c`
  담당, 파일 겹침 없이 충돌 방지 — 동시 `make`/실행 파일 경합은
  `flock`으로 직렬화 지시). 각 함수: 8424 클램프 제거 →
  `nr_seg_compute()`/`ldpc_init_resolved()` → 코드블록 루프(encode+
  rate-match→concat) → 수신측 코드블록별 combine+decode+CRC24B 검사→
  재조립+CRC24A. `pusch.c`에 `nr_sch.h` include 신규 추가.
  **검증**: 두 fork 완료 후 메인 세션에서 clean 전체 재빌드(경고 0건,
  기존 3건 무관 경고만 유지) + `regression_test.sh` **87/87 통과**
  (기존 커버리지 전무하던 CL_8PORT/CL_32PORT/EIGEN_16PORT에 신규 7개
  항목 추가 포함). 추가로 메인 세션이 직접: `git stash`로 세션 시작
  시점(커밋 `3e65fb1`) 바이너리를 복원해 `run_pusch_simulation`/
  `run_pusch_tdl_turbo_simulation`(가장 단순 vs 가장 복잡한 pusch.c
  케이스) C=1 BER·BLER가 수정 전/후 **bit-for-bit 완전 동일**함을
  재확인, `run_pusch_simulation`을 NUM_RB=261·MCS27(TABLE2)로 B=11622
  (C=2, 균등분할)까지 밀어붙여 30dB(BLER=0.02)→40/50dB(BLER=0.0)로
  정상 수렴함을 확인(pdsch.c 쪽은 CL_4PORT/CL_32PORT로 fork 자체가
  C>1 실행 검증 완료). 비정수분할 B(예: NUM_RB=260)에서 `exit(1)`
  가드도 이 함수들 안에서 정상 발동 확인.
  **남은 것**: HARQ 있는 14곳(위 "진행 중" 섹션 참조) — 코드블록별
  soft-combining 버퍼 재설계가 필요해 이번 그룹과 별개 과제.
- [x] P0-2c: TS 38.212 §5.2.2 다중 코드블록 세그멘테이션 + CRC24B,
  §5.4.2.1 `E_r` 배분, §5.5 concatenation — 2026-09-03 완료(모듈) +
  파일럿 통합 완료.
  신규 `nr_sch.h`/`.c`(`nr_seg_compute()` — C/L/B'/K'/Zc/K/filler_size
  산출, `nr_seg_split()` — 코드블록 분할+CRC24B, `nr_seg_concat()` —
  §5.5), `nr_rate_matching.c`에 `nr_ldpc_er_alloc()`(§5.4.2.1 floor/
  ceil 자기정합 분배) 추가. `crc.h`에 CRC24B(poly `0x800063`, TS
  38.212 §5.1 3gpp-server MCP로 확인) 추가. 기존 `ldpc_nr.c`의
  `nr_select_bg_zc()`(TB 레벨 BG/Kb 선택 + 코드블록 레벨 Zc 탐색이
  섞여있던 단일 함수)를 `nr_select_bg()`+`nr_select_zc()`로 분리(동작
  100% 동일, `ldpc_init_resolved()` 신규로 이미 해석된 파라미터로
  바로 초기화 가능하게 함 — 세그멘테이션된 K'로 BG를 잘못 재판정하는
  것 방지).
  **미해결 스펙 디테일(사용자 확인 후 현재 정책 확정)**: K'=B'/C가
  스펙 원문 이미지에 반올림 기호 없이 그대로 나오는데, B가 C로 안
  나누어떨어지면(예: B=8449) 대수적으로 정수가 안 나옴 — 1차 소스
  (3gpp-server MCP TS 38.212 v18.8.0 §5.2.2 이미지)와 2차 소스
  (WebSearch/WebFetch 교차검증) 모두 반올림 규칙을 확정 못함. 추정으로
  밀어붙이지 않고, `nr_seg_compute()`는 B'가 C로 정확히 나누어떨어지는
  경우만 처리하고 아니면 진단 메시지와 함께 `exit(1)` — 사용자가 이
  정책 유지를 명시적으로 선택.
  **파일럿 통합**: `pdsch.c`의 `run_pdsch_simulation()`(HARQ 없는 가장
  단순한 non-DMRS 함수)을 다중 코드블록 구조로 재작성 — 8424비트 TBS
  클램프 제거, encode/decode를 코드블록 C개 루프로 교체.
  **검증**: `nr_sch` standalone 하네스(scratch, 25개 체크) — 단일 CB가
  레거시 `nr_select_bg_zc()`와 bit-for-bit 일치, C=2 균등분할 케이스의
  segment→encode→CB별 rate matching(서로 다른 `E_r` 포함)→concat→
  combine→decode 전체 라운드트립 성공, 비정수분할 케이스 `exit(1)`
  정상 발동. 파일럿 통합 후: TB_SIZE=3000(C=1) 수정 전/후 BER·BLER
  bit-for-bit 동일 확인, TB_SIZE=8426(C=2) 300트라이얼×5 SNR 포인트
  깨끗한 waterfall(0dB BLER=1.0→8dB BLER=0.0) 확인, TB_SIZE=20000
  (비정수분할)에서 `exit(1)` 진단이 실제 파이프라인에서도 정상 발동.
  `regression_test.sh`에 이 함수 전용 신규 항목 2개(C=1/C=2, 기존엔
  이 함수 커버리지 전무) 추가, 80/80 통과. `make clean && make` 경고
  없음(기존 3건 무관 경고만 유지). 나머지 49개 함수로의 확장은 위
  "진행 중" 섹션 참조.
- [x] TS 38.212 §5.4.2.1 표준 rate matching(P0-3) — 2026-09-02 완료.
  P0-1(NR BG1/BG2 QC-LDPC) 직후 남아있던 마지막 비표준 지점 — LDPC를
  쓰는 51개 `run_pdsch_*`/`run_pusch_*` 함수 중 HARQ 14곳은 균등
  1/4-버퍼 k0(`rate_matching.c`, PUCCH F3 Polar HARQ와 공유)를 그대로
  쓰고 있었고, 나머지 37곳은 rate matching 자체가 없이 mother
  codeword 앞부분을 그냥 truncate하는 방식이었음(P0-1로 mother
  codeword가 2~3배 커지면서 BLER이 MCS 목표율이 아닌 mother-code
  성능을 반영하던 원인). 신규 `nr_rate_matching.c`(`nr_ldpc_k0()` —
  Table 5.4.2.1-2, 3gpp-server MCP로 원문 수식 이미지 직접 확인;
  `nr_ldpc_rate_match_select()`/`_combine()`/`_select_soft()` —
  filler bit 건너뛰는 순환버퍼, PUCCH와 공유하는 기존
  `rate_matching.c`는 무변경) 추가. HARQ 14곳은 k0 공식만 교체, 나머지
  37곳은 TX(`E=num_data*bps`개만 rate-match해 변조)/RX(수신 LLR을
  빈 Ncb 버퍼에 scatter 후 복호) 구조를 새로 추가 — RE 그리드가 없는
  레거시 `run_pdsch_simulation`은 `E=A/cr`로 별도 유도,
  `run_pusch_tdl_turbo_simulation`은 iteration간 extrinsic 재선별을
  위해 `_select_soft()`(double 버전) 신규 추가.
  **검증**: `nr_ldpc_k0()` 손계산 일치, select/combine round-trip +
  filler-skip 정확성 독립 하네스로 확인. 51개 함수는 pdsch.c(24개)/
  pusch.c(12개) 두 fork로 병렬 처리 후 직접 통합 재검증 — 회귀 78/78,
  clean 빌드(기존 5건 무관 경고만 유지), 여러 함수 SNR 스윕에서 BLER
  단조 감소 확인. `run_pusch_tdl_turbo_simulation`은 특히 `PUSCH_
  TURBO_ITERS=1`일 때 BER/BLER이 path1(noDFE)과 bit-for-bit 정확히
  일치하는 기존 불변조건이 rate matching 리팩터링 후에도 그대로
  유지됨을 재확인. 상세는 `docs/analysis/history.md` 참조.
- [x] NR BG1/BG2 QC-LDPC 이식(P0-1) — 2026-09-02 완료.
  `docs/analysis/phy_development_direction_validation.md` P0-1 검증·
  계획 승인(EnterPlanMode) 후 착수. 자체 modulo 기반 surrogate LDPC를
  실제 TS 38.212 BG1(46×68)/BG2(42×52) QC-LDPC로 교체 — `ldpc_init`/
  `ldpc_encode`/`ldpc_decode`/`ldpc_decode_soft` 공개 시그니처 무변경
  (53개 이상 호출부 전부 무수정). 신규 `ldpc_tables.c`(TS 38.212 표
  5.3.2-2/5.3.2-3 shift-coefficient 316/197개×8 lifting-set, 로컬
  스펙 docx에서 zipfile+regex로 프로그램적 추출 — 手전사 없음)/
  `ldpc_nr.c`(BG/`Zc` 선택, 리프팅된 H 구성 — 기존 BP 디코더 무변경
  재사용, Richardson-Urbanke류 구조화 인코딩 — 표에서 실측 확인된
  "core 4×4 + 나머지 대각 항등" 구조를 그대로 이용, GF(2) core 역행렬
  캐싱으로 트라이얼당 재계산 없음). 범위: 단일 코드블록만(세그멘테이션
  은 P0-2c 후속 과제), 표준 rate matching은 P0-3 후속(현재
  `rate_matching.c` 무변경). 부수 수정: HARQ mother-rate 호출부
  14곳이 `ldpc_init`에 `HARQ_MOTHER_RATE` 대신 실제 목표율(`cr`)을
  넘기도록 수정(BG 선택이 실제 목표율 기준이 되도록 — 인코딩 로직
  자체는 무변경), 이제 안 쓰는 `HARQ_MOTHER_RATE` 매크로 제거.
  **검증**: 표 추출 자체를 구조적으로 검증(두 표 모두 중복 없음·
  전체 행/열 커버, "core 4×4 + 대각 항등" 구조가 실제 표에서 성립함을
  확인 — 문헌 암기가 아니라 실측으로 알고리즘 설계). BG/`Zc` 선택
  경계값(A=292/293, A=3824/3825, R=0.25/0.67 경계, 8개 lifting-set
  전부) 전수 검증. 인코더 syndrome 검증(H·coded^T=0) 1145회 트라이얼
  전부 통과. 기존 BP 디코더(무변경)로 noise-free round-trip 100%
  성공 + AWGN sweep에서 깨끗한 waterfall 곡선 확인. 최종 컷오버 후
  회귀 78/78 그대로 통과, `make clean && make` 경고 없음(기존 5건
  무관 경고만 유지). **주의**: 이 컷오버로 기존 ~40개 "직접 호출"
  함수의 `coded_size`가 mother-code 길이(2~3배 증가)가 되어 BLER이
  MCS 목표율이 아닌 mother-code 성능을 반영하게 됨(사용자 승인된
  변화, P0-3 완료 전까지 유지) — 상세는 `docs/analysis/history.md`,
  검증 세부 수치는 `docs/analysis/phy_development_direction_validation.md`
  Section 10 참조.
- [x] LDPC BP decoder edge-message 교정 — 2026-09-02 완료.
  `docs/analysis/phy_development_direction_validation.md`(사용자 제공
  외부 검토) P0-2 검증·승인 후 착수. `ldpc_decode_soft()`의
  variable-node 갱신이 목적 check 자신의 이전 기여분을 빼지 않고 전체
  posterior를 그대로 재사용하는 self-feedback 구조였음 — edge별
  extrinsic(`q_edge[H_nnz] = full_llr[v] - c2v[edge]`)으로 교정,
  syndrome 기반 조기 종료 + 실제 성공/실패 반환값(0/1) 추가,
  `ldpc.h` 독스트링도 정정. **검증**: 손으로 구성한 트리형 5변수/2체크
  그래프(순환 없음)로 반복 1회차·수렴 후 posterior 모두 손계산/
  브루트포스 정확한 marginal과 bit-exact(~1e-7) 일치, 프로젝트 실제
  `ldpc_init`/encode로 K=8/64/400 noise-free round-trip 800회 전부
  무오류·수렴, 호출부 68개 `run_*` 전수조사로 반환값 미참조 확인(기존
  로직 영향 없음), 동일 config BLER 비교로 수정 전/후 일관된(방향성
  있는) 개선 확인(12dB에서 0.900→0.806). 회귀 78/78 유지, `make clean
  && make` 경고 없음. 상세는 `docs/analysis/history.md` 참조.
- [x] UL Eigen-BF를 K=4(최대 4레이어)로 확장 — 2026-09-02 완료.
  2-Tx의 닫힌 형식 2×2 SVD + rank 1/2 적응 설계를 4×4로 일반화(rank는
  1~4에서 적응 선택, 3레이어도 이 적응의 자연스러운 결과로 커버).
  4×4 Hermitian 고유분해는 DL EIGEN_16PORT가 이미 검증한 Cyclic
  Jacobi를 `utils.h`의 `herm4x4_eig()`로 추출해 두 모듈이 공유(로직
  변경 없이 이동), `ul_eigen_bf.c`에 `ul_eigen_svd_genie4()`/
  `ul_eigen_orthogonalize4()` 추가. 랭크 적응은 새 함수 없이 DL
  EIGEN_16PORT의 `eigen_bf_16port_select_rank()`를 그대로 재사용
  (인터페이스가 이미 동일), 검출은 CL_32PORT의 기존 검출기 계열
  (mrc_combine_4rx/mimo_mmse_detect_4rx2/4rx3/4x4)을 재사용(신규 검출
  코드 없음, ZF는 rank<4에 미구현이라 CL_32PORT 선례처럼 MMSE 고정).
  `run_pusch_ul_eigen_bf_4tx_simulation()`/`_tdl_simulation()`/
  `_harq_simulation()`(pusch.c) 3개 신규 함수, `MIMO_MODE=UL_EIGEN_BF_4TX`
  신규(main.c/config_parser.c/pusch.h 배선). **검증**: SVD4/직교화4를
  독립 하네스로 먼저 검증(직교정규성/특이값 일치 오차 전부 ~1e-15
  수준), SNR 스윕에서 AvgRank가 -25dB(1.00)→-5dB(3.11)→0dB 이상(4.00)
  으로 매끄럽게 전이, flat/TDL/HARQ 전부 기존 1/2-Tx 패턴과 일치. 회귀
  4건 추가로 78/78 통과(기존 74개 유지), clean 빌드 경고 없음(기존
  5건 무관 경고만 유지). 상세는 `docs/analysis/history.md` 참조.
- [x] UL Eigen-BF(2-Tx)에 랭크 적응 추가 — 2026-09-01 완료. Genie
  특이값 σ1,σ2 기준 등력분배 추정 용량(C1 vs C2)으로 트라이얼당 1회
  rank 1/2 결정(HARQ는 attempt 0 이전 1회, 이후 고정 — 실제 RI 관례와
  동일), Genie/Eigen-BF 두 경로가 rank 공유(네트워크가 결정하는 값이라
  Genie 기준이 타당, CW개수가 경로마다 갈리는 복잡함 회피). rank=1은
  이미 계산된 2×2 유효채널 열 0만 골라 기존 `mrc_combine()`으로 검출
  (신규 검출 코드 없음 — Genie는 무손실, Eigen-BF는 w_A 단독보다 오히려
  약간 유리). `ul_eigen_svd_genie()`가 특이값도 반환하도록 시그니처
  확장(호출처 3곳 갱신), flat/TDL/HARQ 세 함수 모두에 로직 추가.
  **검증**: SNR 스윕에서 AvgRank가 1.08(저SNR)→2.00(고SNR)으로 매끄럽게
  전이(등력분배 이론과 일치), HARQ에서도 동일 패턴+기존 HARQ 특성
  유지. 새 config 표면 없이 기존 케이스가 랭크 적응 경로를 자연히
  태워 회귀 74/74 그대로 통과, clean 빌드 경고 없음. K>2(3/4 레이어)는
  2026-09-02 완료(위 항목 참조). 상세는 `docs/analysis/history.md` 참조.
- [x] UL Eigen-BF(2-Tx)를 TDL/HARQ로 확장 — 2026-09-01 완료. 1-Tx의
  "공유 클러스터 게인 × 고정 공간 시그니처" 교훈을 그대로 적용 — H(16×2)가
  트라이얼 내내 고정이라 Genie U/Eigen-BF Q도 RE 무관하게 1회만 계산,
  2×2 유효채널은 고정 베이스 행렬을 g(re)로 스케일하는 것으로 단순화.
  `run_pusch_ul_eigen_bf_2tx_tdl_simulation()`/`_harq_simulation()` 신규.
  **검증**: TDL에서 Genie/EigenBF BLER 거의 일치(1-Tx와 동일 패턴),
  HARQ에서 BLER(HARQ)이 BLER(1st) 대비 크게 개선·AvgTx 하락·TDL이
  flat보다 항상 열화 — 전부 타당. 회귀에 positive 3건 추가(기존
  negative 1건은 유효해져 제거), 74/74 통과(기존 72개 유지), clean
  빌드 경고 없음. "적은 순서대로 진행" 목록 첫 항목(UL Eigen-BF
  1/2-Tx TDL/HARQ) 완료. 상세는 `docs/analysis/history.md` 참조.
- [x] UL Eigen-BF(1-Tx)를 TDL/HARQ로 확장 — 2026-09-01 완료.
  **배선 전 검증에서 설계 결함 발견·수정**: 처음엔 다른 massive-MIMO
  TDL 함수처럼 16개 Rx 안테나 각각 독립 TDL을 줬는데, Eigen-BF가
  SNR을 올려도 Genie에 전혀 수렴 못 하는 현상 발견 — standalone
  하네스로 확인해보니 파일럿이 걸친 대역이 coherence bandwidth를
  훨씬 초과해 안테나별 독립 페이딩 시 고정된 공간 방향 자체가
  사라짐이 원인. 빔 관리 TDL과 동일한 "공유 클러스터 게인 × 고정
  공간벡터" 모델로 교체해 해결(5dB에서 BER 0.0160 vs 0.0163으로
  거의 일치하는 정상 결과 회복). `run_pusch_ul_eigen_bf_tdl_simulation()`
  /`run_pusch_ul_eigen_bf_harq_simulation()`(flat/TDL 공용, 빔 추정은
  트라이얼당 1회만) 신규. 회귀에 positive 3건 추가, 72/72 통과(기존
  70개 유지), clean 빌드 경고 없음. 상세는 `docs/analysis/history.md`
  참조.
- [x] UL 2계층(SM_2X2) 수신 빔포밍(Eigen-BF, 비-코드북) — 2026-09-01
  완료. UE 2 Tx로 채널 H(16×2)가 rank-1이 아니게 되면서 진짜 SVD가
  필요해짐 — Genie 경로는 2×2 Gram 행렬(H^H H)의 닫힌 형식(반복법
  불필요) 고유분해로 좌특이벡터 U(16×2, 직교정규) 계산, U가 H의
  신호 부분공간을 정확히 張해 이후 2×2 ZF/MMSE와 결합하면 완전한
  채널지식 기준 최적 선형 수신기와 동치. Eigen-BF 경로는 레이어별
  FDM 파일럿으로 기존 `ul_eigen_beamform()`을 2회 호출해 w_A,w_B를
  얻고 신규 `ul_eigen_orthogonalize2()`(그람-슈미트)로 직교화 —
  직교화해야 결합잡음이 N0·I가 돼 기존 2×2 검출기 가정이 유지됨.
  **배선 전 독립 검증**: U^H U와 I 최대오차 8.5e-15, H 재구성 오차
  최대 2.4e-13, 직교화 후 |w1^H w2|~5.4e-17·‖w2‖²=1.0 — 모두 설계
  의도와 정확히 일치. `run_pusch_ul_eigen_bf_2tx_simulation()`
  (`pusch.c`) 신규 — 기존 `mimo_zf_detect`/`mimo_mmse_detect`를
  그대로 재사용(신규 검출 코드 없음). `MIMO_MODE=UL_EIGEN_BF_2TX`
  신규 분기. **검증**: SNR 스윕에서 두 경로 BLER 모두 단조 감소,
  EigenBF가 저~중SNR에서 Genie보다 살짝 나쁘고 고SNR에서 수렴 — 1-Tx
  버전과 동일한 정성적 패턴. 회귀에 positive 1건+negative 1건 추가,
  70/70 통과(기존 68개 유지), clean 빌드 경고 없음. TDL/HARQ/랭크
  적응/K>2는 위 "진행 중" 섹션 참조. 상세는 `docs/analysis/history.md`
  참조.
- [x] UL SIMO 수신 빔포밍(Eigen-BF, 비-코드북) — 2026-09-01 완료.
  사용자 지적("rx는 코드북 말고 빔포머") 반영 — DL EIGEN_16PORT와
  대칭되는 Rx측 비-코드북 접근, UE 1 Tx→gNB 16 Rx. 순간 채널은
  rank-1이라 고유빔포밍=MRC와 수학적으로 동일하므로, 대신 "M개 잡음
  파일럿 관측의 공간공분산"에서 지배적 고유벡터를 전력반복법으로
  추출(잡음은 16차원에 고르게 퍼지고 신호는 h 방향에만 실려 평균
  공분산이 잡음에 더 강건 — 순간 MRC와 실질적으로 달라지는 지점).
  `ul_eigen_bf.c`/`.h` 신규(전력반복법, 지배적 고유벡터만 필요해 전체
  EVD보다 저렴). **배선 전 독립 검증**: 잡음 없음에서 정확한 MRC
  일치(오차조차 없음), 잡음 상당함(N0=2)에서 M을 1→256으로 늘리면
  정렬도 0.528→0.996으로 단조 개선 확인. `run_pusch_ul_eigen_bf_simulation()`
  (`pusch.c`) 신규 — Genie MRC vs Eigen-BF 병렬 비교(빔관리 함수의
  Genie/P1 비교와 동일 철학), `MIMO_MODE=UL_EIGEN_BF` 신규 분기,
  TDL 조합은 전용 함수 없어 차단. **검증**: SNR 스윕에서 GainLoss(dB)
  가 항상 음수(코시-슈바르츠 상한과 일치)이고 SNR 오를수록 0에 수렴
  (-3.98dB→-0.01dB) — 설계 의도와 정확히 일치. 회귀에 positive 1건+
  negative 1건 추가, 68/68 통과(기존 66개 유지), clean 빌드 경고
  없음. TDL/HARQ, UE 2 Tx 확장은 위 "진행 중" 섹션 참조. 상세는
  `docs/analysis/history.md` 참조.
- [x] PUSCH UL SM_2X2에 HARQ 추가(TDL 전용) — 2026-09-01 완료. DL
  `run_pdsch_sm2x2_tdl_harq_simulation()` 구조를 그대로 PUSCH로 이식
  (mother LDPC+영구 soft-combining 버퍼, 4개 독립 안테나쌍 TDL을
  attempt마다 재드로우, `rate_matching.c` circular buffer 재사용).
  DL도 flat+HARQ가 없는 비대칭이라 UL도 새 통합설계 대신 검증된 DL
  설계를 1:1 포팅(설계 리스크 최소화). `main.c`/`config_parser.c`의
  PUSCH HARQ 화이트리스트에 SM_2X2+TDL 추가. **검증**: 고SNR(25~35dB)
  에서 BLER(HARQ)이 0.55~0.66에서 잘 안 내려가는 현상을 발견했으나,
  DL의 기존(무수정) 함수를 동일 조건으로 재실행해 정확히 같은 수치의
  정체를 확인 — 새 코드의 결함이 아니라 SM_2X2+TDL 검출의 기존 특성을
  정확히 재현한 것. 회귀에 positive 1건 추가, 66/66 통과(기존 65개
  유지), clean 빌드 경고 없음. flat+HARQ/K>2/다중Rx/코드북기반은 위
  "진행 중" 섹션 참조. 상세는 `docs/analysis/history.md` 참조.
- [x] 빔 관리를 TDL/HARQ로 확장 — 2026-09-01 완료. P1/Genie 빔 선택은
  wideband로 유지, TDL은 선택된 빔의 데이터 전송에만 적용(단일
  클러스터 공유 SISO TDL tap-set, 32안테나 공통). **핵심 분석적
  발견**: 이 모델에서는 후보 빔 순위가 TDL 페이딩 값과 무관하게
  wideband 버전과 항상 동일함을 대수적으로 증명(H_true·g(RE) 형태로
  g가 모든 후보에 동일하게 곱해짐) — CL_XPORT의 "wideband PMI 설계 +
  per-RE 전송" 철학과 동일. `run_pdsch_beam_mgmt_tdl_simulation()`/
  `run_pdsch_beam_mgmt_harq_simulation()`(flat/TDL 하나의 함수, 빔
  선택은 트라이얼당 1회만) 신규(`pdsch.c`). **검증 중 확인(버그
  아님)**: TDL이 flat보다 훨씬 크게 열화(15dB에서 BLER 0.13→0.80) —
  빔포밍 배열이득으로 flat 동작점이 이미 매우 낮은 BLER인 상태에서,
  안테나 전체가 공유하는 단일 스칼라 페이딩(다이버시티 전혀 없음)이
  깊은 페이드마다 코드워드 전체를 무너뜨려 상대적 열화폭이 커짐 —
  방향은 플레인 SISO TDL과 같고 크기만 다름, 구조적으로 타당. HARQ도
  초기 저SNR 테스트에서 결함처럼 보였으나 SNR 범위를 넓히자 정상
  waterfall 전이 확인. 회귀에 positive 4건 추가, 65/65 통과(기존
  62개 유지). clean 빌드 경고 없음. UE Rx 빔 스위핑/P2·P3는 위
  "진행 중" 섹션 참조. 상세는 `docs/analysis/history.md` 참조.
- [x] MU-MIMO를 TDL/HARQ로 확장(K=2 고정) — 2026-09-01 완료. ZF-BF
  프리코더가 K=2 폐형 2×2 역행렬이라 RE당 비용이 무시할 만해, 다른
  massive-MIMO 모드와 달리 PRG 서브밴드 근사 없이 TDL에서 매 RE
  정확히 재설계. `run_pdsch_mumimo_tdl_simulation()`(사용자-Tx 안테나
  쌍마다 8개 독립 TDL tap-set, genie-aided라 심볼 인덱스를 RE로
  취급 — `pbch.c` 페이딩 함수와 동일 단순화)/`run_pdsch_mumimo_harq_simulation()`
  (flat/TDL 하나의 함수로 지원, `rate_matching.c` 범용 circular
  buffer 재사용, 두 사용자 CRC 모두 통과해야 종료) 신규(`pdsch.c`).
  `main.c` MIMO_MODE=MU_MIMO 분기를 TDL/HARQ로 세분화,
  `config_parser.c` HARQ 화이트리스트에 MU_MIMO 추가(기존엔 차단
  대상). **검증**: 수동 SNR 스윕에서 TDL이 flat 대비 항상 열화(기존
  다른 TDL 조합과 동일 패턴), HARQ는 BLER(HARQ)이 BLER(1st) 대비
  크게 개선·AvgTx 고SNR 하락(기존 HARQ 함수들과 동일 패턴) 확인.
  회귀에 positive 4건 추가, 이제 유효해진 기존 negative 1건(MU_MIMO+
  HARQ 미지원) 제거, 62/62 통과. clean 빌드 경고 없음. K>2/다중 Rx
  확장은 위 "진행 중" 섹션 참조. 상세는 `docs/analysis/history.md`
  참조.
- [x] OLLA를 SIMO_MRC/SM_2X2로 확장 — 2026-09-01 완료. 기존 SISO
  구조(고정 SNR 시계열, Open-Loop vs OLLA 2-pass, DMRS 미모델링·
  genie-aided) 유지하되 물리 채널만 2-branch MRC / 2x2 공간다중화로
  교체. MCS 예측식은 SISO와 동일하게 유지(다이버시티 이득/MIMO 검출
  손실을 예측식에 반영 안 함 — OLLA가 물리계층 세부사항 몰라도
  ACK/NACK만으로 수렴함을 보이려는 의도적 단순화). `run_pdsch_olla_simo_mrc_simulation()`/
  `run_pdsch_olla_sm2x2_simulation()`(`pdsch.c`) 신규, `main.c`의
  `OLLA_ENABLE=1` 분기를 MIMO_MODE로 세분화. **같은 오배선 클래스
  재도입 방지**: `config_parser.c`에 `OLLA_ENABLE=1`+미지원 MIMO_MODE
  조합 CFG_ERR 차단 추가. **검증 중 발견(버그 아님)**: SIMO_MRC를
  4dB에서 실행하니 OLLA가 offset을 -12dB까지 낮춰도 결합 BLER이
  0.15~0.16 하한에서 안 내려가는 현상 발견 — standalone 하네스로
  확인한 결과 이론상 요구 SNR 대비 실제 부족 확률은 0.7%뿐인데 측정
  BLER은 14.27%(약 20배) — 2026-09-01 OLLA 최초 구현 때 이미 발견한
  "Shannon+3dB gap이 이 프로젝트 LDPC 코덱엔 너무 낙관적" 특성이
  블록-플랫 페이딩(SISO OLLA엔 없던 효과)과 결합해 만들어낸 진짜 MCS
  테이블 하한(floor) — 더 높은 SNR(SIMO_MRC 15dB, SM_2X2 20dB)에서는
  정상 수렴 경향 확인, 메커니즘 자체는 정상 동작. 회귀에 positive 3건
  +negative 1건 추가, 60/60 통과(기존 56개 유지), clean 빌드 경고
  없음. SM_4X4/CL_XPORT 확장은 위 "진행 중" 섹션 참조. 상세는
  `docs/analysis/history.md` 참조.
- [x] PUSCH UL SU-MIMO(SM_2X2) 추가 — 2026-09-01 완료. 완성도 점검
  두 번째 항목(PUSCH에 MIMO 전무, DL과의 구조적 비대칭) 해소. `mimo.c`의
  RE 단위 순수 함수(`mimo_zf_detect`/`mimo_mmse_detect`/
  `mimo_channel_draw_2x2`)를 방향 무관하게 재사용, DL `run_pdsch_sm2x2_*`
  구조를 PUSCH 파이프라인에 이식. `run_pusch_sm2x2_simulation()`(평탄)/
  `_tdl_simulation()`(TDL, 4개 독립 안테나쌍) 신규, `main.c`에
  `MIMO_MODE=SM_2X2` 분기 추가. **스펙 제약 반영**: TS 38.211 §6.3.1.4가
  Transform Precoding을 1개 레이어 초과 전송에 금지 — SM_2X2는 CP-OFDM
  전용으로 설계, `config_parser.c`가 `TRANSFORM_PRECODING=1`과 함께
  쓰면 CFG_ERR로 차단. **같은 오배선 클래스 재도입 방지**: PUSCH
  `HARQ_ENABLE=1`이 여전히 MIMO_MODE 무시하고 SISO로 가는 상태이므로,
  `MIMO_MODE=SM_2X2`+`HARQ_ENABLE=1` 조합도 함께 CFG_ERR로 미리 차단
  (전용 함수 없음). **검증**: regression에 positive 2건+negative 2건
  추가, 56/56 통과(기존 52개 유지), clean 빌드 경고 없음. 수동 SNR
  스윕(-5~15dB, MCS10): flat/TDL 둘 다 BLER 단조 감소, TDL이 flat 대비
  항상 열화(기존 DL SM_2X2와 동일 패턴) — 물리적으로 타당. K>2/다중 Rx/
  코드북 기반 UL/TDL+HARQ는 위 "진행 중" 섹션에 후속 과제로 등록.
  상세는 `docs/analysis/history.md` 참조.
- [x] STRUCTURE.md 전면 갱신 — 문서 드리프트 해소 — 2026-09-01 완료.
  `codebook.c`/`codebook_8port.c`/`codebook_32port.c`/`eigen_16port.c`/
  `olla.c`/`mumimo.c`/`beam_mgmt.c`/`ul_power_ctrl.c` 8개 파일을 모듈
  테이블에 추가, PDSCH dispatch 트리(`CL_4PORT`에서 멈춰있던 것)를
  `main.c`의 실제 if/else if 순서 그대로 재작성, `ULPC`/`BER`/`NONE`
  최상위 분기 추가. 스크립트로 `run_*` 함수 전체와 문서 언급을 diff —
  양방향 완전 일치 확인. 부수 발견(수정 안 함, 아래 "진행 중" 등록):
  `PHY/src/ber_sim.c`가 `c_Makefile`에 미포함된 고아 파일. 문서 전용
  변경, 재빌드/회귀 불필요.
- [x] LLS 완성도 점검 — HARQ dispatch 오배선(silent wrong-dispatch)
  발견·수정 — 2026-09-01 완료. 사용자 요청으로 신규 기능 대신 기존
  코드베이스 감사(`main.c` dispatch를 실제 존재하는 함수와 대조).
  **발견**: PDSCH `HARQ_ENABLE=1` 분기가 SM_4X4/CL_4PORT/CL_8PORT/
  CL_32PORT/SM_2X2+TDL/SIMO_MRC+TDL만 전용 함수로 처리, 그 외 모든
  MIMO_MODE(MU_MIMO/BEAM_MGMT/EIGEN_16PORT/SM_2X2+비TDL/SIMO_MRC+
  비TDL)는 MIMO_MODE를 완전히 무시하는 순수 SISO `run_pdsch_harq_simulation()`
  으로 조용히 떨어짐 — 에러 없이 "MIMO Mode: MU_MIMO" 헤더를 찍으며
  실제로는 SISO 결과를 냄(단순 공백이 아니라 잘못된 결과를 조용히
  내는 정확성 결함, 기존 회귀 테스트는 이 조합들을 안 돌려서 못 잡음).
  **수정**: `config_parser.c`에 main.c dispatch를 그대로 미러링한
  화이트리스트 검증 추가, 미지원 조합은 명시적 CFG_ERR로 종료(기존
  검증 패턴 재사용). **검증**: 미지원 5개 조합 전부 명확한 에러+
  exit code 1 확인, 기존 정상 조합은 그대로 통과 확인. 회귀에 negative
  test 2건 추가, 52/52 통과(기존 50개 유지), clean 빌드 경고 없음.
  같은 감사에서 발견된 낮은 우선순위 항목(PUSCH UL MIMO 전무,
  STRUCTURE.md 문서 드리프트)은 위 "진행 중" 섹션에 등록. 상세는
  `docs/analysis/history.md` 참조.
- [x] 빔 관리(Beam Management, SSB/CSI-RS 기반 P1 절차) — 2026-09-01
  완료. MU-MIMO에 이은 "다음 단계 리스트" 세 번째 항목 — CSI-RS
  채널추정은 이미 있었지만 "빔 스위핑 절차"(후보 빔 순차 송신→RSRP
  측정→선택) 자체는 없던 축. `beam_mgmt.c`/`.h` 신규 — 기존
  CL_32PORT rank-1 코드북(1024개 후보, i1_1×i1_2×i2 전체 격자)을
  후보 빔 집합으로 재사용, 참 채널은 코드북 v_{l,m} 공식을 연속값
  방향으로 일반화한 LOS steering vector. `beam_mgmt_p1_sweep()`
  (빔마다 `BEAM_MGMT_NUM_REP`회 반복 RSRP 평균으로 선택, 측정잡음
  포함) vs `beam_mgmt_genie_best()`(잡음 없는 전수탐색, 양자화
  손실만) 두 기준선을 분리. **배선 전 발견·수정한 설계 결함**: 처음엔
  후보를 256개(i2 편파위상 고정=0)로 뒀는데, standalone 하네스로
  "격자 위 참 채널은 genie에서 gain=1.0 정확 일치"를 검증하다 n_true
  홀수일 때 최대 이득이 0.5로 떨어지는 결함 발견 — 후보를 1024개
  (i2 포함) 전체로 확장해 해결. `run_pdsch_beam_mgmt_simulation()`
  (`pdsch.c`) 신규 — 매 트라이얼 임의 연속 방향의 참 채널에서
  Genie/P1이 선택한 빔을 각각 실제 프리코더로 써서 rank-1 SISO 등가
  채널로 독립 CW 2개 전송·복호 비교. `MIMO_MODE=BEAM_MGMT`로
  `main.c` 신규 분기. **검증**: standalone 하네스로 격자 위
  200/200 정확 일치, off-grid 최악 양자화손실 0.78배, 초저잡음에서
  P1==Genie 99.7%, 저SNR에서 평균 P1 gain(0.054) ≪ 평균 genie
  gain(0.924) 확인. `regression_test.sh`에 "PDSCH BEAM_MGMT flat"
  추가, 50/50 통과(기존 49개 유지), clean 빌드 경고 없음. 수동 SNR
  스윕(-10~15dB, MCS10): P1==Genie 매치율 0.3%→45.6% 단조 증가,
  두 BLER 모두 단조 감소, P1이 전 SNR에서 Genie보다 항상 나쁨(측정
  잡음 손실 일관 관찰) — 물리적으로 타당. UE Rx 빔 스위핑/TDL/HARQ/
  P2·P3는 위 "진행 중" 섹션에 후속 과제로 등록. 상세는
  `docs/analysis/history.md` 참조.
- [x] MU-MIMO (Zero-Forcing Beamforming) — 2026-09-01 완료. 새 영역
  착수(OLLA에 이은 두 번째 항목) — 기존 SU-MIMO(SM_2X2/SM_4X4/
  CL_XPORT)와 근본적으로 다른 축(서로 다른 사용자를 동시에 공간
  분리 서비스). 1차 스코프: gNB Nt=4, K=2 사용자(각 1 Rx, MU-MISO),
  평탄 페이딩 전용, 두 사용자 동일 고정 MCS. `mumimo.c`/`.h` 신규 —
  `mumimo_channel_draw()`(i.i.d. Rayleigh 2×4), `mumimo_zf_precode()`
  (우측 유사역행렬 H^+=H^H(HH^H)^-1, 2×2 Gramian 폐형 역행렬, 열별
  ‖W[:,k]‖²=1/K 정규화 — 실수 양수 스케일이라 간섭제거 성질 보존).
  **배선 전 독립 검증**: 50,000trial Monte Carlo standalone 하네스로
  사용자 간 간섭 최대 2.110e-15(정확한 널링), 유효 채널 항상 실수/
  음수 없음, 평균 총 전력=정확히 1.0(정규화 정상) 확인 후 실제
  시뮬레이션 함수 작성. `run_pdsch_mumimo_simulation()`(`pdsch.c`)
  신규 — ZF 간섭제거로 MIMO 검출기 불필요(순수 스칼라 채널로 환원),
  두 사용자 서로 다른 독립 CW/LDPC 코드워드, 사용자별 BER/BLER +
  "둘 중 하나라도 실패" Combined BLER 3열 출력. `MIMO_MODE=MU_MIMO`로
  `main.c` 신규 분기. **검증**: `regression_test.sh`에 "PDSCH MU_MIMO
  flat" 케이스 추가, 49/49 통과(기존 48개 유지), clean 빌드 경고
  없음(기존 무관 경고만 유지). 수동 SNR 스윕(-5~20dB, MCS10 16QAM,
  2000trial/pt): 두 사용자 BLER 거의 대칭(i.i.d. 통계상 예상대로)·
  SNR 증가에 단조 감소, Combined BLER이 항상 개별 사용자 BLER 이상
  (합집합 상한과 일치) — 물리적으로 타당. 상세는
  `docs/analysis/history.md` 참조.
- [x] OLLA (Outer Loop Link Adaptation) — 2026-09-01 완료. 새 영역
  착수 — 이번 세션 내내 반복 관찰된 "고정 MCS라 rank/채널 품질과
  무관하게 BLER이 갈리는" 현상의 근본 원인(폐루프 링크적응 없음)을
  해결하는 첫 걸음. `olla.c`/`.h` 신규 — `OLLAState`(offset_db 누적,
  ACK 시 +step_up/NACK 시 -step_down, step_up=step_down·target/(1-target)
  로 목표 BLER 수렴 조건 자동 계산), `olla_select_mcs()`(Shannon 용량+
  구현마진(gap_db, 구현 정의 — Tse & Viswanath SNR gap 개념 근거)로
  effective_snr_db 이하 최고 인덱스 MCS 선택). `run_pdsch_olla_simulation()`
  (`pdsch.c`) 신규 — SISO/AWGN 고정 SNR 시계열, Open-Loop(오프셋 항상 0)
  vs OLLA(폐루프 적응) 2-pass 비교. 신규 설정 `OLLA_ENABLE`(기본 0,
  배선 시 다른 dispatch보다 우선)/`OLLA_BLER_TARGET`(기본 0.1)/
  `OLLA_STEP_DOWN_DB`(기본 0.5)/`OLLA_SNR_GAP_DB`(기본 3.0).
  **구현 중 발견·수정한 버그**: `olla_select_mcs`가 "MCS 인덱스 오름차순
  =스펙트럼효율 오름차순"을 가정해 첫 미달 지점에서 조기 종료했는데,
  MCS 표를 직접 덤프해 검증하다 TS 38.214 Table 5.1.3.1-1이 변조차수
  전환 경계(MCS16→17, 16QAM→64QAM)에서 스펙트럼 효율이 아주 미세하게
  감소하는 지점이 있음을 발견(실제 스펙 수치, 표 구현 버그 아님) —
  배포 전 전수 스캔으로 수정(29개뿐이라 성능 영향 없음).
  **검증**: 두 SNR(4dB, 10dB)에서 실행 — 둘 다 Open-Loop이 초기 MCS를
  과도하게 낙관적으로 골라 BLER=1.0(100% 실패)로 완전히 빗나감을 확인
  (Shannon+3dB gap 근사가 이 프로젝트의 실제 LDPC 코덱·짧은 블록길이
  조합에는 너무 낙관적이라는 흥미로운 부수 발견 — 버그 아니라 실제
  코덱 성능 특성, gap_db 기본값을 더 크게 조정하는 건 후속 튜닝
  과제로 남김). OLLA는 두 경우 모두 수백 트라이얼 안에 오프셋을
  큰 폭으로(-5~-7dB) 낮춰 MCS를 재조정하고 최종 BLER을 0.12~0.13으로
  목표(0.10)에 근접 수렴시킴 — 실제 LDPC+QAM 체인으로 폐루프 보정의
  가치를 직접 실증. 회귀 48/48 유지, clean 빌드 경고 없음(기존 무관
  경고만 유지). 상세는 `docs/analysis/history.md` 참조.
- [x] PRG-평균 타깃 전용 MMSE(Wiener) 추정기 — 2026-09-01 완료. subband
  확장(2026-08-31) 때 발견한 문제(기존 MMSE는 "파일럿 지점별 개별 MSE
  최소화"가 목표라 subband/wideband 평균 타깃에는 최적이 아님, DFT보다
  NMSE가 나쁠 수 있었음)를 근본적으로 해결 — LMMSE를 타깃(Y=평균값) 자체에
  대해 재유도. `mmse_build_avg_filter()`(`channel_estimation.c`/`.h`)
  신규 — `target_pos`/`num_target`을 받아 길이 M 가중치 행벡터를 반환,
  이후 내적 한 번으로 스칼라 추정치. **검증(구현 전 이론 예측)**: PRG
  단위(M=24) NMSE 재측정 결과 새 필터가 모든 SNR에서 DFT를 확실히 이김
  (20dB: MMSE(신규)=1.11e-3 vs DFT=1.75e-3 vs MMSE(기존)=3.53e-3, LMMSE
  이론의 "최소 MSE 선형 추정" 보장이 정확히 실측 확인됨). 6곳 배선
  (CL_4/8/32PORT TDL, EIGEN_16PORT wideband TDL, subband의 wideband+
  PRG별 MMSE) 전부 `mmse_build_filter`+평균 → `mmse_build_avg_filter`+
  내적으로 교체, 불필요해진 `h_est_pilot` 버퍼 6곳 모두 제거. subband를
  500trial로 재실행한 최종 BLER은 NONE/LS/MMSE/DFT 여전히 거의 구분 안 됨
  — 2026-08-31에 예측한 "이 동작점은 채널추정 정확도가 아니라 다른 요인이
  성능을 제한한다"가 재확인됨(채널추정 자체의 이론적 정확성과 최종 BLER
  개선은 별개 문제였고 둘 다 예측대로 나옴). 회귀 48/48 유지, clean 빌드
  경고 없음. 상세는 `docs/analysis/history.md` 참조.

- [x] CL_4/8/32PORT TDL에 채널추정(LS/MMSE/DFT) 연결 — 2026-09-01 완료.
  기존에는 LS/MMSE/DFT 채널추정 체인이 EIGEN_16PORT에만 배선돼 있고 코드북
  기반 빔포머 3종은 여전히 genie-aided였음(사용자 지적으로 확인) — 신규
  공용 설정 `CHAN_EST_METHOD=NONE(기본값)/LS/MMSE/DFT` 하나로
  `run_pdsch_cl_4port_tdl_simulation`/`_cl_8port_tdl_simulation`/
  `_cl_32port_tdl_simulation` 세 함수 모두의 wideband RI+PMI 설계용 H_avg를
  제어. NONE 기본값은 기존 genie 코드 경로와 완전히 동일해 하위 호환 100%
  보존. EIGEN_16PORT TDL 때 만든 패턴을 재사용하되, CL_4/8/32PORT는 Tx
  공간상관이 있어(t축을 가로질러 섞음) 파일럿마다 전체 4×T 채널행렬을 먼저
  만들고 상관을 그 위치에서 한 번에 적용한 뒤 노이즈를 더하는 순서로
  재설계(EIGEN_16PORT는 Tx 상관이 없어 (r,t) 독립 루프로 충분했던 것과 차이).
  DFT/MMSE 사전계산(w_dft, mmse_build_filter)은 Tx 상관과 무관한 순수
  주파수영역 선형연산이라 그대로 재사용. 회귀 48/48 유지(NONE이 기존 코드
  경로와 동일해 결과 자체가 무변경 보장), clean 빌드 경고 없음. 세 모드
  모두 NONE/MMSE로 수동 검증 — 크래시 없이 물리적으로 타당, NONE-MMSE 차이는
  wideband 평균화로 인해 EIGEN_16PORT 때와 동일하게 잡음 수준(2026-08-31
  발견, 버그 아님). 상세는 `docs/analysis/history.md` 참조.
- [x] EIGEN_16PORT Subband(PRG) 프리코딩 확장 — 2026-08-31 완료.
  `run_pdsch_eigen_16port_subband_simulation()` 신규(기존 wideband 함수는
  그대로 보존, 회귀 위험 최소화), `EIGEN16_PRECODER_GRAN=WIDEBAND|SUBBAND`
  (기본 WIDEBAND) 신규 설정으로 main.c에서 분기. RI(rank)는 여전히
  wideband 1회 결정(3GPP RI 보고와 정합), 프리코더 방향만 PRG(4RB 단위,
  20RB 기준 5개)별로 그 PRG 파일럿으로 재계산. 구현 중 `Wg_store` 고정크기
  배열(`[64][16][4]`)이 num_prg(NR 최대 273RB 기준 최대 69)를 초과할 수
  있는 버퍼 오버플로우 결함을 배포 전 자체 발견·malloc 동적할당으로 즉시
  수정.
  **핵심 검증 결과**: (1) subband 아키텍처 자체는 확실히 유효 — 동일
  genie(NONE) 조건에서 20dB BLER이 wideband 0.76→subband 0.40으로 거의
  2배 개선, PRG별 프리코더가 wideband 평균보다 훨씬 낫다는 것을 실측
  확인. (2) 그런데 LS/MMSE/DFT 간 최종 BLER 차이는 subband에서도 거의 안
  보임(500trial, 15/20dB) — 원인이 wideband 때와는 다름: PRG 단위(M=24)
  NMSE를 별도 측정해보니 DFT가 LS/MMSE보다 확실히 좋은데(1.4배), 이 채널
  추정 품질 차이 자체가 rank4 100%·고정 MCS인 이 동작점에서는 최종 BLER에
  거의 전달되지 않음(다른 오차 요인이 지배적). (3) DFT가 MMSE보다 나은
  구조적 이유 규명: "PRG 평균"은 수학적으로 시간영역 DC 성분(tap 0)과
  같은데, 지금 구현한 MMSE는 파일럿 지점별 개별 MSE를 최적화하도록
  설계되어 있어 그 출력의 평균이 "평균값 자체의 MSE" 최적은 아님(버그
  아니라 추정기 설계 목표와 사용 목적의 불일치 — 표면 결과만 보고 결론
  내리지 않고 수식으로 원인을 끝까지 추적함). 회귀 48/48 유지, clean
  빌드 경고 없음. 상세는 `docs/analysis/history.md` 참조.
- [x] EIGEN_16PORT TDL의 DFT 채널추정 경로 성능 최적화 — 2026-08-31 완료.
  `interpolate_channel→IDFT→절단→DFT→data RE 평균` 전체 파이프라인이
  파일럿 벡터에 대해 **선형연산**이라는 점을 이용(dft_num_taps가 SNR/
  트라이얼/안테나쌍과 무관하게 고정이라 이 선형함수 자체도 고정) —
  시뮬레이션 시작 시 표준기저벡터 num_pilots개를 파이프라인에 흘려
  "임펄스 응답"을 측정하는 방식(중첩의 원리)으로 고정 가중치 벡터
  w_dft를 1회만 계산, 이후 트라이얼×64안테나쌍마다는 O(num_active²)
  전체 파이프라인 대신 O(num_pilots) 내적(`H_est_avg[r][t]=w_dft·h_ls_pilot`)
  한 번만 수행. FFT 재작성(dft_precode.c는 PUSCH DFT-s-OFDM에도 쓰이는
  이미 검증된 공용 코드라 건드리지 않는 쪽을 택함, 3GPP가 M=2,3,5 곱만
  요구해 임의 M 지원을 위해 의도적으로 direct DFT를 쓴다는 기존 설계
  주석과도 정합) 대신 이 방향을 택함 — 더 크고 안전한 최적화.
  **검증**: 20개 무작위 파일럿 벡터로 사전계산 경로(fast)와 기존
  brute-force 경로가 오차 ~2e-16(부동소수점 잡음 수준)으로 완전히 일치함을
  별도 하네스로 확인(동작 불변 보장). 실제 시뮬레이션 재실행 결과 BER/BLER
  전 행이 최적화 전과 완전히 동일(같은 난수 시퀀스, 선형변환만 다른 경로로
  계산했으므로 당연한 결과)하면서 런타임은 28.2초→1.56초로 **약 18배
  단축**(100trial×7SNR 기준) — 이제 MMSE/LS와 비슷한 속도. 회귀 48/48 유지,
  clean 빌드 경고 없음.

- [x] EIGEN_16PORT TDL의 DFT 채널추정 경로 성능 최적화 — 2026-08-31 완료.
  `interpolate_channel→IDFT→절단→DFT→data RE 평균` 전체 파이프라인이
  파일럿 벡터에 대해 **선형연산**이라는 점을 이용(dft_num_taps가 SNR/
  트라이얼/안테나쌍과 무관하게 고정이라 이 선형함수 자체도 고정) —
  시뮬레이션 시작 시 표준기저벡터 num_pilots개를 파이프라인에 흘려
  "임펄스 응답"을 측정하는 방식(중첩의 원리)으로 고정 가중치 벡터
  w_dft를 1회만 계산, 이후 트라이얼×64안테나쌍마다는 O(num_active²)
  전체 파이프라인 대신 O(num_pilots) 내적(`H_est_avg[r][t]=w_dft·h_ls_pilot`)
  한 번만 수행. FFT 재작성(dft_precode.c는 PUSCH DFT-s-OFDM에도 쓰이는
  이미 검증된 공용 코드라 건드리지 않는 쪽을 택함, 3GPP가 M=2,3,5 곱만
  요구해 임의 M 지원을 위해 의도적으로 direct DFT를 쓴다는 기존 설계
  주석과도 정합) 대신 이 방향을 택함 — 더 크고 안전한 최적화.
  **검증**: 20개 무작위 파일럿 벡터로 사전계산 경로(fast)와 기존
  brute-force 경로가 오차 ~2e-16(부동소수점 잡음 수준)으로 완전히 일치함을
  별도 하네스로 확인(동작 불변 보장). 실제 시뮬레이션 재실행 결과 BER/BLER
  전 행이 최적화 전과 완전히 동일(같은 난수 시퀀스, 선형변환만 다른 경로로
  계산했으므로 당연한 결과)하면서 런타임은 28.2초→1.56초로 **약 18배
  단축**(100trial×7SNR 기준) — 이제 MMSE/LS와 비슷한 속도. 회귀 48/48 유지,
  clean 빌드 경고 없음.
- [x] EIGEN_16PORT에 imperfect CSI 배선 — TDL + LS/MMSE/DFT 채널추정 —
  2026-08-31 완료. `run_pdsch_eigen_16port_tdl_simulation()` 신규(TDL +
  imperfect CSI), `main.c`에 `MIMO_MODE=EIGEN_16PORT`+`CHANNEL_MODEL=TDL`
  라우팅, 신규 설정 `EIGEN16_CHAN_EST=NONE|LS|MMSE|DFT`(기본 MMSE)로 4가지
  직접 비교 가능. UE 4안테나→gNB 16안테나 UL SRS reciprocity를 모사(파일럿
  120개, 4×16=64 안테나쌍마다 노이즈 관측 → 선택 방식으로 처리 → wideband
  평균 H_est로 프리코더 설계), 실제 하향링크는 RE별 진짜 주파수선택적
  H_true로 통과 — 불일치가 잔여 스트림간 간섭으로 자연스럽게 나타남.
  `mmse_channel_estimate()`를 `mmse_build_filter()`(SNR당 1회 O(M³))+
  `mmse_apply_filter()`(이후 O(M²) 재사용)로 분리하는 성능 리팩터링 선행(기존
  함수는 두 개를 순서 호출하는 wrapper로 유지, 동작 불변 확인) — 64안테나쌍×
  매 트라이얼마다 M×M 역행렬을 다시 푸는 것을 피함.
  **검증 및 중요한 발견**: genie(NONE)이 예상대로 항상 최고 BLER(상한 성질
  유지 확인)이지만, LS/MMSE/DFT 세 방식 간 차이가 예상보다 훨씬 작게 나타남 —
  표면 결과만 보고 결론 내리지 않고 별도 하네스로 wideband 평균 채널 자체의
  NMSE를 직접 측정해 원인 확인(`tasks/lessons.md` 2026-08-27 교훈 적용): 120개
  파일럿의 단순 평균 자체가 이미 노이즈 대부분을 없애버려 MMSE의 "주파수상관
  활용" 이득이 wideband 설계에서 거의 사라짐 — 버그 아니라 설계 선택(wideband
  프리코딩)의 자연스러운 결과, 위 "진행 중" 항목으로 후속 과제 등록. 회귀
  48/48 유지, clean 빌드 경고 없음. 상세는 `docs/analysis/history.md` 참조.
- [x] 채널추정 확장 — MMSE(Wiener)/DFT 기반, `channel_estimation.c`/`.h`
  범용 함수로 신규 추가 — 2026-08-31 완료. EIGEN_16PORT 빔포머에 imperfect CSI를
  배선하기 전에 LS 하나만으로는 부족하다는 지적(사용자)에 따라 먼저 구현.
  `mmse_channel_estimate()`(파일럿 도메인 LMMSE, 지수 PDP 주파수상관 모델,
  임의크기 M×M 복소역행렬 `inv_dynamic` 신규), `dft_channel_estimate()`(시간영역
  잡음절단, 기존 `dft_precode.h` 유니터리 DFT/IDFT 재사용). TDL 멀티패스 채널
  300trial×6 SNR로 검증: MMSE가 전 SNR에서 LS/DFT보다 항상 낮은 NMSE(이론과 일치),
  DFT는 tap 수에 따른 편향-분산 트레이드오프가 교과서대로 재현됨(tap 너무 적으면
  고SNR에서 LS보다 나빠지는 오차 바닥 발생). 회귀 48/48 유지, clean 빌드 경고 없음,
  기존 LS/interpolate_channel 시그니처 불변이라 기존 사용처 전부 영향 없음.
  상세는 `docs/analysis/history.md` 참조.

- [x] 채널추정 확장 — MMSE(Wiener)/DFT 기반, `channel_estimation.c`/`.h`
  범용 함수로 신규 추가 — 2026-08-31 완료. EIGEN_16PORT 빔포머에 imperfect CSI를
  배선하기 전에 LS 하나만으로는 부족하다는 지적(사용자)에 따라 먼저 구현.
  `mmse_channel_estimate()`(파일럿 도메인 LMMSE, 지수 PDP 주파수상관 모델,
  임의크기 M×M 복소역행렬 `inv_dynamic` 신규), `dft_channel_estimate()`(시간영역
  잡음절단, 기존 `dft_precode.h` 유니터리 DFT/IDFT 재사용). TDL 멀티패스 채널
  300trial×6 SNR로 검증: MMSE가 전 SNR에서 LS/DFT보다 항상 낮은 NMSE(이론과 일치),
  DFT는 tap 수에 따른 편향-분산 트레이드오프가 교과서대로 재현됨(tap 너무 적으면
  고SNR에서 LS보다 나빠지는 오차 바닥 발생). 회귀 48/48 유지, clean 빌드 경고 없음,
  기존 LS/interpolate_channel 시그니처 불변이라 기존 사용처 전부 영향 없음.
  아직 어떤 시뮬레이션에도 배선 안 된 범용 빌드블록 상태 — 위 "진행 중" 항목 참조.
  상세는 `docs/analysis/history.md` 참조.

- [x] Massive MIMO — Type I SP 32-port 코드북(N1=4,N2=4,O1=4,O2=4,P=32), rank 1~4 —
  2026-08-31 완료. **스펙 정합 확인**: TS 38.214 Rel-18 Type I SP는 64 CSI-RS 포트를
  정의하지 않음(§5.2.2.2.1 서두가 4/8/12/16/24/**32**만 나열, TS 38.211
  Table 7.4.1.5.3-1도 최대 X=32) — 사용자에게 이 사실을 먼저 보고하고, "64안테나 소자
  = 32포트 × 2편파"로 스코프를 재정의(사용자 확인 후 진행). 32포트 중 유일한 정사각
  2D 배열인 (N1,N2)=(4,4), (O1,O2)=(4,4) 채택(다른 옵션은 (8,2), (16,1)).
  RI 범위는 rank 1~4로 사용자 확인(rank 1~2/1~4/1~8 중 선택) — rank 3/4는 P≥16 분기라
  rank 1/2와 다른 구조(k1/k2 없이 θ_p 코사인, 절반 길이 빔벡터 ṽ, i1_1 범위도 0~7로
  절반).
  - **구현**: `codebook_32port.c`/`.h` 신규 — 2D DFT 빔벡터 v_{l,m}(u_m 수직 성분 포함),
    ṽ_{l,m}(rank3/4용, 길이 절반), rank1~4 프리코더 4개, RI+PMI 선택기(rank 1~4 전수
    탐색 5120후보 = 1024+2048+1024+1024). 선택기는 rank-1도 포함해 전 rank를 하나의
    일반 N×N(N≤4) Gramian 역행렬 기반 MMSE 후-검출 SINR 합 공식으로 통일(rank=1일 때
    기존 "후-빔포밍 파워/N0" 공식과 수학적으로 동치임을 확인) — 4-port/8-port의 2×2
    전용 catastrophic-cancellation 방어(det 부호 확인)보다 일반적인 부분피벗 Gauss-Jordan
    역행렬로 대체(수치적으로 더 견고).
  - `mimo_channel_draw_4x32()`(`mimo.c`), `mimo_mmse_detect_4rx3()`(4Rx×3Layer MMSE,
    3×3 Gramian, 신규) 추가 — rank4는 기존 `mimo_mmse_detect_4x4()` 그대로 재사용
    (프리코딩 후 유효 채널이 항상 4×rank라 Tx 포트 수 무관, 8-port와 동일 논리).
  - `run_pdsch_cl_32port_simulation()`(`pdsch.c`), `main.c`에 `MIMO_MODE=CL_32PORT`
    라우팅. 4-port/8-port와 달리 **"Adaptive" 단일 시나리오만** 보고(R1fix~R4fix 4개
    추가 시나리오는 최대 4개 코드워드 인코딩 배수만 늘리고 검증 가치가 낮아 범위 제외,
    사용자와 사전 확인 없이 내린 설계 판단) — 대신 SNR별 rank 선택 분포(%)를 출력.
  - Tx 공간상관/TDL/HARQ는 8-port와 동일 사유로 이번 범위에서 제외, 아래 후속 과제로 등록.
  - **검증**: standalone 하네스로 rank 1~4 전체 5120개 코드워드 정규화(오차 ~1e-16)·
    직교성(오차 ~1e-16) 확인. RI 선택기 물리 타당성: i.i.d. 채널에서 SNR
    -5→20dB로 올릴 때 평균 선택 rank가 1.16→4.00으로 단조 증가, 20dB에서 rank4
    선택률 100%로 수렴(4 Rx 안테나가 rank4의 물리적 상한이라 타당). 실제 시뮬레이션
    (MCS10, -10~20dB, 100trial/포인트, 7포인트 총 2.25초)에서 BLER 1.0→0.16로 단조
    감소, 평균 rank 1.16→4.00 동반 상승 확인 — massive MIMO 다중화 이득이 SNR에 따라
    나타나는 것을 직접 실측. 회귀 48/48 유지, clean 빌드 경고 없음(기존 무관 경고만
    유지). 상세는 `docs/analysis/history.md` 참조.
- [x] CL_32PORT Tx 공간상관(Kronecker N1=N2=4 2D) 모델 — 2026-09-01 완료.
  `mimo_apply_tx_correlation_4x32()`(`mimo.c`/`.h`) 신규 — CL_8PORT의 N1=4
  1차원 지수상관을 진짜 2D URA(Uniform Rectangular Array)로 확장: 수평(N1)·
  수직(N2) 두 축에 독립적으로 4x4 지수상관 Cholesky 인수를 적용(두 축이
  서로 다른 텐서 성분이라 순서 무관하게 교환 가능, R_2D=R_horiz⊗R_vert
  분리모델 — URA의 표준적 관례). 편파 간 XPD(rho_xpol)는 기존과 동일하게
  N1/N2와 무관하게 (n1,n2) 위치마다 반복 적용. 신규 설정
  `SPATIAL_CORR_TX_VERT`(수직 축, 기존 `SPATIAL_CORR_TX`는 수평 축으로
  재해석) 추가, `run_pdsch_cl_32port_simulation()`에 배선,
  `config_parser.c` 요약 출력도 CL_32PORT 조건 추가.
  **검증**: 200k Monte Carlo로 수평/수직 실측 공분산이 각각 이론
  rho_h^|i-j|/rho_v^|i-j|와 일치(오차 <0.005), 포트별 분산 정규화(~1.0,
  32포트 전부) 유지 확인. XPD 교차상관도 이론값과 일치(오차 <0.001), 축
  간 교차항은 0에 가까움(축 분리성 확인). 시뮬레이션 레벨: i.i.d.(모든
  rho=0)에서 rank가 섞여 나옴(평균 2.31) → rho_h=rho_v=rho_xpol=0.999
  (거의 완전 상관)에서 rank1 100% 선택 + BLER 1.0→0.0067로 극적 개선 —
  4-port/8-port 때와 동일한 물리적으로 타당한 패턴. 회귀 48/48 유지,
  clean 빌드 경고 없음.
- [x] CL_32PORT TDL/HARQ 변형 — 2026-09-01 완료. `run_pdsch_cl_32port_tdl_simulation()`
  (Wideband PMI, H_avg 기준, "Adaptive" 단일 시나리오 — flat 버전과 동일 설계
  판단)과 `run_pdsch_cl_32port_harq_simulation()`(FLAT/TDL 모두 지원, 시도0에서
  rank 1~4 중 하나로 PMI 고정, CW 수=rank 최대 4개)를 8-port 구조 그대로 32
  Tx 포트·rank 1~4로 확장. Tx 공간상관(2D Kronecker)도 TDL/HARQ 양쪽에 배선
  (RE별 H_cache에 개별 적용 후 H_avg 누적, 8-port TDL과 동일 순서). 회귀 48/48
  유지, clean 빌드 경고 없음. 수동 검증: TDL(MCS10, -10~20dB)에서 BLER
  1.0→0.86 단조감소·평균rank 1.00→3.79 동반 상승, HARQ(MCS15, FLAT/TDL 둘 다,
  -5~10dB)에서 BLER(1st) 높게 유지된 채 재전송으로 BLER(HARQ, 최종) 개선
  (예: TDL 10dB에서 1.0→0.84)·AvgTx 고SNR에서 하락·AvgRank도 SNR 따라 상승 —
  기존 4/8-port TDL/HARQ와 동일한 물리적으로 타당한 패턴. 상세는
  `docs/analysis/history.md` 참조.
- [x] Eigen-Beamforming(SVD) 16-port, 비-코드북 개루프 방식 — 2026-08-31 완료. 코드북
  (CL_4/8/32PORT) 계열과 달리 채널 H의 SVD에서 직접 프리코더를 계산(genie-aided CSI,
  실제로는 TDD SRS reciprocity에 대응, 3GPP 코드북 표준과 무관한 신호처리 알고리즘 —
  Tse & Viswanath 참고서적 근거).
  - **구현**: `eigen_16port.c`/`.h` 신규 — 4×4 Hermitian 고유분해(복소 Cyclic Jacobi
    알고리즘, 신규 구현)로 4×16 채널의 SVD를 경량 계산(H·H^H 4×4 Gram 행렬 경유,
    v_i=H^H·u_i/σ_i). rank 적응은 등력 분배 기준 추정 용량 최대화(water-filling
    아님, codebook_32port의 "열당 1/rank 전력" 관례와 동일 가정이라 직접 비교
    가능). `mimo_channel_draw_4x16()`(`mimo.c`) 추가. 검출은 CL_32PORT와 동일한
    기존 검출기(mrc_combine_4rx/mimo_mmse_detect_4rx2/4rx3/4x4)를 그대로 재사용
    (SVD 정의상 H·v_i=σ_i·u_i라 스트림이 자동 직교, 유효 채널이 대각에 가까워
    특히 잘 조건화됨).
  - `run_pdsch_eigen_16port_simulation()`(`pdsch.c`), `main.c`에
    `MIMO_MODE=EIGEN_16PORT` 라우팅. Tx 공간상관/TDL/HARQ는 다른 massive MIMO
    모드와 동일 판단으로 이번 범위 제외.
  - **검증**: 신규 Jacobi 고유분해는 이번 세션에서 가장 리스크가 큰 신규 수치
    알고리즘이라 시뮬레이션 배선 전에 먼저 standalone 하네스로 독립 검증(2000회
    무작위 채널) — SVD 재구성 오차 ‖H−UΣV^H‖_F 최대 7.7e-15, U/V 정규직교성
    오차 최대 2.1e-15, 정의 관계 ‖H·v_i−σ_i·u_i‖ 최대 8.5e-15(전부 부동소수점
    잡음 수준), 특이값 내림차순 위반 0건. rank 선택기 물리 타당성(500회×6
    SNR포인트): 평균 rank가 SNR에 따라 매끄럽게 상승(-5dB 2.19→0dB 이후 rank4
    포화) — CL_32PORT보다 저SNR에서도 rank4를 더 적극적으로 씀(코드북 양자화
    없이 진짜 채널 고유벡터를 쓰므로 약한 고유모드도 실제로는 유효 이득이 크기
    때문, 물리적으로 타당). 실제 시뮬레이션(MCS10, -10~20dB, 100trial/포인트,
    7포인트 총 0.98초): BLER 1.0→0.0 완전 수렴(20dB에서 정확히 0 — CL_32PORT는
    같은 조건에서 0.16 바닥, 코드북 프리코더의 잔여 스트림간 간섭 때문 — 두
    계열이 물리적으로 구별되는 특성을 보임, 개루프 방식이 폐루프 코드북 방식
    대비 양자화 손실 없는 상한임을 보여주는 타당한 결과). 회귀 48/48 유지, clean
    빌드 경고 없음. 상세는 `docs/analysis/history.md` 참조.

- [x] CSI 보고 확장 — Type I SP 8-port 코드북 (N1=4,N2=1,O1=4, P=8) — 2026-08-27 착수,
  2026-08-30 rank-1 완료, 2026-08-31 rank-2/시뮬레이션 배선/TDL·HARQ/Tx 공간상관 전체 완료.
  - **완료(2026-08-30)**: `PHY/src/codebook_8port.c`/`include/codebook_8port.h` 신규 — rank-1.
    (N1,N2)=(4,1)/(O1,O2)=(4,1)을 Table 5.2.2.2.1-2에서 확정(문서 내 유일한 8-port 1D
    배열 옵션, 대안은 (2,2)/(4,4) 2D 배열). rank-1 공식은 4-port(N1=2) 코드를 N1=4로 그대로
    일반화 — 64개 코드워드 전부 ||W||²=1 확인.
  - **완료(2026-08-31)**: rank-2 구현. 3gpp-server MCP 연결로 TS 38.214 v18.10.0 원문을
    이미지(뷰어블 PNG)로 직접 열람해 Table 5.2.2.2.1-3(i1,3→k1,k2 매핑)의 실제 값을 확정 —
    N1>2,N2=1 그룹은 **k1 = i1,3·O1**(N1·O1/2 가설은 기각), k2=0. Table 5.2.2.2.1-6의
    codebookMode=1 원식(`W=1/√(2P)·[v_l,v_l'; φ_n v_l, −φ_n v_l']`)을 그대로 적용해
    `codebook_type1_sp_8port_rank2()` 구현. 256개 코드워드(16빔×4×4) 전부 열당 전력=0.5·
    직교성 오차~1e-17(부동소수점 잡음 수준) 확인. clean 재빌드 경고 없음, 회귀 48/48 유지.
    상세 유도 과정은 `docs/analysis/history.md` 참조.
  - **완료(2026-08-31, 4-port 정합화)**: 기존 `codebook.c`(4-port)의 rank-2도 동일 스펙
    원식(같은 n, 2번째 열만 부호반전, k1=i1_3·O1)으로 통일 — 8-port와 동일한 단일 수식
    구조로 재작성, "변형 A/B" 자체 스킴과 라벨 스왑 제거. `codebook.h` 문서, print 함수,
    `codebook_type1_sp_4port_ri_pmi_select()` 탐색 범위(48→64 후보, 총 80→96)도 함께
    갱신. 64개 코드워드 전부 정규화(오차~1e-16)·직교성(오차~1e-17) 확인, clean 재빌드
    경고 없음, 회귀 48/48 유지. RI/PMI가 전수탐색이라 BLER 실측치 자체에는 영향 없을
    것으로 판단(코드워드 집합의 물리적 성질 동일, 라벨/스킴만 달랐음).
  - **완료(2026-08-31, 시뮬레이션 배선)**: `run_pdsch_cl_8port_simulation()`(`pdsch.c`)을
    `CL_4PORT` 평탄 페이딩 버전 구조 그대로 8 Tx 포트로 확장해 신규 작성, `main.c`에
    `MIMO_MODE=CL_8PORT` 라우팅 추가. `codebook_type1_sp_8port_ri_pmi_select()`
    (rank-1 64 + rank-2 256 = 320 후보 전수탐색), `mimo_channel_draw_4x8()` 신규 추가.
    프리코딩 이후 유효 채널이 항상 4×rank라 `mrc_combine_4rx`/`mimo_mmse_detect_4rx2`는
    Tx 포트 수 무관하게 그대로 재사용. (이 시점엔 Tx 공간상관·TDL·HARQ 미구현 — 모두
    아래 두 항목에서 이어서 완료.)
  - **검증**: 회귀 48/48 유지, clean 빌드 경고 없음. 수동 SNR 스윕(-10~10dB, MCS10,
    200trial)으로 BLER 단조감소·R1선택률(82.5%→0%, 저SNR→고SNR)이 MIMO 용량 이론과
    일치함을 확인. 동일 MCS/SNR로 기존 CL_4PORT와 비교해 정성적으로 같은 패턴(고정 MCS
    때문에 R2fix BLER이 R1fix보다 나쁜 것도 4-port와 동일한 기존 특성)임을 확인 — 8-port가
    새로 이상 동작하는 게 아니라 이미 검증된 4-port 패턴을 그대로 따름.
- [x] CL_8PORT TDL/HARQ 변형 — 2026-08-31 완료. `run_pdsch_cl_8port_tdl_simulation()`
  (Wideband PMI, H_avg 기준)과 `run_pdsch_cl_8port_harq_simulation()`(FLAT/TDL 모두
  지원, 시도0 PMI 고정)을 4-port 구조 그대로 8 Tx 포트로 확장. `tdl_draw`/
  `tdl_freq_response`가 Tx-Rx 페어별 독립 호출이라 tdl.c/tdl.h 수정 없이 taps 배열
  크기만 4x8로 확장해 재사용. 회귀 48/48 유지, clean 빌드 경고 없음(기존 무관 경고 3건만
  유지). 수동 검증: TDL BLER 단조감소·R1선택률 86.5%→0% 정상 수렴, HARQ는 BLER(1st)
  높게 유지 후 재전송으로 BLER(최종) 크게 개선·AvgTx 고SNR에서 하락 확인 — 상세는
  `docs/analysis/history.md` 참조.
- [x] CL_8PORT Tx 공간상관(Kronecker) 모델 — 2026-08-31 완료. `mimo_apply_tx_correlation_4x8()`
  (`mimo.c`/`mimo.h`) 신규 — N1=2 기존 모델(R_ant=[[1,rho],[rho,1]] 대칭 제곱근)이 실은
  2-원소 지수상관 모델(rho^|i-j|)과 동일함을 확인하고, N1=4로 직접 일반화(Cholesky 인수
  L, R_ant=L·L^T — 입력이 i.i.d. 복소 가우시안이라 대칭 제곱근과 통계적으로 동등,
  4x4 고유분해 불필요). XPD 누설 상관(rho_xpol)은 예상대로 N1과 무관하게 동일 로직
  재사용(편파 그룹 간 2x2 블록을 n1=0~3 각각에 반복 적용). `codebook_type1_sp_8port_*`
  세 시뮬레이션 함수(평탄/TDL/HARQ) 모두에 배선, `config_parser.c` 요약 출력도 CL_8PORT
  포함하도록 조건 확장. 회귀 48/48 유지, clean 빌드 경고 없음. 수동 검증: 200k trial
  Monte Carlo로 실측 공분산이 이론 rho^|i-j|와 일치(오차 <0.003)·포트별 분산 정규화
  유지(~1.0) 확인, rho_xpol 교차편파 상관도 이론값과 일치(오차 <0.005) 확인, rho=0(i.i.d.)
  →rho=rho_xpol=0.999(near-rank-1)에서 R1선택률 0.4%→100%로 물리적으로 타당하게 수렴
  (2026-08-27 4-port XPD 검증과 동일 패턴) — 상세는 `docs/analysis/history.md` 참조.
  CL_8PORT의 시뮬레이션 배선/TDL/HARQ/공간상관 항목 전체 완료.

---

## 다음 후보

> 우선순위 원칙(2026-08-02, 사용자 확인): NTN 같은 새 영역 확장보다 **기존 LLS의 완성도**(이미 있는 채널/기능들이 빠짐없이 조합되어 동작하는 것)를 우선한다.

### A. 조합 공백 메우기 (기존 기능이 서로 안 엮이는 부분)
- [x] `SM_4X4`(4x4 SU-MIMO)에 TDL 변형 추가 — genie-aided CSI, 2026-08-02 완료
- [x] `SM_4X4`에 HARQ 조합 추가 — flat/TDL 모두 지원, genie-aided, 2026-08-03 완료
- [x] `CL_4PORT`에 TDL 변형 추가 — Wideband PMI (H_avg 기반), genie-aided, 2026-08-03 완료
- [x] `CL_4PORT`에 HARQ 조합 추가 — flat/TDL 모두 지원, 시도0 PMI 고정, 2026-08-03 완료
- [x] PUSCH에 HARQ 재전송 조합 추가 — TDL/flat/AWGN 지원, LS est., 2026-08-03 완료

### B. 이번 세션에 확인된 후속 과제
- [x] CL_4PORT 교차편파(XPD) 누설 상관 모델 추가 — `SPATIAL_CORR_XPOL`, Kronecker R_pol⊗R_ant 확장, 2026-08-27 완료. 부작용으로 `codebook.c` RI/PMI 선택기의 기존 버그(2건) 발견 — 상세는 아래 신규 항목과 `docs/analysis/history.md` 참조
- [x] UL CLPC에 채널 페이딩/이동성(시변 PL) 추가 — `UL_PC_PL_VAR_STD_DB`/`UL_PC_PL_VAR_CORR`, Gauss-Markov(AR1), 2026-08-27 완료
- [x] CL_4PORT rank-1/rank-2 코드북 전력 정규화 불일치 수정 — `codebook_type1_sp_4port_rank2()`의 컬럼당 `norm`을 `0.5`→`0.5/√2`로 변경해 총 송신전력을 rank-1(‖W‖²=1)과 동일하게 맞춤(레이어당 0.5, 합계 1). 사용자 확인 후 2026-08-27 완료. 수정 후 재검증: iid Rayleigh에서 R1선택률이 SNR에 따라 정상적으로 갈림(저SNR≈90%, 고SNR≈0%, MIMO 이론과 일치), rho=rho_xpol→1(완전 rank-1 채널)에서 전 SNR R1선택률=100%로 정확히 수렴 — 2026-08-02 "R1선택률=0%" 결론과 이번 XPD 극단값 0% 결과 둘 다 이 정규화 버그가 근본 원인이었음이 확정됨. 회귀 48/48 유지. 상세는 `docs/analysis/history.md` 참조
- [x] `codebook.c` RI/PMI 선택기 2×2 Gramian 역산의 catastrophic cancellation 방어 — `det`가 이론상 항상 `>= N0²`로 양수인데 부동소수점 뺄셈 오차로 음수/근사영이 될 수 있어 `a0/a1`이 허수적으로 1 근처까지 치솟는 결함을 발견·수정(2026-08-27, 상기 XPD 극단값 검증 중 발견). 회귀 48/48 통과, 위 정규화 이슈와는 별개

### C. 새 영역 확장 (완성도 작업보다 낮은 우선순위)
- [x] MU-MIMO — ZF-BF, Nt=4/K=2/평탄 페이딩으로 2026-09-01 완료
  (TDL/HARQ/K>2 확장은 위 "진행 중" 섹션 참조)
- [x] OLLA (Outer Loop Link Adaptation) — SISO/AWGN 시계열로 2026-09-01 완료
  (다른 MIMO 모드로의 확장은 위 "진행 중" 섹션 참조)
- [x] 빔 관리 (SSB/CSI-RS 기반 P1 빔 스위핑) — gNB Tx 빔 스위핑만
  2026-09-01 완료 (UE Rx 빔 스위핑/P2/P3는 위 "진행 중" 섹션 참조)
- [ ] CSI 보고 확장 — Type I SP 4/8포트는 완료(2026-08-31), Type II는 아직 없음
- [x] Massive MIMO — 32-port(N1=4,N2=4) Type I SP 코드북(rank 1~4) + 16-port
  Eigen-Beamforming(SVD, 비-코드북) 둘 다 완료(2026-08-31, "64안테나"는 Rel-18
  Type I SP 스펙상 최대인 32포트×2편파로 재정의). Tx 공간상관/TDL/HARQ는 위 "진행 중"
  섹션의 후속 과제로 별도 등록.

## 보류/결정 사항

- **NVIDIA Aerial SDK 연동**: 보류 (사용자 명시적 결정, 2026-07-14) — 연동하지 않기로 함
- **NTN (Non-Terrestrial Network)**: 보류 (사용자 명시적 결정, 2026-08-02) — 기존 LLS 완성도를 우선하기로 함, 새 영역 확장은 나중

---

## 완료

_(상세 구현/실측 결과는 `docs/analysis/history.md` 참조 — 2026-08-02 CLAUDE.md 분리 시점 이전 이력은 전부 그쪽으로 이관됨)_

- [x] PBCH/PDCCH에 페이딩 채널(FLAT_FADING/TDL) 변형 추가 (2026-08-02) — LLS 완성도 작업 1단계, 상세는 `docs/analysis/history.md` 참조
- [x] Group A 조합 공백 전체 완료 (2026-08-03) — SM_4X4+TDL, SM_4X4+HARQ, CL_4PORT+TDL, CL_4PORT+HARQ, PUSCH+HARQ
- [x] P0 정합성 수정 완료 (2026-08-03) — TABLE3 fallback 제거, config validation 추가, STRUCTURE.md 라우팅 갱신, 48-case 회귀 테스트 스크립트(`PHY/regression_test.sh`) 신규 작성·전체 통과
- [x] Mac → Tailscale → Windows 4060 PC의 WSL2 원격 개발 경로 구축 (2026-08-03) — WSL OpenSSH 포트 `22299`, Tailscale 전용 연결, SSH 키 인증, Mac 별칭 `KANG_HOME`; 상세는 `docs/analysis/history.md` 참조
