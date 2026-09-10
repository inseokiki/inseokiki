# PHY/tests — numeric unit tests

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
bash tests/run_numeric_tests.sh
```

Each test is a standalone `.c` file with its own `main()`, compiled once
per run (not cached) and executed immediately; a non-zero exit from any
test fails the whole run. This mirrors `regression_test.sh`'s "build
first, then run" convention rather than introducing a second build
system.

## What's covered

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
- `test_mimo_detection.c` — `mimo.c`'s SU-MIMO detectors: `mrc_combine()`/
  `mrc_combine_4rx()` noise-free exact recovery; `mimo_zf_detect()` (2x2)
  noise-free round-trip plus a hand-computed example; `mimo_mmse_detect()`/
  `mimo_mmse_detect_4x4()`'s N0→0 ZF limit; `mimo_zf_detect_4x4()`'s
  singular-H (duplicate rows) fallback contract; `mimo_mmse_detect_4rx2/
  4rx3()`'s N0→0 limit for the 4Rx-overdetermined 2/3-layer case. (EVD
  itself is covered by `test_matrix.c`; codebook geometry is deferred —
  see `tasks/todo.md`.)

## Adding a new test

Add `tests/test_<module>.c` with its own `main()` returning 0/1, then add
one `run_test test_<module> <obj1.o> <obj2.o> ...` line to
`run_numeric_tests.sh` naming the project `.o` files it needs from
`../build/`.
