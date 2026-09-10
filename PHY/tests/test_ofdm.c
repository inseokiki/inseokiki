/* test_ofdm.c -- permanent numeric regression test for ofdm.c's radix-2
 * FFT/IFFT and CP handling (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10
 * "OFDM·FFT/IFFT" row: "impulse/tone, scaling, CP 삽입·제거, 지원 길이,
 * 독립 변환과 비교").
 *
 * Independence notes (UT-06 spirit): the impulse/tone/small-N cases
 * below are closed-form DFT identities derivable directly from the
 * definition X[k]=sum_n x[n]*exp(-j2*pi*k*n/N) (forward) and
 * x[n]=(1/N)*sum_k X[k]*exp(+j2*pi*k*n/N) (inverse) -- these are
 * textbook Fourier-transform properties, not values read off this
 * codebase's own implementation, so they're a genuine independent
 * check of radix2_fft()'s correctness (both directions, and the
 * ofdm_modulate()/ofdm_demodulate() scaling convention).
 */
#include "ofdm.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

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

/* Forward FFT (sign=-1) of a unit impulse x=[1,0,0,...] must give the
 * all-ones vector: X[k] = sum_n x[n]e^{-j2pi kn/N} = x[0] = 1 for every k. */
static void test_forward_impulse(int N) {
    cx_t *x = malloc(N * sizeof(cx_t));
    cx_t *expect = malloc(N * sizeof(cx_t));
    for (int i = 0; i < N; i++) { x[i] = (i == 0) ? CX_ONE : CX_ZERO; expect[i] = CX_ONE; }
    radix2_fft(x, N, -1);
    char m[96]; snprintf(m, sizeof(m), "radix2_fft(sign=-1) of a unit impulse == all-ones, N=%d", N);
    CHECK(max_abs_err(x, expect, N) < 1e-9, m);
    free(x); free(expect);
}

/* Inverse FFT (sign=+1, no scaling applied by radix2_fft itself -- that's
 * ofdm_modulate()'s job) of the all-ones vector must give N at index 0,
 * 0 elsewhere: y[n] = sum_k 1*e^{+j2pi kn/N} = N if n=0 else 0
 * (geometric series / orthogonality of roots of unity). */
static void test_inverse_of_allones(int N) {
    cx_t *x = malloc(N * sizeof(cx_t));
    cx_t *expect = malloc(N * sizeof(cx_t));
    for (int i = 0; i < N; i++) { x[i] = CX_ONE; expect[i] = (i == 0) ? CX_MAKE(N, 0) : CX_ZERO; }
    radix2_fft(x, N, +1);
    char m[96]; snprintf(m, sizeof(m), "radix2_fft(sign=+1) of all-ones == [N,0,0,...], N=%d", N);
    CHECK(max_abs_err(x, expect, N) < 1e-9, m);
    free(x); free(expect);
}

/* Forward FFT of a single-frequency complex tone x[n]=exp(+j2*pi*f*n/N)
 * must give a single spike of height N at bin f (orthogonality), zero
 * elsewhere -- tests non-trivial complex exponential handling, not just
 * the all-real impulse/constant cases above. */
static void test_forward_tone(int N, int f) {
    cx_t *x = malloc(N * sizeof(cx_t));
    cx_t *expect = malloc(N * sizeof(cx_t));
    for (int n = 0; n < N; n++) {
        double ang = 2.0 * PHY_PI * f * n / N;
        x[n] = CX_MAKE(cos(ang), sin(ang));
        expect[n] = (n == f) ? CX_MAKE(N, 0) : CX_ZERO;
    }
    radix2_fft(x, N, -1);
    char m[96]; snprintf(m, sizeof(m), "radix2_fft(sign=-1) of tone e^{j2pi*%d*n/%d} == N*delta[k-%d]", f, N, f);
    CHECK(max_abs_err(x, expect, N) < 1e-8, m);
    free(x); free(expect);
}

/* OFDM-level (through the CP wrapper + 1/N scaling): a frequency-domain
 * impulse at bin 0 must produce a constant time-domain signal of
 * amplitude exactly 1/N (both in the CP and the core N samples, since a
 * DC signal's cyclic prefix is identical to its tail by construction). */
static void test_ofdm_freq_impulse_gives_constant_time(int nfft, int cp) {
    OFDMCtx ctx; ofdm_init(&ctx, nfft, cp);
    cx_t *freq = calloc(nfft, sizeof(cx_t));
    freq[0] = CX_ONE;
    cx_t *time_out = malloc((nfft + cp) * sizeof(cx_t));
    ofdm_modulate(&ctx, freq, time_out);

    double expect_amp = 1.0 / nfft;
    int ok = 1;
    for (int i = 0; i < nfft + cp; i++)
        if (cabs(time_out[i] - CX_MAKE(expect_amp, 0)) > 1e-9) ok = 0;
    char m[128];
    snprintf(m, sizeof(m), "ofdm_modulate: freq-domain impulse at bin 0 -> constant time signal of amplitude 1/N=%.6f (incl. CP)", expect_amp);
    CHECK(ok, m);
    free(freq); free(time_out);
}

/* CP structure: the prepended cyclic prefix must be an exact copy of the
 * LAST cp_len samples of the core N-sample symbol -- checked directly on
 * the output buffer layout, independent of what the symbol's content is. */
static void test_cp_is_exact_tail_copy(int nfft, int cp) {
    OFDMCtx ctx; ofdm_init(&ctx, nfft, cp);
    cx_t *freq = malloc(nfft * sizeof(cx_t));
    srand(321);
    for (int i = 0; i < nfft; i++) freq[i] = CX_MAKE((rand() % 21) - 10, (rand() % 21) - 10);
    cx_t *time_out = malloc((nfft + cp) * sizeof(cx_t));
    ofdm_modulate(&ctx, freq, time_out);

    int ok = 1;
    for (int i = 0; i < cp; i++)
        if (cabs(time_out[i] - time_out[cp + nfft - cp + i]) > 1e-9) ok = 0;
    CHECK(ok, "ofdm_modulate: prepended CP is an exact copy of the core symbol's last cp_len samples");
    free(freq); free(time_out);
}

/* Parseval / energy conservation for this scaling convention
 * (IDFT has 1/N, DFT has no prefactor): sum_n|x[n]|^2 == (1/N)*sum_k|X[k]|^2.
 * This is a structural identity independent of any particular input. */
static void test_parseval(int nfft) {
    OFDMCtx ctx; ofdm_init(&ctx, nfft, 0);
    cx_t *freq = malloc(nfft * sizeof(cx_t));
    srand(654);
    for (int i = 0; i < nfft; i++) freq[i] = CX_MAKE((rand() % 21) - 10, (rand() % 21) - 10);
    double freq_energy = 0.0;
    for (int i = 0; i < nfft; i++) freq_energy += CX_NORM(freq[i]);

    cx_t *time_out = malloc(nfft * sizeof(cx_t));
    ofdm_modulate(&ctx, freq, time_out);
    double time_energy = 0.0;
    for (int i = 0; i < nfft; i++) time_energy += CX_NORM(time_out[i]);

    char m[128];
    snprintf(m, sizeof(m), "Parseval: sum|time|^2 == (1/N)*sum|freq|^2, N=%d (time=%.4f, expect=%.4f)",
             nfft, time_energy, freq_energy / nfft);
    CHECK(fabs(time_energy - freq_energy / nfft) < 1e-6, m);
    free(freq); free(time_out);
}

static void test_roundtrip(int nfft, int cp) {
    OFDMCtx ctx; ofdm_init(&ctx, nfft, cp);
    cx_t *freq = malloc(nfft * sizeof(cx_t));
    srand(111);
    for (int i = 0; i < nfft; i++) freq[i] = CX_MAKE((rand() % 21) - 10, (rand() % 21) - 10);
    cx_t *time_out = malloc((nfft + cp) * sizeof(cx_t));
    cx_t *freq_back = malloc(nfft * sizeof(cx_t));
    ofdm_modulate(&ctx, freq, time_out);
    ofdm_demodulate(&ctx, time_out, freq_back);
    char m[96]; snprintf(m, sizeof(m), "ofdm_demodulate(ofdm_modulate(x)) == x exactly, N=%d, cp=%d", nfft, cp);
    CHECK(max_abs_err(freq, freq_back, nfft) < 1e-9, m);
    free(freq); free(time_out); free(freq_back);
}

int main(void) {
    for (int N = 4; N <= 64; N *= 4) {
        test_forward_impulse(N);
        test_inverse_of_allones(N);
    }
    test_forward_tone(8, 1);
    test_forward_tone(8, 3);
    test_forward_tone(64, 17);

    test_ofdm_freq_impulse_gives_constant_time(1024, 72);
    test_cp_is_exact_tail_copy(1024, 72);
    test_parseval(1024);
    test_roundtrip(1024, 72);
    test_roundtrip(4096, 288);

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
