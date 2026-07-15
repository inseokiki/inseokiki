/* ================================================================
 *  ofdm.c
 *  OFDM modulation/demodulation (FFT/IFFT)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "ofdm.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void ofdm_init(OFDMCtx *ctx, int nfft, int cp_len) {
    ctx->nfft   = nfft;
    ctx->cp_len = cp_len;
}

/* In-place Radix-2 Cooley-Tukey FFT.
   sign = -1: forward, sign = +1: inverse (before 1/N scaling) */
void radix2_fft(cx_t *x, int N, int sign) {
    /* bit-reversal permutation */
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { cx_t tmp = x[i]; x[i] = x[j]; x[j] = tmp; }
    }
    /* butterfly stages */
    for (int len = 2; len <= N; len <<= 1) {
        double angle = (double)sign * 2.0 * PHY_PI / len;
        cx_t wlen = CX_MAKE(cos(angle), sin(angle));
        for (int i = 0; i < N; i += len) {
            cx_t w = CX_ONE;
            for (int j = 0; j < len / 2; j++) {
                cx_t u = x[i + j];
                cx_t v = x[i + j + len/2] * w;
                x[i + j]          = u + v;
                x[i + j + len/2]  = u - v;
                w *= wlen;
            }
        }
    }
}

void ofdm_modulate(const OFDMCtx *ctx, const cx_t *freq, cx_t *time_out) {
    int N = ctx->nfft, cp = ctx->cp_len;
    cx_t *tmp = (cx_t *)malloc(N * sizeof(cx_t));
    memcpy(tmp, freq, N * sizeof(cx_t));
    radix2_fft(tmp, N, +1);           /* IFFT: sign=+1 */
    double inv = 1.0 / N;
    for (int i = 0; i < N; i++) tmp[i] *= inv;
    /* prepend CP */
    for (int i = 0; i < cp; i++)  time_out[i]      = tmp[N - cp + i];
    for (int i = 0; i < N; i++)   time_out[cp + i] = tmp[i];
    free(tmp);
}

void ofdm_demodulate(const OFDMCtx *ctx, const cx_t *time_in, cx_t *freq_out) {
    int N = ctx->nfft, cp = ctx->cp_len;
    cx_t *tmp = (cx_t *)malloc(N * sizeof(cx_t));
    for (int i = 0; i < N; i++) tmp[i] = time_in[cp + i];
    radix2_fft(tmp, N, -1);           /* FFT: sign=-1 */
    memcpy(freq_out, tmp, N * sizeof(cx_t));
    free(tmp);
}
