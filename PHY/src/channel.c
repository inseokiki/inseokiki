/* ================================================================
 *  channel.c
 *  AWGN and flat-fading channel models
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "channel.h"
#include <math.h>
#include <stdlib.h>

void awgn_init(AWGNChannel *ch, double snr_db)   { ch->snr_db = snr_db; }
void awgn_set_snr(AWGNChannel *ch, double snr_db) { ch->snr_db = snr_db; }

void awgn_add_noise(AWGNChannel *ch, const cx_t *sig, int n,
                    int nfft, cx_t *out) {
    double snrlin = pow(10.0, ch->snr_db / 10.0);
    double np;
    if (nfft > 0) {
        np = 1.0 / (nfft * snrlin);
    } else {
        double sp = 0.0;
        for (int i = 0; i < n; i++) sp += CX_NORM(sig[i]);
        sp /= n;
        np = sp / snrlin;
    }
    double sigma = sqrt(np / 2.0);
    for (int i = 0; i < n; i++)
        out[i] = sig[i] + CX_MAKE(randn() * sigma, randn() * sigma);
}

void flat_fading_init(FlatFadingChannel *ch, double snr_db) { ch->snr_db = snr_db; }
void flat_fading_set_snr(FlatFadingChannel *ch, double snr_db) { ch->snr_db = snr_db; }

void flat_fading_apply(FlatFadingChannel *ch, const cx_t *tx, int n,
                       cx_t *out, cx_t *true_h) {
    double inv_sq2 = 1.0 / sqrt(2.0);
    cx_t h = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
    if (true_h) *true_h = h;
    double snrlin = pow(10.0, ch->snr_db / 10.0);
    double sigma  = sqrt(1.0 / (2.0 * snrlin));
    for (int i = 0; i < n; i++)
        out[i] = h * tx[i] + CX_MAKE(randn() * sigma, randn() * sigma);
}
