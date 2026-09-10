/* test_mimo_correlation.c -- permanent numeric regression test for
 * mimo.c's Tx-side spatial correlation (Kronecker model): PHY_UNIT_
 * VALIDATION_PLAN.md, 2026-09-10 §5 2단계 "채널"(channel.c/tdl.c/mimo.c
 * 공간상관) row. channel.c/tdl.c themselves have no spatial-correlation
 * code (grep-confirmed) -- the correlation model lives entirely in
 * mimo.c's mimo_apply_tx_correlation_4x4/4x8/4x32(), so that's the
 * actual scope here.
 *
 * Independence notes (UT-06 spirit): the elementary-input responses
 * below are hand-derived closed forms, not values read off this
 * codebase:
 *   - The N1=2 block a=0.5(sqrt(1+rho)+sqrt(1-rho)), b=0.5(sqrt(1+rho)
 *     -sqrt(1-rho)) satisfies a^2+b^2=1 and 2ab=rho algebraically (see
 *     mimo.h's own doc comment) -- checked directly from a,b implied by
 *     the function's own elementary-input output, an independent
 *     algebraic identity, not a re-statement of the code.
 *   - The N1=4 Cholesky factor of the exponential-correlation matrix
 *     R[i][j]=rho^|i-j| (an AR(1) process covariance) has the
 *     well-known closed form L[i][0]=rho^i, L[i][j]=rho^(i-j)*sqrt(1-
 *     rho^2) for 1<=j<=i (textbook AR(1)/Markov-correlation Cholesky
 *     decomposition, re-derived by hand here, not copied from
 *     chol_exp_corr4() which is `static` and not directly reachable
 *     from a test anyway) -- so feeding a unit-impulse input into a
 *     specific port column and checking the output against this
 *     closed form independently verifies the (otherwise unexported)
 *     Cholesky-based correlation path.
 */
#include "mimo.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

/* ---- 4x4 (N1=2 per-polarization block) ------------------------------- */

static void test_4x4_zero_rho_is_exact_noop(void) {
    cx_t h[4][4], orig[4][4];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++) {
            h[r][t] = CX_MAKE((r + 1) * 0.7 + t, -(t + 1) * 0.3);
            orig[r][t] = h[r][t];
        }
    mimo_apply_tx_correlation_4x4(h, 0.0, 0.0);
    int ok = 1;
    for (int r = 0; r < 4; r++) for (int t = 0; t < 4; t++) if (h[r][t] != orig[r][t]) ok = 0;
    CHECK(ok, "4x4: rho=0, rho_xpol=0 leaves h bit-for-bit unchanged (exact no-op)");

    /* Doc: "rho<=0 ... is a no-op for that factor" -- negative rho too. */
    for (int r = 0; r < 4; r++) for (int t = 0; t < 4; t++) h[r][t] = orig[r][t];
    mimo_apply_tx_correlation_4x4(h, -0.5, -0.5);
    ok = 1;
    for (int r = 0; r < 4; r++) for (int t = 0; t < 4; t++) if (h[r][t] != orig[r][t]) ok = 0;
    CHECK(ok, "4x4: negative rho/rho_xpol is also an exact no-op (both factors gated by `> 0.0`)");
}

/* Feed a single-antenna elementary input (only port 0 = 1, rest 0) into
 * one Rx row and recover the implied (a,b) from the output, then check
 * the algebraic identity a^2+b^2=1, 2ab=rho independently -- this is a
 * property of the CORRECT closed form, not of any particular rho. */
static void test_4x4_rant_algebraic_identity(double rho) {
    cx_t h[4][4];
    for (int r = 0; r < 4; r++) for (int t = 0; t < 4; t++) h[r][t] = CX_ZERO;
    h[0][0] = CX_ONE;   /* row 0: [1,0,0,0] */
    mimo_apply_tx_correlation_4x4(h, rho, 0.0);
    double a = creal(h[0][0]);
    double b = creal(h[0][1]);
    char m[160];
    snprintf(m, sizeof(m), "4x4 R_ant rho=%.3f: implied a=%.6f,b=%.6f satisfy a^2+b^2=1 and 2ab=rho", rho, a, b);
    CHECK(fabs(a * a + b * b - 1.0) < 1e-9 && fabs(2.0 * a * b - rho) < 1e-9, m);
    /* imaginary/other-port parts must stay exactly 0 for this real, rho-only case */
    CHECK(cimag(h[0][0]) == 0.0 && cimag(h[0][1]) == 0.0 && h[0][2] == CX_ZERO && h[0][3] == CX_ZERO,
          "4x4 R_ant: ports 2-3 and imaginary parts untouched by a real rho-only elementary input on ports 0-1");
}

/* Combined R_ant (ports 0-1/2-3) then R_pol (ports 0-2/1-3), applied in
 * that order by the implementation -- hand-derive the fully composed
 * response for input=[1,0,0,0]: after R_ant -> [a,b,0,0], after R_pol
 * (mixes {0,2} and {1,3}) -> [a_x*a, a_x*b, b_x*a, b_x*b]. */
static void test_4x4_combined_hand_derived(double rho, double rho_xpol) {
    double a  = 0.5 * (sqrt(1.0 + rho) + sqrt(1.0 - rho));
    double b  = 0.5 * (sqrt(1.0 + rho) - sqrt(1.0 - rho));
    double ax = 0.5 * (sqrt(1.0 + rho_xpol) + sqrt(1.0 - rho_xpol));
    double bx = 0.5 * (sqrt(1.0 + rho_xpol) - sqrt(1.0 - rho_xpol));
    double expect[4] = { ax * a, ax * b, bx * a, bx * b };

    cx_t h[4][4];
    for (int r = 0; r < 4; r++) for (int t = 0; t < 4; t++) h[r][t] = CX_ZERO;
    h[1][0] = CX_ONE;   /* use row 1 this time, to also confirm rows act independently */
    mimo_apply_tx_correlation_4x4(h, rho, rho_xpol);

    int ok = 1;
    for (int t = 0; t < 4; t++) if (fabs(creal(h[1][t]) - expect[t]) > 1e-9 || fabs(cimag(h[1][t])) > 1e-9) ok = 0;
    char m[160];
    snprintf(m, sizeof(m), "4x4 combined rho=%.2f,rho_xpol=%.2f: matches hand-derived R_ant-then-R_pol composition on elementary input", rho, rho_xpol);
    CHECK(ok, m);
}

static void test_4x4_rho_one_stays_finite(void) {
    cx_t h[4][4];
    for (int r = 0; r < 4; r++) for (int t = 0; t < 4; t++) h[r][t] = CX_MAKE(1.0, -1.0);
    mimo_apply_tx_correlation_4x4(h, 1.0, 1.0);
    int ok = 1;
    for (int r = 0; r < 4; r++) for (int t = 0; t < 4; t++)
        if (!isfinite(creal(h[r][t])) || !isfinite(cimag(h[r][t]))) ok = 0;
    CHECK(ok, "4x4: rho=1.0/rho_xpol=1.0 (clamped internally to 1-1e-9) produces no NaN/Inf");
}

/* ---- 4x8 (N1=4 Cholesky exponential-correlation block) ---------------- */

/* AR(1)/Markov correlation matrix R[i][j]=rho^|i-j| has the well-known
 * closed-form Cholesky factor L[i][0]=rho^i, L[i][j]=rho^(i-j)*
 * sqrt(1-rho^2) for 1<=j<=i -- re-derived by hand (see file header),
 * independent of chol_exp_corr4()'s own (unexported) implementation. */
static double ar1_chol_col0(double rho, int i) { return pow(rho, i); }
static double ar1_chol_coln(double rho, int i, int n) {
    if (n == 0) return ar1_chol_col0(rho, i);
    if (n > i) return 0.0;
    if (n == i) return sqrt(1.0 - rho * rho);
    return pow(rho, i - n) * sqrt(1.0 - rho * rho);
}

static void test_4x8_rant_cholesky_columns(double rho) {
    for (int n = 0; n < 4; n++) {
        cx_t h[4][8];
        for (int r = 0; r < 4; r++) for (int t = 0; t < 8; t++) h[r][t] = CX_ZERO;
        h[2][n] = CX_ONE;   /* elementary input at antenna index n within polarization group 0 */
        mimo_apply_tx_correlation_4x8(h, rho, 0.0);

        int ok = 1;
        for (int i = 0; i < 4; i++) {
            double expect = ar1_chol_coln(rho, i, n);
            if (fabs(creal(h[2][i]) - expect) > 1e-9 || fabs(cimag(h[2][i])) > 1e-9) ok = 0;
        }
        /* polarization group 2 (ports 4-7) must stay exactly 0 */
        for (int i = 4; i < 8; i++) if (h[2][i] != CX_ZERO) ok = 0;
        char m[160];
        snprintf(m, sizeof(m), "4x8 R_ant rho=%.2f, elementary input at antenna %d: output matches AR(1) Cholesky column %d closed form", rho, n, n);
        CHECK(ok, m);
    }
}

static void test_4x8_zero_rho_is_exact_noop(void) {
    cx_t h[4][8], orig[4][8];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 8; t++) { h[r][t] = CX_MAKE(t - 3.5, r * 0.2); orig[r][t] = h[r][t]; }
    mimo_apply_tx_correlation_4x8(h, 0.0, 0.0);
    int ok = 1;
    for (int r = 0; r < 4; r++) for (int t = 0; t < 8; t++) if (h[r][t] != orig[r][t]) ok = 0;
    CHECK(ok, "4x8: rho=0, rho_xpol=0 leaves h bit-for-bit unchanged (exact no-op)");
}

/* R_pol for the 8-port case is the SAME closed-form 2x2 block as the
 * 4x4 case, just repeated per antenna index n1=0..3 (ports {n1, 4+n1}). */
static void test_4x8_rpol_same_block_as_4x4(double rho_xpol) {
    double ax = 0.5 * (sqrt(1.0 + rho_xpol) + sqrt(1.0 - rho_xpol));
    double bx = 0.5 * (sqrt(1.0 + rho_xpol) - sqrt(1.0 - rho_xpol));
    cx_t h[4][8];
    for (int r = 0; r < 4; r++) for (int t = 0; t < 8; t++) h[r][t] = CX_ZERO;
    h[0][2] = CX_ONE;   /* antenna index n1=2, polarization group 0 */
    mimo_apply_tx_correlation_4x8(h, 0.0, rho_xpol);
    int ok = fabs(creal(h[0][2]) - ax) < 1e-9 && fabs(creal(h[0][6]) - bx) < 1e-9;
    for (int t = 0; t < 8; t++) if (t != 2 && t != 6 && h[0][t] != CX_ZERO) ok = 0;
    char m[128];
    snprintf(m, sizeof(m), "4x8 R_pol rho_xpol=%.2f: matches the same closed-form 2x2 block as 4x4, applied at n1=2 (ports 2,6)", rho_xpol);
    CHECK(ok, m);
}

/* ---- 4x32 (N1=4 horiz, N2=4 vert, same Cholesky block reused) --------- */

static void test_4x32_rhoh_reuses_ar1_cholesky(double rho_h) {
    const int N2 = 4;
    /* elementary input at (n1=0,n2=0), polarization group 0 -> index 0 */
    cx_t h[4][32];
    for (int r = 0; r < 4; r++) for (int t = 0; t < 32; t++) h[r][t] = CX_ZERO;
    h[3][0] = CX_ONE;
    mimo_apply_tx_correlation_4x32(h, rho_h, 0.0, 0.0);

    int ok = 1;
    for (int n1 = 0; n1 < 4; n1++) {
        double expect = ar1_chol_coln(rho_h, n1, 0);   /* column 0 of the same AR(1) Cholesky factor */
        int idx = n1 * N2 + 0;
        if (fabs(creal(h[3][idx]) - expect) > 1e-9 || fabs(cimag(h[3][idx])) > 1e-9) ok = 0;
    }
    /* every other (n1,n2) slot (n2!=0) and the second polarization group must stay exactly 0 */
    for (int t = 0; t < 32; t++) {
        int is_expected_slot = 0;
        for (int n1 = 0; n1 < 4; n1++) if (t == n1 * N2 + 0) is_expected_slot = 1;
        if (!is_expected_slot && h[3][t] != CX_ZERO) ok = 0;
    }
    char m[160];
    snprintf(m, sizeof(m), "4x32 R_horiz rho_h=%.2f: reuses the same AR(1) Cholesky closed form along the n1 axis (n2, pol untouched)", rho_h);
    CHECK(ok, m);
}

static void test_4x32_zero_all_is_exact_noop(void) {
    cx_t h[4][32], orig[4][32];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 32; t++) { h[r][t] = CX_MAKE(t * 0.11, r - 1.5); orig[r][t] = h[r][t]; }
    mimo_apply_tx_correlation_4x32(h, 0.0, 0.0, 0.0);
    int ok = 1;
    for (int r = 0; r < 4; r++) for (int t = 0; t < 32; t++) if (h[r][t] != orig[r][t]) ok = 0;
    CHECK(ok, "4x32: rho_h=rho_v=rho_xpol=0 leaves h bit-for-bit unchanged (exact no-op)");
}

int main(void) {
    test_4x4_zero_rho_is_exact_noop();
    test_4x4_rant_algebraic_identity(0.3);
    test_4x4_rant_algebraic_identity(0.7);
    test_4x4_rant_algebraic_identity(0.95);
    test_4x4_combined_hand_derived(0.4, 0.2);
    test_4x4_combined_hand_derived(0.8, 0.6);
    test_4x4_rho_one_stays_finite();

    test_4x8_zero_rho_is_exact_noop();
    test_4x8_rant_cholesky_columns(0.5);
    test_4x8_rant_cholesky_columns(0.85);
    test_4x8_rpol_same_block_as_4x4(0.3);

    test_4x32_zero_all_is_exact_noop();
    test_4x32_rhoh_reuses_ar1_cholesky(0.6);

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
