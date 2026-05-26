#ifndef OFDM_H
#define OFDM_H

#include "utils.h"

typedef struct {
    int nfft;
    int cp_len;
} OFDMCtx;

void ofdm_init(OFDMCtx *ctx, int nfft, int cp_len);

/* freq_syms[nfft] -> time_out[cp_len+nfft] */
void ofdm_modulate(const OFDMCtx *ctx, const cx_t *freq_syms, cx_t *time_out);

/* time_in[cp_len+nfft] -> freq_out[nfft] */
void ofdm_demodulate(const OFDMCtx *ctx, const cx_t *time_in, cx_t *freq_out);

/* In-place FFT/IFFT on x[n].  sign=-1 forward, sign=+1 inverse. */
void radix2_fft(cx_t *x, int n, int sign);

#endif
