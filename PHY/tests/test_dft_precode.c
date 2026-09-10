/* test_dft_precode.c -- permanent numeric regression test for
 * dft_precode.c's unitary M-point DFT/IDFT (PUSCH transform precoding,
 * TS 38.211 Sec 6.3.1.4). lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10
 * "DFT/IDFT precoding" row: "M=1/2/4 손계산, 길이 경계, Parseval, 왕복".
 *
 * Independence notes (UT-06 spirit): the M=2/M=4 expected matrices below
 * are derived by hand directly from the unitary-DFT definition
 * W[k][n] = exp(-j2*pi*k*n/M)/sqrt(M) and plugged into a concrete numeric
 * example independently of dft_precode.c's O(M^2) summation loop -- this
 * catches sign/index errors in that loop that a self-consistent
 * round-trip check alone would not (a consistently-wrong transform still
 * round-trips with its own inverse). Parseval (unitary => energy
 * preserved) is checked per-direction, independent of round-trip too.
 * M=3/5/6 (non-power-of-2, the case this direct-sum implementation
 * exists FOR, since 3GPP restricts M to products of {2,3,5} and this LLS
 * has no FFTW dependency) get impulse/all-ones checks, same as ofdm.c's
 * radix2_fft tests but for the different M^2 code path.
 */
#include "dft_precode.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

static double max_abs_err(const cx_t *a, const cx_t *b, int n) {
    double m = 0.0;
    for (int i = 0; i < n; i++) { double d = cabs(a[i] - b[i]); if (d > m) m = d; }
    return m;
}

static void test_M1_trivial(void) {
    cx_t x[1] = { CX_MAKE(3.5, -2.0) };
    cx_t y[1], z[1];
    dft_precode(x, 1, y);
    CHECK(cabs(y[0] - x[0]) < 1e-12, "M=1: dft_precode is the identity (1/sqrt(1)=1, single term, zero phase)");
    idft_precode(y, 1, z);
    CHECK(cabs(z[0] - x[0]) < 1e-12, "M=1: idft_precode is the identity");
}

/* Hand-derived from W[k][n]=exp(-j2pi*kn/2)/sqrt(2): W = (1/sqrt2)*[[1,1],[1,-1]].
 * dft_precode([a,b]) = [(a+b)/sqrt2, (a-b)/sqrt2]. */
static void test_M2_hand_computed(void) {
    cx_t x[2] = { CX_MAKE(5.0, 1.0), CX_MAKE(3.0, -2.0) };
    cx_t out[2];
    dft_precode(x, 2, out);
    double inv_sqrt2 = 1.0 / sqrt(2.0);
    cx_t expect[2] = { (x[0] + x[1]) * inv_sqrt2, (x[0] - x[1]) * inv_sqrt2 };
    CHECK(max_abs_err(out, expect, 2) < 1e-9,
          "M=2: dft_precode matches hand-derived [(a+b)/sqrt2, (a-b)/sqrt2]");

    cx_t back[2];
    idft_precode(out, 2, back);
    /* IDFT matrix for M=2 is the same real matrix (since exp(+/-j*pi)=-1
     * either way) -- so idft_precode([c,d]) = [(c+d)/sqrt2, (c-d)/sqrt2]. */
    cx_t expect_back[2] = { (out[0] + out[1]) * inv_sqrt2, (out[0] - out[1]) * inv_sqrt2 };
    CHECK(max_abs_err(back, expect_back, 2) < 1e-9,
          "M=2: idft_precode matches hand-derived formula (self-inverse structure for M=2)");
    CHECK(max_abs_err(back, x, 2) < 1e-9, "M=2: idft_precode(dft_precode(x)) == x");
}

/* Hand-derived from W[k][n]=exp(-j*pi*kn/2)/2 for M=4:
 *   row0=[1,1,1,1]/2, row1=[1,-j,-1,j]/2, row2=[1,-1,1,-1]/2, row3=[1,j,-1,-j]/2
 * Applied to x=[1,2,3,4] (real, chosen so hand arithmetic is exact):
 *   out0=(1+2+3+4)/2=5, out1=(1-2j-3+4j)/2=-1+j, out2=(1-2+3-4)/2=-1, out3=(1+2j-3-4j)/2=-1-j */
static void test_M4_hand_computed(void) {
    cx_t x[4] = { CX_MAKE(1,0), CX_MAKE(2,0), CX_MAKE(3,0), CX_MAKE(4,0) };
    cx_t out[4];
    dft_precode(x, 4, out);
    cx_t expect[4] = { CX_MAKE(5,0), CX_MAKE(-1,1), CX_MAKE(-1,0), CX_MAKE(-1,-1) };
    CHECK(max_abs_err(out, expect, 4) < 1e-9,
          "M=4: dft_precode([1,2,3,4]) matches hand-derived [5, -1+j, -1, -1-j]");

    cx_t back[4];
    idft_precode(out, 4, back);
    CHECK(max_abs_err(back, x, 4) < 1e-9, "M=4: idft_precode(dft_precode(x)) == x");
}

/* dft_precode of a unit impulse must give a constant 1/sqrt(M) at every
 * output bin (phase term is exp(0)=1 for all k when n=0 is the only
 * nonzero input); idft_precode of a unit impulse at k=0 likewise gives a
 * constant 1/sqrt(M). Checked for non-power-of-2 M too (3,5,6). */
static void test_impulse_response(int M) {
    cx_t *x = calloc(M, sizeof(cx_t));
    x[0] = CX_ONE;
    cx_t *out = malloc(M * sizeof(cx_t));
    dft_precode(x, M, out);
    double expect_amp = 1.0 / sqrt((double)M);
    int ok = 1;
    for (int i = 0; i < M; i++) if (cabs(out[i] - CX_MAKE(expect_amp, 0)) > 1e-9) ok = 0;
    char m[96]; snprintf(m, sizeof(m), "M=%d: dft_precode(unit impulse) == constant 1/sqrt(M)=%.6f", M, expect_amp);
    CHECK(ok, m);

    cx_t *out2 = malloc(M * sizeof(cx_t));
    idft_precode(x, M, out2);
    ok = 1;
    for (int i = 0; i < M; i++) if (cabs(out2[i] - CX_MAKE(expect_amp, 0)) > 1e-9) ok = 0;
    snprintf(m, sizeof(m), "M=%d: idft_precode(unit impulse) == constant 1/sqrt(M)=%.6f", M, expect_amp);
    CHECK(ok, m);
    free(x); free(out); free(out2);
}

/* dft_precode of an all-ones vector: sum_n 1*exp(-j2pi kn/M)/sqrt(M) is a
 * geometric series that is sqrt(M) at k=0 and exactly 0 for every other
 * k (orthogonality of the M-th roots of unity), for ANY M including
 * non-power-of-2. */
static void test_allones_response(int M) {
    cx_t *x = malloc(M * sizeof(cx_t));
    for (int i = 0; i < M; i++) x[i] = CX_ONE;
    cx_t *out = malloc(M * sizeof(cx_t));
    dft_precode(x, M, out);
    double sqrtM = sqrt((double)M);
    int ok = 1;
    for (int k = 0; k < M; k++) {
        cx_t expect = (k == 0) ? CX_MAKE(sqrtM, 0) : CX_ZERO;
        if (cabs(out[k] - expect) > 1e-8) ok = 0;
    }
    char m[96]; snprintf(m, sizeof(m), "M=%d: dft_precode(all-ones) == sqrt(M)*delta[k] (root-of-unity orthogonality)", M);
    CHECK(ok, m);
    free(x); free(out);
}

/* Parseval, checked independently per direction (unitary transform =>
 * sum|out|^2 == sum|in|^2 exactly, no 1/M factor since BOTH directions
 * are scaled by 1/sqrt(M) here, unlike ofdm.c's IDFT-only 1/N convention). */
static void test_parseval(int M) {
    cx_t *x = malloc(M * sizeof(cx_t));
    srand(2222 + M);
    for (int i = 0; i < M; i++) x[i] = CX_MAKE((rand() % 21) - 10, (rand() % 21) - 10);
    double in_energy = 0.0;
    for (int i = 0; i < M; i++) in_energy += CX_NORM(x[i]);

    cx_t *fwd = malloc(M * sizeof(cx_t));
    dft_precode(x, M, fwd);
    double fwd_energy = 0.0;
    for (int i = 0; i < M; i++) fwd_energy += CX_NORM(fwd[i]);
    char m[128];
    snprintf(m, sizeof(m), "M=%d: Parseval holds for dft_precode (unitary): sum|out|^2 == sum|in|^2 (%.4f vs %.4f)", M, fwd_energy, in_energy);
    CHECK(fabs(fwd_energy - in_energy) < 1e-6, m);

    cx_t *inv = malloc(M * sizeof(cx_t));
    idft_precode(x, M, inv);
    double inv_energy = 0.0;
    for (int i = 0; i < M; i++) inv_energy += CX_NORM(inv[i]);
    snprintf(m, sizeof(m), "M=%d: Parseval holds for idft_precode (unitary): sum|out|^2 == sum|in|^2 (%.4f vs %.4f)", M, inv_energy, in_energy);
    CHECK(fabs(inv_energy - in_energy) < 1e-6, m);

    free(x); free(fwd); free(inv);
}

static void test_roundtrip(int M) {
    cx_t *x = malloc(M * sizeof(cx_t));
    srand(9999 + M);
    for (int i = 0; i < M; i++) x[i] = CX_MAKE((rand() % 21) - 10, (rand() % 21) - 10);
    cx_t *fwd = malloc(M * sizeof(cx_t));
    cx_t *back = malloc(M * sizeof(cx_t));
    dft_precode(x, M, fwd);
    idft_precode(fwd, M, back);
    char m[96]; snprintf(m, sizeof(m), "M=%d: idft_precode(dft_precode(x)) == x exactly", M);
    CHECK(max_abs_err(x, back, M) < 1e-9, m);
    free(x); free(fwd); free(back);
}

int main(void) {
    test_M1_trivial();
    test_M2_hand_computed();
    test_M4_hand_computed();

    int Ms[] = { 1, 2, 3, 4, 5, 6, 12, 15, 24, 25, 48 };
    int nM = (int)(sizeof(Ms) / sizeof(Ms[0]));
    for (int i = 0; i < nM; i++) {
        test_impulse_response(Ms[i]);
        test_allones_response(Ms[i]);
        test_parseval(Ms[i]);
        test_roundtrip(Ms[i]);
    }

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
