/* ================================================================
 *  tdl.c
 *  TDL frequency-selective fading channel (approx. 6-tap NLOS profile)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "tdl.h"
#include <math.h>

void tdl_channel_init(TDLChannel *ch, double delay_spread_ns,
                      double scs_hz, double snr_db) {
    static const double delay_ns_ref[TDL_MAX_TAPS] = {0, 30, 70, 90, 110, 190};
    static const double power_db[TDL_MAX_TAPS]     = {0, -1.5, -1.4, -3.6, -0.6, -9.1};

    ch->snr_db = snr_db;
    ch->scs_hz = scs_hz;
    ch->profile.num_taps = TDL_MAX_TAPS;

    double lin[TDL_MAX_TAPS], sum_lin = 0.0;
    for (int l = 0; l < TDL_MAX_TAPS; l++) {
        lin[l] = pow(10.0, power_db[l] / 10.0);
        sum_lin += lin[l];
    }
    double scale = delay_spread_ns / 30.0;
    for (int l = 0; l < TDL_MAX_TAPS; l++) {
        ch->profile.power_lin[l] = lin[l] / sum_lin;
        ch->profile.delay_s[l]   = delay_ns_ref[l] * scale * 1e-9;
    }
}

void tdl_draw(const TDLChannel *ch, cx_t *taps_out) {
    for (int l = 0; l < ch->profile.num_taps; l++) {
        double sigma = sqrt(ch->profile.power_lin[l] / 2.0);
        taps_out[l] = CX_MAKE(randn() * sigma, randn() * sigma);
    }
}

cx_t tdl_freq_response(const TDLChannel *ch, const cx_t *taps, int k) {
    cx_t h = CX_ZERO;
    for (int l = 0; l < ch->profile.num_taps; l++) {
        double phase = -2.0 * PHY_PI * (double)k * ch->scs_hz * ch->profile.delay_s[l];
        h += taps[l] * CX_MAKE(cos(phase), sin(phase));
    }
    return h;
}

void tdl_channel_apply(const TDLChannel *ch, const cx_t *taps,
                       const cx_t *tx, int active, cx_t *rx, cx_t *h_out) {
    double snrlin = pow(10.0, ch->snr_db / 10.0);
    double sigma  = sqrt(1.0 / (2.0 * snrlin));
    for (int k = 0; k < active; k++) {
        cx_t h = tdl_freq_response(ch, taps, k);
        if (h_out) h_out[k] = h;
        rx[k] = h * tx[k] + CX_MAKE(randn() * sigma, randn() * sigma);
    }
}
