#include "ofdm.h"
#include <stdlib.h>
#include <string.h>

void ofdm_init(OFDMCtx *ctx, int nfft, int cp_len) {
    ctx->nfft   = nfft;
    ctx->cp_len = cp_len;
    fft_init(&ctx->fft, nfft);
}

void ofdm_free(OFDMCtx *ctx) {
    fft_free(&ctx->fft);
}

void ofdm_modulate(const OFDMCtx *ctx, const cx_t *freq, cx_t *time_out) {
    int   N   = ctx->nfft;
    int   cp  = ctx->cp_len;
    cx_t *tmp = (cx_t *)malloc(N * sizeof(cx_t));
    memcpy(tmp, freq, N * sizeof(cx_t));
    fft_inverse(&ctx->fft, tmp);          /* IDFT, includes 1/N scaling */
    for (int i = 0; i < cp; i++) time_out[i]      = tmp[N - cp + i];  /* CP */
    for (int i = 0; i < N;  i++) time_out[cp + i] = tmp[i];
    free(tmp);
}

void ofdm_demodulate(const OFDMCtx *ctx, const cx_t *time_in, cx_t *freq_out) {
    int   N   = ctx->nfft;
    int   cp  = ctx->cp_len;
    cx_t *tmp = (cx_t *)malloc(N * sizeof(cx_t));
    for (int i = 0; i < N; i++) tmp[i] = time_in[cp + i];  /* remove CP */
    fft_forward(&ctx->fft, tmp);          /* DFT */
    memcpy(freq_out, tmp, N * sizeof(cx_t));
    free(tmp);
}
