/* ================================================================
 *  tdl.c
 *  TDL frequency-selective fading channel -- TS 38.901 §7.7.2/7.7.3
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "tdl.h"
#include "tdl_tables.h"
#include <math.h>

void tdl_channel_init(TDLChannel *ch, char profile_letter,
                      double delay_spread_ns, double scs_hz, double snr_db) {
    const TDLProfileSpec *spec = tdl_profile_lookup(profile_letter);
    /* config_parser.c's validate_config() rejects an unrecognized
     * TDL_PROFILE before any simulation function (and therefore this)
     * ever runs -- see tdl.h. */
    if (!spec) spec = tdl_profile_lookup('A');

    ch->snr_db = snr_db;
    ch->scs_hz = scs_hz;
    ch->profile.num_taps = spec->num_taps;
    ch->profile.has_los   = spec->has_los;

    /* TS 38.901 §7.7.3 eq.(7.7-1): scaled_delay_ns = delay_norm *
     * desired_delay_spread_ns (see tdl.h's provenance note on this
     * equation). Power values are converted to linear and jointly
     * normalized (across all Rayleigh taps + the LOS component, if any)
     * so the total average power sums to exactly 1 -- required because
     * the raw table dB values do not already sum to 0dB (see tdl.h). */
    double sum_lin = 0.0;
    double lin[TDL_MAX_TAPS];
    for (int l = 0; l < spec->num_taps; l++) {
        lin[l] = pow(10.0, spec->taps[l].power_db / 10.0);
        sum_lin += lin[l];
    }
    double los_lin = 0.0;
    if (spec->has_los) {
        los_lin = pow(10.0, spec->los_power_db / 10.0);
        sum_lin += los_lin;
    }

    for (int l = 0; l < spec->num_taps; l++) {
        ch->profile.delay_s[l]   = spec->taps[l].delay_norm * delay_spread_ns * 1e-9;
        ch->profile.power_lin[l] = lin[l] / sum_lin;
    }
    ch->profile.los_power_lin = spec->has_los ? (los_lin / sum_lin) : 0.0;
}

void tdl_draw(const TDLChannel *ch, cx_t *taps_out) {
    for (int l = 0; l < ch->profile.num_taps; l++) {
        double sigma = sqrt(ch->profile.power_lin[l] / 2.0);
        taps_out[l] = CX_MAKE(randn() * sigma, randn() * sigma);
    }
    if (ch->profile.has_los) {
        /* Rician tap 0: deterministic LOS mean (magnitude sqrt(los_power),
         * phase uniform on [0,2pi) -- see tdl.h's tdl_draw() doc comment
         * for why the phase is redrawn every call) plus the Rayleigh
         * component already drawn above for tap 0. */
        double mag = sqrt(ch->profile.los_power_lin);
        double phase = atan2(randn(), randn());
        taps_out[0] += CX_MAKE(mag * cos(phase), mag * sin(phase));
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
