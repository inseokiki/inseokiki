#ifndef OFDM_H
#define OFDM_H

#include "utils.h"
#include "fft.h"

typedef struct {
    int    nfft;
    int    cp_len;
    FFTCtx fft;   /* precomputed twiddle table, shared by mod/demod */
} OFDMCtx;

void ofdm_init(OFDMCtx *ctx, int nfft, int cp_len);
void ofdm_free(OFDMCtx *ctx);

/* freq_syms[nfft] -> time_out[cp_len + nfft] */
void ofdm_modulate  (const OFDMCtx *ctx, const cx_t *freq_syms, cx_t *time_out);

/* time_in[cp_len + nfft] -> freq_out[nfft] */
void ofdm_demodulate(const OFDMCtx *ctx, const cx_t *time_in,   cx_t *freq_out);

#endif
