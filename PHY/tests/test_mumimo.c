/* test_mumimo.c -- permanent numeric regression test for mumimo.c's
 * ZF-BF precoder (lab/PHY_REVIEW_2026-09-10.md PHY-03/PHY-04).
 *
 * Migrated from an ad-hoc scratch harness used during the MU-MIMO K>2
 * extension (2026-09-09) session. Checks:
 *   1. H*W is diagonal: off-diagonal entries ~0 (inter-user interference
 *      cancelled), diagonal entries real and strictly positive.
 *      PHY-03 correction: H*W is NOT the identity matrix in general (its
 *      diagonal magnitude depends on the channel realization and the
 *      1/K column-power normalization) -- do not assert H*W==I.
 *   2. Each precoder column has ||W[:,k]||^2 == 1/K exactly (equal power
 *      allocation convention).
 *   3. A hand-computed H=2*I case matches the closed-form expected W.
 *   4. Singular input (two users with identical channel vectors, making
 *      the K*K Gramian singular) returns a finite (non-NaN/Inf) all-zero
 *      W, not a crash or garbage output (PHY-03 "특이 입력 시 유한 출력").
 *
 * UT-06 (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10): checks 1/2/3 above
 * are closed-form algebraic properties/hand-computed values, not
 * dependent on an external reference implementation -- there is no
 * independent ZF-BF reference vector cross-checked here, but unlike a
 * codec round-trip, correctness here doesn't hinge on two sides of this
 * project agreeing with each other (the math is verified directly).
 *
 * Build/run: see PHY/tests/README.md (run_numeric_tests.sh drives this).
 */
#include "mumimo.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

static int is_finite_cx(cx_t z) {
    return isfinite(creal(z)) && isfinite(cimag(z));
}

int main(void) {
    printf("MUMIMO_NT=%d MUMIMO_K=%d\n", MUMIMO_NT, MUMIMO_K);
    /* UT-03 (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10): this file never
     * calls libc rand() directly -- mumimo_channel_draw() draws from
     * randn() (utils.c's own xorshift64 RNG) -- so the previous srand(2026)
     * here seeded a stream nothing in this test actually reads from,
     * making the "seed" cosmetic only. Seed the RNG that's actually used. */
    rng_seed(2026);

    /* 1+2: random channels, diagonal-not-identity property + column power.
     * UT-04 (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10): plain
     * `if (x > max) max = x` comparisons silently ignore NaN (IEEE 754:
     * every comparison against NaN is false), so a NaN element would
     * never move max/min and would be invisible in the aggregate stats
     * below -- check isfinite() explicitly on every raw H/W/H*W value. */
    int trials = 500;
    double max_offdiag = 0.0, max_imag_diag = 0.0, min_diag_re = 1e9, max_diag_re = 0.0;
    int norm_ok = 1, all_finite = 1;
    for (int trial = 0; trial < trials; trial++) {
        cx_t H[MUMIMO_K][MUMIMO_NT];
        mumimo_channel_draw(H);
        cx_t W[MUMIMO_NT][MUMIMO_K];
        mumimo_zf_precode(H, W);

        for (int i = 0; i < MUMIMO_K; i++)
            for (int t = 0; t < MUMIMO_NT; t++)
                if (!is_finite_cx(H[i][t])) all_finite = 0;
        for (int t = 0; t < MUMIMO_NT; t++)
            for (int k = 0; k < MUMIMO_K; k++)
                if (!is_finite_cx(W[t][k])) all_finite = 0;

        for (int i = 0; i < MUMIMO_K; i++) {
            for (int j = 0; j < MUMIMO_K; j++) {
                cx_t s = CX_ZERO;
                for (int t = 0; t < MUMIMO_NT; t++) s += H[i][t] * W[t][j];
                if (!is_finite_cx(s)) all_finite = 0;
                if (i == j) {
                    if (fabs(cimag(s)) > max_imag_diag) max_imag_diag = fabs(cimag(s));
                    if (creal(s) < min_diag_re) min_diag_re = creal(s);
                    if (creal(s) > max_diag_re) max_diag_re = creal(s);
                } else {
                    double m = cabs(s);
                    if (m > max_offdiag) max_offdiag = m;
                }
            }
        }
        for (int k = 0; k < MUMIMO_K; k++) {
            double n2 = 0;
            for (int t = 0; t < MUMIMO_NT; t++) n2 += CX_NORM(W[t][k]);
            if (fabs(n2 - 1.0 / MUMIMO_K) > 1e-9) norm_ok = 0;
        }
    }
    printf("max |off-diag| = %.3e, max |Im(diag)| = %.3e, diag range = [%.4f, %.4f]\n",
           max_offdiag, max_imag_diag, min_diag_re, max_diag_re);
    CHECK(all_finite, "every H, W, and H*W entry across all trials is finite (no NaN/Inf hiding in aggregate max/min)");
    CHECK(max_offdiag < 1e-9, "H*W off-diagonal (inter-user interference) ~ 0");
    CHECK(max_imag_diag < 1e-9, "H*W diagonal is real (no imaginary residue)");
    CHECK(min_diag_re > 0.0, "H*W diagonal (effective channel gain) always positive");
    /* PHY-03: diagonal varies with the channel realization -- assert it is
     * NOT pinned to 1 (i.e. this is genuinely a diagonal matrix, not I). */
    CHECK(max_diag_re - min_diag_re > 1e-6,
          "H*W diagonal varies across trials (confirms it is NOT the identity matrix)");
    CHECK(norm_ok, "each precoder column has ||W[:,k]||^2 == 1/K");

    /* 3: hand-computed H=2*I case (only meaningful when K==Nt) */
    if (MUMIMO_K == MUMIMO_NT) {
        cx_t H[MUMIMO_K][MUMIMO_NT];
        for (int i = 0; i < MUMIMO_K; i++)
            for (int t = 0; t < MUMIMO_NT; t++)
                H[i][t] = (i == t) ? 2.0 : 0.0;
        cx_t W[MUMIMO_NT][MUMIMO_K];
        mumimo_zf_precode(H, W);
        int identity_like_ok = 1;
        for (int i = 0; i < MUMIMO_K; i++)
            for (int t = 0; t < MUMIMO_NT; t++) {
                /* Wraw = 0.5*I from H=2*I; normalizing each column to
                 * ||.||=sqrt(1/K) forces the single nonzero entry to exactly
                 * sqrt(1/K), independent of the "2" scale factor. */
                cx_t expect = (i == t) ? CX_MAKE(sqrt(1.0 / MUMIMO_K), 0.0) : CX_ZERO;
                if (cabs(W[t][i] - expect) > 1e-9) identity_like_ok = 0;
            }
        CHECK(identity_like_ok, "H=2*I hand-computed case: W = sqrt(1/K)*I exactly");
    }

    /* 4: singular input -- two identical user channels make H*H^H singular. */
    {
        cx_t H[MUMIMO_K][MUMIMO_NT];
        mumimo_channel_draw(H);
        for (int t = 0; t < MUMIMO_NT; t++) H[1 % MUMIMO_K][t] = H[0][t]; /* user 1 := user 0 */
        cx_t W[MUMIMO_NT][MUMIMO_K];
        mumimo_zf_precode(H, W);
        int all_finite = 1, all_zero = 1;
        for (int t = 0; t < MUMIMO_NT; t++)
            for (int k = 0; k < MUMIMO_K; k++) {
                if (!is_finite_cx(W[t][k])) all_finite = 0;
                if (cabs(W[t][k]) > 1e-12) all_zero = 0;
            }
        CHECK(all_finite, "singular Gramian (duplicate user channels): W is finite (no NaN/Inf)");
        CHECK(all_zero, "singular Gramian: W falls back to the documented all-zero vector");
    }

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
