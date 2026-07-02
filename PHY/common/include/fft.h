#ifndef FFT_H
#define FFT_H

#include "utils.h"

/* Radix-2 Cooley-Tukey FFT.
   N must be a power of 2.
   Twiddle factors W[k] = e^{-j2πk/N}, k=0..N/2-1, are precomputed once at
   fft_init() to avoid repeated cos/sin and floating-point drift from
   iterative twiddle multiplication (w *= wlen). */
typedef struct {
    int   N;   /* FFT size */
    cx_t *W;   /* twiddle table, length N/2 */
} FFTCtx;

void fft_init   (FFTCtx *ctx, int N);
void fft_free   (FFTCtx *ctx);

/* In-place forward DFT: X[k] = Σ_{n=0}^{N-1} x[n] · e^{-j2πnk/N} */
void fft_forward(const FFTCtx *ctx, cx_t *x);

/* In-place inverse DFT with 1/N scaling:
   x[n] = (1/N) Σ_{k=0}^{N-1} X[k] · e^{+j2πnk/N} */
void fft_inverse(const FFTCtx *ctx, cx_t *x);

#endif
