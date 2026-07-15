/* ================================================================
 *  mimo.c
 *  2x2 SU-MIMO channel model + MRC/ZF/MMSE detection
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "mimo.h"
#include <math.h>

void mimo_channel_init(MIMOChannel *ch, double snr_db) { ch->snr_db = snr_db; }

void mimo_channel_draw_1x2(cx_t h[2]) {
    double inv_sq2 = 1.0 / sqrt(2.0);
    for (int r = 0; r < 2; r++)
        h[r] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
}

void mimo_channel_draw_2x2(cx_t h[2][2]) {
    double inv_sq2 = 1.0 / sqrt(2.0);
    for (int r = 0; r < 2; r++)
        for (int t = 0; t < 2; t++)
            h[r][t] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
}

void mimo_channel_apply_1x2(const MIMOChannel *ch, const cx_t h[2],
                            cx_t tx, cx_t y[2]) {
    double snrlin = pow(10.0, ch->snr_db / 10.0);
    double sigma  = sqrt(1.0 / (2.0 * snrlin));
    for (int r = 0; r < 2; r++)
        y[r] = h[r] * tx + CX_MAKE(randn() * sigma, randn() * sigma);
}

void mimo_channel_apply_2x2(const MIMOChannel *ch, cx_t h[2][2],
                            const cx_t tx[2], cx_t y[2]) {
    double snrlin = pow(10.0, ch->snr_db / 10.0);
    double sigma  = sqrt(1.0 / (2.0 * snrlin));
    for (int r = 0; r < 2; r++) {
        cx_t s = h[r][0] * tx[0] + h[r][1] * tx[1];
        y[r] = s + CX_MAKE(randn() * sigma, randn() * sigma);
    }
}

void mrc_combine(const cx_t h[2], const cx_t y[2], double N0,
                 cx_t *x_hat, double *noise_var) {
    double hp = CX_NORM(h[0]) + CX_NORM(h[1]);
    if (hp < 1e-10) { *x_hat = CX_ZERO; *noise_var = N0; return; }
    cx_t num = conj(h[0]) * y[0] + conj(h[1]) * y[1];
    *x_hat     = num / hp;
    *noise_var = N0 / hp;
}

/* Shared 2x2 complex inverse: [[a,b],[c,d]]^-1 */
static void inv2x2(cx_t a, cx_t b, cx_t c, cx_t d, cx_t inv[2][2]) {
    cx_t det = a * d - b * c;
    if (cabs(det) < 1e-12) det = CX_MAKE(1e-12, 0.0);
    cx_t id = 1.0 / det;
    inv[0][0] =  d * id; inv[0][1] = -b * id;
    inv[1][0] = -c * id; inv[1][1] =  a * id;
}

void mimo_zf_detect(cx_t h[2][2], const cx_t y[2], double N0,
                    cx_t x_hat[2], double noise_var[2]) {
    cx_t hinv[2][2];
    inv2x2(h[0][0], h[0][1], h[1][0], h[1][1], hinv);
    x_hat[0] = hinv[0][0] * y[0] + hinv[0][1] * y[1];
    x_hat[1] = hinv[1][0] * y[0] + hinv[1][1] * y[1];
    noise_var[0] = N0 * (CX_NORM(hinv[0][0]) + CX_NORM(hinv[0][1]));
    noise_var[1] = N0 * (CX_NORM(hinv[1][0]) + CX_NORM(hinv[1][1]));
}

void mimo_mmse_detect(cx_t h[2][2], const cx_t y[2], double N0,
                      cx_t x_hat[2], double noise_var[2]) {
    cx_t h00 = h[0][0], h01 = h[0][1], h10 = h[1][0], h11 = h[1][1];

    /* A = H^H H + N0*I */
    cx_t a = conj(h00) * h00 + conj(h10) * h10 + N0;
    cx_t b = conj(h00) * h01 + conj(h10) * h11;
    cx_t c = conj(h01) * h00 + conj(h11) * h10;
    cx_t d = conj(h01) * h01 + conj(h11) * h11 + N0;
    cx_t Ainv[2][2];
    inv2x2(a, b, c, d, Ainv);

    /* W = Ainv * H^H */
    cx_t Hh00 = conj(h00), Hh01 = conj(h10);
    cx_t Hh10 = conj(h01), Hh11 = conj(h11);
    cx_t W00 = Ainv[0][0] * Hh00 + Ainv[0][1] * Hh10;
    cx_t W01 = Ainv[0][0] * Hh01 + Ainv[0][1] * Hh11;
    cx_t W10 = Ainv[1][0] * Hh00 + Ainv[1][1] * Hh10;
    cx_t W11 = Ainv[1][0] * Hh01 + Ainv[1][1] * Hh11;

    cx_t xs0 = W00 * y[0] + W01 * y[1];
    cx_t xs1 = W10 * y[0] + W11 * y[1];

    /* alpha_t = Re{(W H)_tt}: effective (biased) gain applied to x_t */
    double a0 = creal(W00 * h00 + W01 * h10);
    double a1 = creal(W10 * h01 + W11 * h11);
    if (a0 < 1e-6) a0 = 1e-6;
    if (a1 < 1e-6) a1 = 1e-6;

    x_hat[0] = xs0 / a0;
    x_hat[1] = xs1 / a1;
    noise_var[0] = (1.0 - a0) / a0;
    noise_var[1] = (1.0 - a1) / a1;
}
