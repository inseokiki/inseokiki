/* test_mimo_detection.c -- permanent numeric regression test for
 * mimo.c's SU-MIMO detectors (MRC/ZF/MMSE, 2x2/4x4/4Rx-overdetermined).
 * PHY_UNIT_VALIDATION_PLAN.md §5 3단계 "추정·검출" 그룹 (2026-09-11),
 * "SU-MIMO 검출" row: "H=I/대각/rank 결손, ZF/MMSE/MRC". (EVD itself is
 * already covered independently by test_matrix.c's herm4x4_eig() checks
 * -- eigen_16port.c/ul_eigen_bf.c's Tx/Rx-side SVD path -- and codebook
 * geometry is deferred, see tasks/todo.md.)
 *
 * Independence notes (UT-06 spirit): noise-free round-trip (y=H*tx,
 * detect(y)==tx) is a property any CORRECT linear detector must satisfy
 * for an invertible H -- it doesn't depend on which detector formula is
 * "right", just on H actually being inverted correctly. The N0->0 MMSE
 * limit is a standard property of the de-biased MMSE formula (matches
 * ZF as the noise term vanishes), and the singular-H fallback check is
 * against mimo.c's own documented contract (x_hat=0, noise_var=N0),
 * not a re-derivation of new correctness.
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

static double randu(void) { return (double)rand() / RAND_MAX; }
static cx_t randc(double scale) { return CX_MAKE((randu() - 0.5) * 2.0 * scale, (randu() - 0.5) * 2.0 * scale); }

static void test_mrc_combine_noise_free(void) {
    srand(11011);
    int ok = 1;
    for (int trial = 0; trial < 30; trial++) {
        cx_t h[2] = { randc(2.0), randc(2.0) };
        cx_t x = randc(3.0);
        cx_t y[2] = { h[0] * x, h[1] * x };
        cx_t x_hat; double noise_var;
        mrc_combine(h, y, 0.1, &x_hat, &noise_var);
        if (cabs(x_hat - x) > 1e-9) ok = 0;
        double expect_nv = 0.1 / (CX_NORM(h[0]) + CX_NORM(h[1]));
        if (fabs(noise_var - expect_nv) > 1e-12) ok = 0;
    }
    CHECK(ok, "mrc_combine: noise-free y=h*x recovers x exactly and noise_var matches N0/sum|h|^2, 30 random trials");

    cx_t h0[2] = { CX_ZERO, CX_ZERO }, y0[2] = { CX_MAKE(5, 5), CX_MAKE(-3, 1) };
    cx_t x_hat0; double nv0;
    mrc_combine(h0, y0, 0.2, &x_hat0, &nv0);
    CHECK(x_hat0 == CX_ZERO && nv0 == 0.2, "mrc_combine: h==[0,0] falls back to x_hat=0,noise_var=N0 (documented near-zero guard)");
}

static void test_mimo_zf_detect_2x2_noise_free(void) {
    srand(12012);
    int ok = 1;
    for (int trial = 0; trial < 30; trial++) {
        cx_t H[2][2];
        double a, b, c, d;
        do {
            H[0][0] = randc(2.0); H[0][1] = randc(2.0);
            H[1][0] = randc(2.0); H[1][1] = randc(2.0);
            a = cabs(H[0][0] * H[1][1] - H[0][1] * H[1][0]);
        } while (a < 0.3);   /* reject near-singular draws */
        (void)b; (void)c; (void)d;
        cx_t tx[2] = { randc(3.0), randc(3.0) };
        cx_t y[2] = { H[0][0] * tx[0] + H[0][1] * tx[1], H[1][0] * tx[0] + H[1][1] * tx[1] };
        cx_t x_hat[2]; double nv[2];
        mimo_zf_detect(H, y, 0.1, x_hat, nv);
        if (cabs(x_hat[0] - tx[0]) > 1e-8 || cabs(x_hat[1] - tx[1]) > 1e-8) ok = 0;
    }
    CHECK(ok, "mimo_zf_detect (2x2): noise-free y=H*tx recovers tx exactly for well-conditioned H, 30 random trials");

    /* Hand-computed: H=[[2,1],[1,1]] (det=1), tx=[3,-1] -> y=[5,2]. */
    cx_t H[2][2] = { { CX_MAKE(2, 0), CX_MAKE(1, 0) }, { CX_MAKE(1, 0), CX_MAKE(1, 0) } };
    cx_t y[2] = { CX_MAKE(5, 0), CX_MAKE(2, 0) };
    cx_t x_hat[2]; double nv[2];
    mimo_zf_detect(H, y, 0.05, x_hat, nv);
    CHECK(cabs(x_hat[0] - CX_MAKE(3, 0)) < 1e-9 && cabs(x_hat[1] - CX_MAKE(-1, 0)) < 1e-9,
          "mimo_zf_detect (2x2): hand-computed H=[[2,1],[1,1]], y=[5,2] -> x_hat=[3,-1] exactly");
}

static void test_mimo_mmse_detect_2x2_zf_limit(void) {
    srand(13013);
    cx_t H[2][2] = { { CX_MAKE(1.7, -0.4), CX_MAKE(0.3, 0.2) }, { CX_MAKE(-0.2, 0.5), CX_MAKE(1.4, 0.1) } };
    cx_t tx[2] = { randc(3.0), randc(3.0) };
    cx_t y[2] = { H[0][0] * tx[0] + H[0][1] * tx[1], H[1][0] * tx[0] + H[1][1] * tx[1] };
    cx_t x_hat[2]; double nv[2];
    mimo_mmse_detect(H, y, 1e-9, x_hat, nv);
    CHECK(cabs(x_hat[0] - tx[0]) < 1e-4 && cabs(x_hat[1] - tx[1]) < 1e-4,
          "mimo_mmse_detect (2x2): N0->0 limit converges to the noise-free tx (de-biased ZF limit)");
}

static void gen_well_conditioned_4x4(cx_t H[4][4]) {
    /* Diagonally-dominant construction: near-guaranteed invertible. */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            H[i][j] = (i == j) ? CX_MAKE(3.0 + randu(), 0.3 * (randu() - 0.5)) : randc(0.4);
}

static void test_mimo_zf_mmse_4x4_noise_free(void) {
    srand(14014);
    int zf_ok = 1, mmse_ok = 1;
    for (int trial = 0; trial < 20; trial++) {
        cx_t H[4][4];
        gen_well_conditioned_4x4(H);
        cx_t tx[4] = { randc(2.0), randc(2.0), randc(2.0), randc(2.0) };
        cx_t y[4];
        for (int r = 0; r < 4; r++) {
            cx_t s = CX_ZERO;
            for (int t = 0; t < 4; t++) s += H[r][t] * tx[t];
            y[r] = s;
        }
        cx_t x_hat_zf[4]; double nv_zf[4];
        mimo_zf_detect_4x4(H, y, 0.1, x_hat_zf, nv_zf);
        for (int t = 0; t < 4; t++) if (cabs(x_hat_zf[t] - tx[t]) > 1e-7) zf_ok = 0;

        cx_t x_hat_mmse[4]; double nv_mmse[4];
        mimo_mmse_detect_4x4(H, y, 1e-9, x_hat_mmse, nv_mmse);
        for (int t = 0; t < 4; t++) if (cabs(x_hat_mmse[t] - tx[t]) > 1e-3) mmse_ok = 0;
    }
    CHECK(zf_ok, "mimo_zf_detect_4x4: noise-free y=H*tx recovers tx exactly for diagonally-dominant H, 20 random trials");
    CHECK(mmse_ok, "mimo_mmse_detect_4x4: N0->0 limit converges to noise-free tx, 20 random trials");
}

static void test_mimo_zf_detect_4x4_singular_fallback(void) {
    cx_t H[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            H[i][j] = (i < 2) ? CX_MAKE(1.0 + j, -0.5 * j) : H[i - 2][j];   /* rows 2,3 duplicate rows 0,1 -> singular */
    cx_t y[4] = { CX_MAKE(1, 1), CX_MAKE(2, -1), CX_MAKE(0, 3), CX_MAKE(-2, 2) };
    cx_t x_hat[4]; double nv[4];
    mimo_zf_detect_4x4(H, y, 0.4, x_hat, nv);
    int ok = 1;
    for (int t = 0; t < 4; t++) if (x_hat[t] != CX_ZERO || nv[t] != 0.4) ok = 0;
    CHECK(ok, "mimo_zf_detect_4x4: singular H (duplicate rows) falls back to x_hat=0,noise_var=N0 for every layer (documented contract)");
}

static void test_mrc_combine_4rx_noise_free(void) {
    srand(15015);
    int ok = 1;
    for (int trial = 0; trial < 30; trial++) {
        cx_t h[4] = { randc(2.0), randc(2.0), randc(2.0), randc(2.0) };
        cx_t x = randc(3.0);
        cx_t y[4]; for (int r = 0; r < 4; r++) y[r] = h[r] * x;
        cx_t x_hat; double nv;
        mrc_combine_4rx(h, y, 0.1, &x_hat, &nv);
        if (cabs(x_hat - x) > 1e-9) ok = 0;
    }
    CHECK(ok, "mrc_combine_4rx: noise-free y=h*x recovers x exactly, 30 random trials");
}

static void gen_well_conditioned_tall(cx_t h_eff[4][3], int layers) {
    for (int r = 0; r < 4; r++)
        for (int l = 0; l < layers; l++)
            h_eff[r][l] = (r == l) ? CX_MAKE(2.5 + randu(), 0.2 * (randu() - 0.5)) : randc(0.5);
}

static void test_mimo_mmse_detect_4rx2_noise_free_zf_limit(void) {
    srand(16016);
    int ok = 1;
    for (int trial = 0; trial < 20; trial++) {
        cx_t h_eff3[4][3];
        gen_well_conditioned_tall(h_eff3, 2);
        cx_t h_eff[4][2];
        for (int r = 0; r < 4; r++) { h_eff[r][0] = h_eff3[r][0]; h_eff[r][1] = h_eff3[r][1]; }
        cx_t tx[2] = { randc(2.0), randc(2.0) };
        cx_t y[4];
        for (int r = 0; r < 4; r++) y[r] = h_eff[r][0] * tx[0] + h_eff[r][1] * tx[1];
        cx_t x_hat[2]; double nv[2];
        mimo_mmse_detect_4rx2(h_eff, y, 1e-9, x_hat, nv);
        if (cabs(x_hat[0] - tx[0]) > 1e-4 || cabs(x_hat[1] - tx[1]) > 1e-4) ok = 0;
    }
    CHECK(ok, "mimo_mmse_detect_4rx2: N0->0 limit converges to noise-free tx (4Rx x 2-layer overdetermined), 20 random trials");
}

static void test_mimo_mmse_detect_4rx3_noise_free_zf_limit(void) {
    srand(17017);
    int ok = 1;
    for (int trial = 0; trial < 20; trial++) {
        cx_t h_eff[4][3];
        gen_well_conditioned_tall(h_eff, 3);
        cx_t tx[3] = { randc(2.0), randc(2.0), randc(2.0) };
        cx_t y[4];
        for (int r = 0; r < 4; r++) {
            cx_t s = CX_ZERO;
            for (int l = 0; l < 3; l++) s += h_eff[r][l] * tx[l];
            y[r] = s;
        }
        cx_t x_hat[3]; double nv[3];
        mimo_mmse_detect_4rx3(h_eff, y, 1e-9, x_hat, nv);
        for (int l = 0; l < 3; l++) if (cabs(x_hat[l] - tx[l]) > 1e-4) ok = 0;
    }
    CHECK(ok, "mimo_mmse_detect_4rx3: N0->0 limit converges to noise-free tx (4Rx x 3-layer overdetermined), 20 random trials");
}

int main(void) {
    test_mrc_combine_noise_free();
    test_mimo_zf_detect_2x2_noise_free();
    test_mimo_mmse_detect_2x2_zf_limit();
    test_mimo_zf_mmse_4x4_noise_free();
    test_mimo_zf_detect_4x4_singular_fallback();
    test_mrc_combine_4rx_noise_free();
    test_mimo_mmse_detect_4rx2_noise_free_zf_limit();
    test_mimo_mmse_detect_4rx3_noise_free_zf_limit();

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
