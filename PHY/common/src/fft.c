#include "fft.h"
#include <stdlib.h>
#include <math.h>

void fft_init(FFTCtx *ctx, int N) {
    ctx->N = N;
    ctx->W = (cx_t *)malloc((N / 2) * sizeof(cx_t));
    /* W[k] = e^{-j2πk/N}, k = 0 .. N/2-1
       Used directly for forward DFT, conjugated for inverse. */
    for (int k = 0; k < N / 2; k++) {
        double ang = -2.0 * PHY_PI * k / N;
        ctx->W[k]  = CX_MAKE(cos(ang), sin(ang));
    }
}

void fft_free(FFTCtx *ctx) {
    free(ctx->W);
    ctx->W = NULL;
}

/* Bit-reversal permutation (in-place). */
static void bit_reverse(cx_t *x, int N) {
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { cx_t t = x[i]; x[i] = x[j]; x[j] = t; }
    }
}

/* Butterfly kernel.
   inv = 0 → W[j*step]         (forward, e^{-j2πj/len})
   inv = 1 → conj(W[j*step])   (inverse, e^{+j2πj/len})

   Twiddle lookup: W_len^j = W_N^{j·(N/len)}
   so step = N/len gives the correct stride into the precomputed table. */
static void fft_core(const FFTCtx *ctx, cx_t *x, int inv) {
    int N = ctx->N;
    bit_reverse(x, N);
    for (int len = 2; len <= N; len <<= 1) {
        int step = N / len;
        for (int i = 0; i < N; i += len) {
            for (int j = 0; j < len / 2; j++) {
                cx_t w = inv ? conj(ctx->W[j * step]) : ctx->W[j * step];
                cx_t u = x[i + j];
                cx_t v = x[i + j + len / 2] * w;
                x[i + j]             = u + v;
                x[i + j + len / 2]   = u - v;
            }
        }
    }
}

void fft_forward(const FFTCtx *ctx, cx_t *x) {
    fft_core(ctx, x, 0);
}

void fft_inverse(const FFTCtx *ctx, cx_t *x) {
    fft_core(ctx, x, 1);
    double inv_N = 1.0 / ctx->N;
    for (int i = 0; i < ctx->N; i++) x[i] *= inv_N;
}
