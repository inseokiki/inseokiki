# PHY/tests — numeric tests and measurement campaigns

## AWGN measurement campaign

From the repository root:

```bash
python3 PHY/tests/run_awgn_campaign.py --list
python3 PHY/tests/run_awgn_campaign.py --output /tmp/awgn-measurements --trials 200
python3 -m unittest discover -s PHY/tests -p test_awgn_campaign.py -v
```

The standard-library runner builds the simulator, then runs 69 representative
settings across all actual AWGN channel families, supported PUCCH/PRACH formats,
CSI-RS/SRS, and supplementary BER/legacy OFDM benchmarks. It saves long-form CSV,
a Markdown summary, original configs/logs and a SHA-256 manifest in a **new**
directory. It never modifies `config/sim_config.txt`. Use repeatable `--case ID`
options for targeted measurements and `--seeds`, `--snr-start/end/step` to set
the sampling protocol. It fails on nonzero exit, incomplete grids, malformed or
nonfinite metrics, invalid probabilities, or inconsistent HARQ/PDCCH results.
These are measurements of the current research implementation, not conformance
tests or a full Cartesian product of every parameter. See each generated
`SUMMARY.md` for scope, denominators and model limitations.

The parser tests deliberately inject incomplete/corrupted simulator output;
they do not replace the C numeric tests below.

`regression_test.sh` is a black-box CLI smoke suite (config in, exit code +
output pattern out) — it confirms dispatch/connectivity but cannot check
numeric correctness of individual modules. These tests fill that gap
(lab/PHY_REVIEW_2026-09-10.md PHY-03): they call the actual project `.c`
modules directly (linked against `../build/*.o`, so `make -f c_Makefile`
must already have been run) and check concrete numeric properties with
deterministic seeds.

## Running

```bash
cd PHY
make -f c_Makefile            # build/*.o must exist first
bash tests/run_numeric_tests.sh --list  # names only; no build required
bash tests/run_numeric_tests.sh test_pusch_codebook_4port test_pusch_codebook_4port_integration
bash tests/run_numeric_tests.sh        # all tests, when full validation is intended
```

Test names are exact matches. Multiple names run once each in registry order;
unknown names or invalid option combinations exit with code 2 before any test
runs. `--help` prints usage. No arguments preserves the full-suite behavior.

Each test is a standalone `.c` file with its own `main()`, compiled once
per run (not cached) and executed immediately; a non-zero exit from any
test fails the whole run. This mirrors `regression_test.sh`'s "build
first, then run" convention rather than introducing a second build
system.

## What's covered

- `test_nr_rate_matching.c` — independent py3gpp position vectors for the
  C SCH puncture/filler/RV/Qm mapping. Checks Tx bits, turbo feedback, inverse
  LLR scatter, Chase/IR accumulation and unequal three-CB output allocation.
  The committed header avoids any Python dependency when running C tests.

- `test_rng.c` — `utils.c`'s RNG (PHY-05): same seed reproduces the exact
  same `gen_random_bits()`/`randn()` sequence, different seed diverges,
  and a mid-process reseed doesn't leak the previous stream's cached
  Box-Muller spare into the new stream's first draw.
- `test_mumimo.c` — `mumimo.c` ZF-BF precoder: H·W is diagonal (off-diag
  ~0, diagonal real & positive — **not** the identity matrix, PHY-03
  correction), column power normalization, a hand-computed closed-form
  case, and singular-input (duplicate user channels) graceful fallback.
- `test_polar.c` — `polar.c`/`polar_rate_match.c`: TS 38.212 5.3.1
  noise-free round-trip (reliability sequence, input interleaving, PC
  bits) across PBCH/PDCCH/UCI configurations, CA-SCL(L=1)≡plain-SC
  equivalence, CA-SCL(L=8) BLER improvement over plain SC, and PDCCH's
  RNTI-masked CRC path selection (correct/incorrect RNTI). Also (§5
  3단계, 2026-09-11): `polar_encode()` at rate-1 (K=N) matches an
  independently-derived *recursive* Arikan-kernel transform (structurally
  different from `polar.c`'s own iterative butterfly, which isn't even
  exported); `polar_interleaver()` matches TS 38.212 Table 5.4.1.1-1,
  independently re-transcribed from the local primary-source docx (not
  copied from `polar_rate_match.c`'s own table); `polar_uci_npc()`'s
  18≤K≤25 boundary; and `polar_decode_scl()`'s documented return-value
  contract (1 iff CRC-passing, `decoded[]` always a valid 0/1 array even
  when the CRC-fail fallback path is exercised — direct regression
  coverage for the p==0 fallback-candidate bug fixed 2026-09-10).
- `test_ldpc.c` — `nr_sch.c`/`nr_rate_matching.c`/`ldpc.c`/`ldpc_nr.c`:
  TS 38.212 5.2.2 segmentation round-trip for both C=1 and C>1 (reusing
  the already-known-valid (TBS=8426, MCS10/TABLE1) combination from
  `regression_test.sh`'s "PDSCH legacy AWGN C=2 segmentation" case —
  see `nr_sch.h`'s documented open question on non-integer B'/C before
  picking a different B), and the 5.4.2.1 circular-buffer's boundary
  behavior (exact non-filler-length selection, wrap-around, Chase
  combining). Also (§5 3단계, 2026-09-11): `nr_select_bg()`'s TS 38.212
  6.2.2 base-graph-selection boundary (A≤292, A≤3824&&R≤0.67, R≤0.25),
  `nr_select_zc()`'s minimal-Zc search cross-checked against an
  independent search over the same `ZC_SETS` spec data, `nr_ldpc_k0()`
  against an independent re-statement of Table 5.4.2.1-2's rv0-3
  formula, `nr_ldpc_er_alloc()`'s sum/multiple-of-NlQm/floor-ceil-split
  invariants, and an LDPC encoder syndrome check (`H·codeword==0 mod 2`,
  using `build_H_nr()`'s output — a code path separate from
  `ldpc_encode_nr()` — cross-checked via a freshly-written syndrome
  computation, not `ldpc_decode()`'s own BP loop).
- `test_crc.c` — `crc.c`: CRC16 cross-checked against the public
  CRC-16/XMODEM known check value ("123456789" → 0x31C3), full-codeword
  divisibility-by-generator for all 4 CRC types (CRC24A/B/C, CRC16),
  every single-bit-flip position detected, `attach_crc`'s bit-order
  (unpermuted data prefix + `compute_crc()`-matching suffix), and RNTI
  masking (rnti=0 no-op, correct/wrong RNTI, masked-vs-plain check_crc).
- `test_modulation.c` — `modulation.c`: exhaustive constellation-mapping
  check for QPSK/16QAM/64QAM against an independently-derived
  non-recursive TS 38.211 §5.1 closed-form (all symbols), average
  symbol power == 1.0, noise-free modulate→demodulate round-trip, and
  QPSK LLR sign convention + exact 2/noise_var magnitude scaling.
- `test_ofdm.c` — `ofdm.c`: `radix2_fft()` forward/inverse impulse and
  single-tone closed-form identities (N=4..64), `ofdm_modulate()`'s
  frequency-impulse→constant-time-signal case (amplitude exactly 1/N,
  including inside the CP), CP-is-exact-tail-copy structural check,
  Parseval energy conservation, and modulate→demodulate round-trip
  (N=1024/4096, realistic CP lengths).
- `test_dft_precode.c` — `dft_precode.c` (TS 38.211 §6.3.1.4 PUSCH
  transform precoding): hand-derived M=2/M=4 unitary-DFT matrices
  applied to concrete numeric examples, impulse/all-ones closed-form
  responses and Parseval per direction for both power-of-2 and
  non-power-of-2 M (1,2,3,4,5,6,12,15,24,25,48 — this direct-sum
  implementation exists specifically for the non-power-of-2 case, since
  3GPP restricts M to products of {2,3,5}), and round-trip.
- `test_matrix.c` — `utils.c`'s `herm4x4_eig()` (Cyclic Jacobi
  eigendecomposition of a 4x4 Hermitian matrix, shared by
  `eigen_16port.c`/`ul_eigen_bf.c`): exact closed forms for diagonal and
  identity input (no rotation needed), a rank-1 outer-product case with
  a hand-derivable spectrum ([|v|^2,0,0,0]), and — over 500 random
  Hermitian trials each — trace/Frobenius-norm invariants, the
  reconstruction identity `V*diag(eigval)*V^H == A`, orthonormality
  (`V^H V == I`), and descending eigenvalue order.
- `test_mimo_correlation.c` — `mimo.c`'s Tx-side spatial correlation
  (Kronecker model: `mimo_apply_tx_correlation_4x4/4x8/4x32`) — note
  channel.c/tdl.c have no spatial-correlation code themselves, this is
  the actual scope of the plan's "채널 공간상관" row. Exact no-op at
  rho<=0, the N1=2 block's `a^2+b^2=1, 2ab=rho` algebraic identity
  recovered from elementary-input output, hand-derived composed
  R_ant-then-R_pol responses, a hand-derived AR(1)/Markov-correlation
  Cholesky closed form (`L[i][0]=rho^i`, `L[i][j]=rho^(i-j)*sqrt(1-
  rho^2)`) checked against the N1=4 (8-port/32-port) Cholesky path via
  elementary-column inputs, and a `rho=1.0` clamp finiteness check.
- `test_channel_estimation.c` — `channel_estimation.c`: `ls_estimate()`
  exact noise-free recovery; `interpolate_channel()`'s exact knot
  pass-through, exact interior reproduction of a linear channel, and
  flat extrapolation outside the pilot range; `mmse_channel_estimate()`
  ≡ `mmse_build_filter()`+`mmse_apply_filter()` exactly, plus its Wiener-
  filter N0→0/N0→∞ limits (→identity/→zero); `mmse_build_avg_filter()`'s
  hand-solved single-pilot/coincident-target scalar case; `dft_channel_
  estimate()`'s truncation structure (already-in-window support is a
  no-op, entirely-out-of-window support zeroes everything); `zf_equalize()`/
  `mmse_equalize()` against independently-recomputed formulas, including
  the N0→0 MMSE→ZF limit.
- `test_pusch_channel_estimation_noise.c` — noisy component fixture using the UL driver's
  one-RB per-layer FDM pilot layout, ranks 1–4 and N0=0.02/0.5.
  Each of 20 cases uses 20,000 seeded independent pilot-noise draws.
  Checks flat pilot averaging and LS/interpolation MSE against elementary
  independent-noise variance plus known affine-channel edge-hold bias;
  all 12 REs are checked with 5% relative tolerance. This tests component
  composition, not the driver's inline LS code, standard DMRS allocation,
  or calibration of final LLRs with channel-estimation uncertainty.
- `test_mimo_detection.c` — `mimo.c`'s SU-MIMO detectors: `mrc_combine()`/
  `mrc_combine_4rx()` noise-free exact recovery; `mimo_zf_detect()` (2x2)
  noise-free round-trip plus a hand-computed example; `mimo_mmse_detect()`/
  `mimo_mmse_detect_4x4()`'s N0→0 ZF limit; `mimo_zf_detect_4x4()`'s
  singular-H (duplicate rows) fallback contract; `mimo_mmse_detect_4rx2/
  4rx3()`'s N0→0 limit for the 4Rx-overdetermined 2/3-layer case. (EVD
  itself is covered by `test_matrix.c`; codebook geometry is deferred —
  see `tasks/todo.md`.)
- `test_ldpc.c` (§5 3단계 "HARQ·적응 상태" group, 2026-09-11 addition):
  `tb_cb_isolation_check()` — with a C=2 transport block, heavily
  corrupting only code block 0's received LLRs fails CB0's own CRC24B
  while code block 1 (independently allocated `soft_buf`, decoded
  separately) still passes CRC24B with bit-exact payload recovery —
  confirms no cross-code-block state bleed. (Buffer reset/RV-change/
  Chase-combining coverage for the same group was already in place from
  `harq_buffer_boundary_check()`, 2026-09-10.)
- `test_beam_mgmt.c` — `beam_mgmt.c`'s P1/P2/P3 beam management sweep
  (§5 3단계 "빔관리 결정론적 상태 천이"): at N0=0 (noiseless),
  `beam_mgmt_p1_sweep()` selects exactly `beam_mgmt_genie_best()`'s grid
  point/gain for on- and off-grid true channels; `beam_mgmt_true_channel()`
  at integer (l,m,n) reproduces `codebook_type1_sp_32port_rank1()`'s own
  steering vector up to a global phase; `beam_mgmt_p3_sweep()`'s
  true-AoA→selected-UE-beam mapping is the exact fixed mirror bijection
  `(BM_UE_CAND-true_idx) mod BM_UE_CAND` (empirically confirmed against
  the actual conjugate-combining convention before being encoded as the
  expected value, not assumed) with full coherent array gain recovered
  every time; the full P1→P3→P2→P1 chain is self-consistent (re-sweeping
  P2's effective channel reselects the identical original gNB Tx beam).
- `test_codebook_ri_pmi.c` — `codebook.c`'s
  `codebook_type1_sp_4port_ri_pmi_select()` (§5 3단계 "RI/PMI 결정론적
  상태 천이"; codebook geometry itself remains a separate open item, see
  `tasks/todo.md`): identical-input determinism; a row-rank-1 channel
  selects rank=1 and a near-identity well-conditioned channel selects
  rank=2 at high SNR, both matching their own rank-restricted search's
  optimum PMI; selected rank is monotonically non-decreasing over a
  descending-N0 (ascending-SNR) sweep on a fixed channel.
- `test_codebook_8port_olla.c` — 8-port RI/PMI and rank-aware effective
  SNR: a rank-1 single-coefficient channel has the hand-derived 1/(8N0)
  SNR; a full-rank channel selects rank 2 and its effective SNR matches
  an independent post-MMSE capacity calculation for the selected precoder.
- `test_codebook_32port_olla.c` — ranks 1–4 use precoders with orthogonal
  columns; matched channels have a closed-form per-layer SNR of
  `gain²/(rank²·N0)`, independently checking the MCS input in every rank.
- `test_pusch_codebook_4port.c` — checks all 62 UL four-port TPMI matrices have
  orthogonal columns and unit total power after normalization, anchors
  selected entries to TS 38.211 Tables 6.3.1.5-3/-5/-6/-7, and checks
  low/high SNR rank selection.
- `test_pusch_codebook_4port_integration.c` — executes the production extended UL driver for
  all 24 combinations of rank 1–4, flat/TDL, and non-HARQ/IR/Chase.
  Test-only observers require positive finite per-RE variances, frequency
  variation in TDL and constant variance in flat channels, RV order, zero
  initial soft buffers and retained values on retries, and exact attempt
  counts. Eight additional high-SNR flat cases check real decoded payloads
  and CRC acceptance after one or two attempts, with single/multiple CBs
  and IR/Chase. Each CB's buffer must retain exactly its saved contents
  until its next combine call, and different CBs must use distinct buffers.
  Rank is fixed; CRC rejection is controlled, while acceptance requires the
  real CRC check. Channel generation, estimation, detection and coding stay
  real. This is a wiring/state test, not an independent PHY reference or
  BLER performance test. Six further sequences each run four TBs in one
  driver invocation (flat/independent TDL/correlated TDL × IR/Chase), changing rank 4→1→4→2. Flat
  sequences alternate single/multiple CBs and forced failure/real ACK at
  attempts 1/2/1; TDL sequences force all four attempts. Initial zero buffers,
  RV restart and per-TB call counts check state isolation between TBs.
  A separate receiver fixture covers all 62 TPMIs with a known complex
  diagonal physical channel: noiseless one-RB FDM pilot recovery (including
  unequal per-layer pilot counts), near-zero-noise symbol recovery, and
  detector residual energy at N0=0.001/0.1/10. Receiver basis inputs measure
  its linear response; interference plus noise energy is compared with the
  reported variance without reusing the production inverse/alpha formula.
  This fixture tests component composition with perfect channel recovery,
  not noisy-estimation uncertainty, extreme singular channels or standard
  DMRS allocation conformance.
  The driver source is included with local observer macros (do not also
  link `pusch_codebook_4port_sim.o`).
- `test_link_adaptation.c` — `olla.c` (§5 3단계 "OLLA 결정론적 상태
  천이") and `ul_power_ctrl.c`'s TPC decision ("ULPC 결정론적 상태
  천이"): `olla_init()`'s `step_up_db` formula exactly; `olla_update()`'s
  offset after a fixed ACK/NACK sequence matches exact hand-accumulation,
  and returns to exactly 0 after every full cycle at the target ACK
  ratio; `olla_select_mcs()`'s boundary never selects an MCS exceeding
  its own independently-recomputed required-SNR, is monotone in achieved
  spectral efficiency over a fine SNR sweep, and floors to MCS 0 far
  below every requirement. `ul_power_ctrl.c`'s previously-`static`
  `tpc_decide()` was renamed `ulpc_tpc_decide()` and exposed via
  `ul_power_ctrl.h` specifically so this file could call the real
  decision function instead of re-deriving it (2026-09-11, the one
  production-code change this session made — mechanical rename only, two
  call sites updated, behavior unchanged and re-verified via
  `run_ulpc_simulation()` CLI smoke run): exact `{-1,0,+1,+3}` dB bucket
  at every threshold including both closed boundaries of the `[-0.5,0.5]`
  deadband; the f(i) accumulator (restated as the same two lines
  `run_ulpc_simulation()` itself uses, since that loop lives inside a
  printf-driven Monte-Carlo driver, not a separately callable unit)
  reaches the ±30dB clamp in exactly the hand-computed number of steps
  and stays pinned there.

- `test_codebook_geometry.c` (`tasks/todo.md`'s "SU-MIMO codebook 기하
  검증" item, 2026-09-11 addition): exhaustive per-codeword unit-norm
  and inter-layer-orthogonality check across every candidate this
  project's three Type I SP codebooks generate — 4-port rank-1/2 (32+64
  codewords), 8-port rank-1/2 (64+256), 32-port rank-1/2/3/4
  (1024+2048+1024+1024) — 4544 codewords total, every one checked to
  `<1e-9` on both `||W[:,c]||^2==1/rank` and every pairwise cross-column
  product `==0`. `codebook_32port.c`'s own
  `codebook_type1_sp_32port_print()` already runs the identical
  exhaustive loops/formulas for rank 1-4 (as a human-read max-error
  printf, not a pass/fail assertion) — this file reuses that same
  candidate enumeration and adds equivalent exhaustive coverage for the
  4-port/8-port codebooks, which had no aggregated-max-error check at
  all before this.

- `test_tbs.c` (`tasks/todo.md`'s "TBS를 TS 38.214 §5.1.3.2 표준 절차로
  교체" item, 2026-09-14 addition): `tbs.c`'s `nr_determine_tbs()` — three
  hand-derived examples (Step 3 small-N_info, Step 4 both code-rate
  branches) computed independently in the test file's own comments; a
  2492-case sweep confirming every Step-3 (N_info≤3824) output is an
  exact Table 5.1.3.2-1 entry (independently re-transcribed, not
  `#include`-shared with `tbs.c`'s own copy); the `TBS≥24` floor;
  monotonicity in `n_re_qm` swept across the Step3/Step4 boundary; and
  the exact N_info==3824.0 boundary (closed on the Step-3 side, Step-4
  side clears the 3840 floor).

**Not unit-tested (structural, not skipped)**: §5 3단계's "재전송 상한"
(HARQ max-retransmission) row is embedded inline inside `pdsch.c`/
`pusch.c`/`pucch.c`'s Monte-Carlo `run_*` drivers, interleaved with
per-attempt RNG channel draws and LDPC/Polar encode-decode state — unlike
this group's other rows, there is no separable pure function to call
without refactoring those production simulation drivers (out of scope
for a test-only session). It remains covered only at the integration
level, by `regression_test.sh`'s existing HARQ-enabled cases (which do
exercise `HARQ_MAX_RETX` end-to-end).

## CLI simulation regression selection

From `PHY/`, after building the simulator:

```bash
bash regression_test.sh --list
bash regression_test.sh --match UL_CB_4PORT
bash regression_test.sh "PUSCH UL_CB_4PORT flat" "PUSCH UL_CB_4PORT HARQ TDL"
```

`--match` is a literal, case-sensitive substring (not a regular expression).
Exact labels can be combined and duplicate selections run once. Unknown names,
empty filters and filters matching no cases exit with code 2 before any case
runs. Listing/help need no simulator binary. No arguments preserves full-suite
execution; select affected cases for routine development.

## File naming

Use `snake_case` and identify the physical channel or module before the
purpose: `test_pusch_codebook_4port.c` for codebook numeric checks,
`test_pusch_codebook_4port_integration.c` for driver integration, and
`test_pusch_channel_estimation_noise.c` for estimation-noise statistics.
Use established PHY terms such as PUSCH and DMRS; avoid ad hoc abbreviations
such as `cb4` when `codebook_4port` identifies the module more clearly.

## Adding a new test

Add `tests/test_<module>.c` with its own `main()` returning 0/1, then add
one `register_test test_<module> <obj1.o> <obj2.o> ...` line to
`run_numeric_tests.sh` naming the project `.o` files it needs from
`../build/`.

- `test_tdl_time.c` checks seeded reproducibility, query-order independence,
  sampling without RNG consumption, invalid inputs, zero Doppler in A–E,
  pure LOS quarter-turn, and ensemble powers/covariance (4,000 realizations
  per profile, fD=100 Hz, lags 1/5 ms, power tolerance 8%, covariance 0.07).
  The PUSCH integration test additionally observes 16 pair states per TB and
  sampling times 0/1/2/3 ms across four forced attempts, resetting per TB.

- `test_tdl_mimo.c` checks zero-correlation identity, a known complex impulse,
  invalid arguments and all entries of the 16x16 Tx/Rx covariance with
  12,000 seeded draws for Tx-only, Rx-only and joint correlation (absolute
  covariance tolerance 0.05). This validates the research exponential model,
  not standardized correlation presets or polarized arrays.
