/* test_channel_estimation.c -- permanent numeric regression test for
 * channel_estimation.c (LS/MMSE/DFT channel estimation, ZF/MMSE
 * equalization). PHY_UNIT_VALIDATION_PLAN.md §5 3단계 "추정·검출" 그룹
 * (2026-09-11), "채널 추정·등화" row: "잡음 없는 알려진 H, pilot/data
 * 인덱스, 정규화, 잡음·경계 fixture".
 *
 * Independence notes (UT-06 spirit): the MMSE-filter checks below use
 * the LMMSE Wiener-filter formula's own well-known LIMITING behavior
 * (N0->0 => identity filter, N0->infinity => zero filter) and a
 * hand-solvable single-pilot/coincident-target special case -- both
 * derivable from the documented formula W=R(R+N0 I)^-1 without
 * reproducing channel_estimation.c's actual matrix-inversion code. The
 * dft_channel_estimate() checks exploit a structural fact (truncating
 * already-zero taps is a no-op; truncating an impulse OUTSIDE the kept
 * window zeroes everything) that holds regardless of the DFT
 * implementation details, using only the already-independently-tested
 * dft_precode.h API (see test_dft_precode.c) as a composition building
 * block, not as "the answer" being checked.
 */
#include "channel_estimation.h"
#include "dft_precode.h"
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

static void test_ls_estimate_exact(void) {
    srand(1001);
    int n = 20;
    cx_t *h = malloc(n * sizeof(cx_t));
    cx_t *tx = malloc(n * sizeof(cx_t));
    cx_t *rx = malloc(n * sizeof(cx_t));
    cx_t *h_out = malloc(n * sizeof(cx_t));
    int all_ok = 1;
    for (int trial = 0; trial < 30; trial++) {
        for (int k = 0; k < n; k++) {
            h[k] = randc(3.0);
            cx_t tv;
            do { tv = randc(2.0); } while (cabs(tv) < 0.1);
            tx[k] = tv;
            rx[k] = h[k] * tx[k];
        }
        ls_estimate(rx, tx, n, h_out);
        for (int k = 0; k < n; k++) if (cabs(h_out[k] - h[k]) > 1e-9) all_ok = 0;
    }
    CHECK(all_ok, "ls_estimate: noise-free rx=h*tx recovers h exactly, 30 random trials");
    free(h); free(tx); free(rx); free(h_out);
}

static void test_interpolate_channel_linear_and_knots(void) {
    int num_active_sc = 48;
    int pilot_pos[] = { 4, 12, 20, 28, 36, 44 };
    int num_pilots = (int)(sizeof(pilot_pos) / sizeof(pilot_pos[0]));

    cx_t A = CX_MAKE(1.5, -0.7), B = CX_MAKE(0.03, 0.05);
    cx_t *h_pilots = malloc(num_pilots * sizeof(cx_t));
    for (int i = 0; i < num_pilots; i++) h_pilots[i] = A + B * (double)pilot_pos[i];

    cx_t *h_out = malloc(num_active_sc * sizeof(cx_t));
    interpolate_channel(h_pilots, num_pilots, pilot_pos, num_active_sc, h_out);

    int knot_ok = 1;
    for (int i = 0; i < num_pilots; i++)
        if (cabs(h_out[pilot_pos[i]] - h_pilots[i]) > 1e-12) knot_ok = 0;
    CHECK(knot_ok, "interpolate_channel: exact pass-through at every pilot position");

    int interior_ok = 1;
    for (int k = pilot_pos[0]; k <= pilot_pos[num_pilots - 1]; k++) {
        cx_t expect = A + B * (double)k;   /* linear interpolation of a linear function is exact */
        if (cabs(h_out[k] - expect) > 1e-9) interior_ok = 0;
    }
    CHECK(interior_ok, "interpolate_channel: exactly reproduces a linear channel at every interior subcarrier (linear interp of a linear function)");

    int edge_ok = 1;
    for (int k = 0; k < pilot_pos[0]; k++) if (cabs(h_out[k] - h_pilots[0]) > 1e-12) edge_ok = 0;
    for (int k = pilot_pos[num_pilots - 1] + 1; k < num_active_sc; k++)
        if (cabs(h_out[k] - h_pilots[num_pilots - 1]) > 1e-12) edge_ok = 0;
    CHECK(edge_ok, "interpolate_channel: extrapolates flat (nearest-pilot hold) outside the [first,last] pilot range");

    free(h_pilots); free(h_out);
}

static void test_mmse_filter_build_apply_equivalence(void) {
    int pilot_pos[] = { 0, 3, 7, 15, 22, 30 };
    int M = (int)(sizeof(pilot_pos) / sizeof(pilot_pos[0]));
    double delay_spread_ns = 200.0, scs_hz = 30e3, N0 = 0.05;

    srand(2002);
    cx_t *h_ls = malloc(M * sizeof(cx_t));
    for (int i = 0; i < M; i++) h_ls[i] = randc(2.0);

    cx_t *h_direct = malloc(M * sizeof(cx_t));
    mmse_channel_estimate(h_ls, pilot_pos, M, delay_spread_ns, scs_hz, N0, h_direct);

    cx_t *W = malloc((size_t)M * M * sizeof(cx_t));
    mmse_build_filter(pilot_pos, M, delay_spread_ns, scs_hz, N0, W);
    cx_t *h_split = malloc(M * sizeof(cx_t));
    mmse_apply_filter(W, M, h_ls, h_split);

    int ok = 1;
    for (int i = 0; i < M; i++) if (cabs(h_direct[i] - h_split[i]) > 1e-12) ok = 0;
    CHECK(ok, "mmse_channel_estimate == mmse_build_filter+mmse_apply_filter exactly (the monolithic call documented as wrapping the split one)");

    free(h_ls); free(h_direct); free(W); free(h_split);
}

/* Wiener filter W=R(R+N0*I)^-1's own well-known limiting behavior:
 * N0->0 => W->I (LS passthrough, no smoothing); N0->huge => W->0
 * (full suppression). Both hold for ANY invertible R, independent of
 * this module's own matrix-inversion code. */
static void test_mmse_wiener_limits(void) {
    int pilot_pos[] = { 0, 5, 11, 19, 26 };
    int M = (int)(sizeof(pilot_pos) / sizeof(pilot_pos[0]));
    double delay_spread_ns = 150.0, scs_hz = 30e3;

    srand(3003);
    cx_t *h_ls = malloc(M * sizeof(cx_t));
    for (int i = 0; i < M; i++) h_ls[i] = randc(2.0);

    /* R (correlation among these closely-spaced, delay-spread-correlated
     * pilots) is itself ill-conditioned, so (R+N0*I)^-1 at N0=1e-10 only
     * approaches R^-1 to within floating-point conditioning error, not
     * machine epsilon -- empirically ~1.5e-5 max residual for this
     * config (verified via a standalone sweep over N0=1e-10..1e-2, which
     * confirms the residual monotonically INCREASES with N0 as expected,
     * not that the limit itself is wrong). Tolerance set accordingly. */
    cx_t *h_tiny = malloc(M * sizeof(cx_t));
    mmse_channel_estimate(h_ls, pilot_pos, M, delay_spread_ns, scs_hz, 1e-10, h_tiny);
    int tiny_ok = 1;
    for (int i = 0; i < M; i++) if (cabs(h_tiny[i] - h_ls[i]) > 1e-4) tiny_ok = 0;
    CHECK(tiny_ok, "mmse_channel_estimate: N0->0 limit matches h_ls (Wiener filter -> identity)");

    cx_t *h_huge = malloc(M * sizeof(cx_t));
    mmse_channel_estimate(h_ls, pilot_pos, M, delay_spread_ns, scs_hz, 1e10, h_huge);
    double max_huge = 0.0;
    for (int i = 0; i < M; i++) if (cabs(h_huge[i]) > max_huge) max_huge = cabs(h_huge[i]);
    char m[128];
    snprintf(m, sizeof(m), "mmse_channel_estimate: N0->huge limit suppresses output near 0 (Wiener filter -> zero), max|h_mmse|=%.3e", max_huge);
    CHECK(max_huge < 1e-6, m);

    free(h_ls); free(h_tiny); free(h_huge);
}

/* Hand-solvable special case: a single pilot (M=1) and a single target
 * (D=1) coincident with that pilot (target_pos==pilot_pos). Delta-f=0
 * everywhere, so rho(0)=1/(1+0)=1 exactly (the documented formula's own
 * value at zero frequency offset): R=[1], A=[1+N0], so
 * w_avg = rho(0)/(1+N0) = 1/(1+N0) exactly -- a plain scalar Wiener
 * gain, independent of the general M-pilot matrix code path. */
static void test_mmse_avg_filter_single_pilot_coincident(void) {
    int pilot_pos[1] = { 7 };
    int target_pos[1] = { 7 };
    double delay_spread_ns = 300.0, scs_hz = 15e3;
    int ok = 1;
    double N0s[] = { 0.01, 0.1, 1.0, 5.0 };
    for (size_t i = 0; i < sizeof(N0s) / sizeof(N0s[0]); i++) {
        double N0 = N0s[i];
        cx_t w_avg;
        mmse_build_avg_filter(pilot_pos, 1, target_pos, 1, delay_spread_ns, scs_hz, N0, &w_avg);
        double expect = 1.0 / (1.0 + N0);
        if (cabs(w_avg - CX_MAKE(expect, 0.0)) > 1e-9) ok = 0;
    }
    CHECK(ok, "mmse_build_avg_filter: single pilot, target coincident with it -> w_avg == 1/(1+N0) exactly (hand-solved scalar Wiener gain)");
}

/* dft_channel_estimate() truncates the time-domain response to the
 * first num_taps samples. Two structural, implementation-independent
 * consequences: (a) if the time-domain support is ALREADY within
 * num_taps, truncation is a no-op -> output == input exactly (idft
 * then dft round-trip, see test_dft_precode.c); (b) if the time-domain
 * support lies ENTIRELY outside num_taps, truncation zeroes everything
 * -> output is exactly the zero vector. */
static void test_dft_channel_estimate_support_truncation(void) {
    int num_sc = 64, num_taps = 8;
    srand(4004);

    cx_t *h_time_in = calloc(num_sc, sizeof(cx_t));
    for (int i = 0; i < num_taps; i++) h_time_in[i] = randc(2.0);   /* support already within num_taps */
    cx_t *h_freq_in = malloc(num_sc * sizeof(cx_t));
    dft_precode(h_time_in, num_sc, h_freq_in);

    cx_t *h_freq_out = malloc(num_sc * sizeof(cx_t));
    dft_channel_estimate(h_freq_in, num_sc, num_taps, h_freq_out);
    int passthrough_ok = 1;
    for (int i = 0; i < num_sc; i++) if (cabs(h_freq_out[i] - h_freq_in[i]) > 1e-9) passthrough_ok = 0;
    CHECK(passthrough_ok, "dft_channel_estimate: time-domain support already within num_taps -> output == input exactly (truncation is a no-op)");

    cx_t *h_time_out = calloc(num_sc, sizeof(cx_t));
    h_time_out[num_taps + 3] = CX_MAKE(5.0, -2.0);   /* impulse strictly outside the kept window */
    cx_t *h_freq_in2 = malloc(num_sc * sizeof(cx_t));
    dft_precode(h_time_out, num_sc, h_freq_in2);
    cx_t *h_freq_out2 = malloc(num_sc * sizeof(cx_t));
    dft_channel_estimate(h_freq_in2, num_sc, num_taps, h_freq_out2);
    int zero_ok = 1;
    for (int i = 0; i < num_sc; i++) if (cabs(h_freq_out2[i]) > 1e-9) zero_ok = 0;
    CHECK(zero_ok, "dft_channel_estimate: time-domain support entirely outside num_taps -> output is exactly zero everywhere");

    free(h_time_in); free(h_freq_in); free(h_freq_out);
    free(h_time_out); free(h_freq_in2); free(h_freq_out2);
}

static void test_zf_equalize(void) {
    srand(5005);
    int n = 20;
    cx_t *rx = malloc(n * sizeof(cx_t));
    cx_t *h = malloc(n * sizeof(cx_t));
    cx_t *eq = malloc(n * sizeof(cx_t));
    for (int k = 0; k < n; k++) { rx[k] = randc(3.0); h[k] = randc(2.0); }
    zf_equalize(rx, h, n, eq);
    int ok = 1;
    for (int k = 0; k < n; k++) if (cabs(eq[k] - rx[k] / h[k]) > 1e-12) ok = 0;
    CHECK(ok, "zf_equalize: eq == rx/h exactly for nonzero h, 20 random subcarriers");

    cx_t rx0 = CX_MAKE(3.0, -1.0), h0 = CX_ZERO, eq0;
    zf_equalize(&rx0, &h0, 1, &eq0);
    CHECK(cabs(eq0 - rx0) < 1e-12, "zf_equalize: h==0 falls back to eq==rx exactly (documented near-zero guard, avoids div-by-zero)");

    free(rx); free(h); free(eq);
}

static void test_mmse_equalize(void) {
    srand(6006);
    int n = 20;
    cx_t *rx = malloc(n * sizeof(cx_t));
    cx_t *h = malloc(n * sizeof(cx_t));
    cx_t *eq = malloc(n * sizeof(cx_t));
    double *alpha = malloc(n * sizeof(double));
    double N0 = 0.3;
    for (int k = 0; k < n; k++) { rx[k] = randc(3.0); h[k] = randc(2.0); }
    mmse_equalize(rx, h, n, N0, eq, alpha);
    int ok = 1;
    for (int k = 0; k < n; k++) {
        double hp = CX_NORM(h[k]);
        cx_t w = conj(h[k]) / (hp + N0);
        cx_t expect_eq = w * rx[k];
        double expect_alpha = hp / (hp + N0);
        if (cabs(eq[k] - expect_eq) > 1e-12 || fabs(alpha[k] - expect_alpha) > 1e-12) ok = 0;
    }
    CHECK(ok, "mmse_equalize: eq/alpha match the independently-recomputed w=h*/(|h|^2+N0) formula exactly, 20 random subcarriers");

    /* N0->0: MMSE converges to the ZF solution and alpha->1 (de-biasing gain vanishes). */
    cx_t h1 = CX_MAKE(1.3, -0.6), rx1 = CX_MAKE(2.1, 0.4), eq_tiny;
    double alpha_tiny;
    mmse_equalize(&rx1, &h1, 1, 1e-12, &eq_tiny, &alpha_tiny);
    cx_t zf_expect = rx1 / h1;
    CHECK(cabs(eq_tiny - zf_expect) < 1e-6 && fabs(alpha_tiny - 1.0) < 1e-6,
          "mmse_equalize: N0->0 limit matches zf_equalize's result and alpha->1");

    free(rx); free(h); free(eq); free(alpha);
}

int main(void) {
    test_ls_estimate_exact();
    test_interpolate_channel_linear_and_knots();
    test_mmse_filter_build_apply_equivalence();
    test_mmse_wiener_limits();
    test_mmse_avg_filter_single_pilot_coincident();
    test_dft_channel_estimate_support_truncation();
    test_zf_equalize();
    test_mmse_equalize();

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
