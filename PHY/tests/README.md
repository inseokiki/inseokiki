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
  RNTI-masked CRC path selection (correct/incorrect RNTI).
- `test_ldpc.c` — `nr_sch.c`/`nr_rate_matching.c`/`ldpc.c`: TS 38.212
  5.2.2 segmentation round-trip for both C=1 and C>1 (reusing the
  already-known-valid (TBS=8426, MCS10/TABLE1) combination from
  `regression_test.sh`'s "PDSCH legacy AWGN C=2 segmentation" case —
  see `nr_sch.h`'s documented open question on non-integer B'/C before
  picking a different B), and the 5.4.2.1 circular-buffer's boundary
  behavior (exact non-filler-length selection, wrap-around, Chase
  combining).

## Adding a new test

Add `tests/test_<module>.c` with its own `main()` returning 0/1, then add
one line to `run_numeric_tests.sh`'s `TESTS=(...)` array naming the
project `.o` files it needs from `../build/`.
