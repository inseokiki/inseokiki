#include "tdl_time.h"
#include <math.h>
#include <string.h>

int tdl_time_init(TDLTimeState *state, const TDLChannel *ch, double fd) {
    if (!state || !ch || !isfinite(fd) || fd < 0.0 ||
        ch->profile.num_taps < 1 || ch->profile.num_taps > TDL_MAX_TAPS) return -1;
    for (int l = 0; l < ch->profile.num_taps; l++)
        if (!isfinite(ch->profile.power_lin[l]) || ch->profile.power_lin[l] < 0.0) return -1;
    if (!isfinite(ch->profile.los_power_lin) || ch->profile.los_power_lin < 0.0) return -1;
    memset(state, 0, sizeof(*state));
    state->profile = ch->profile;
    state->max_doppler_hz = fd;
    for (int l = 0; l < ch->profile.num_taps; l++) {
        double rotation = atan2(randn(), randn());
        double sigma = sqrt(ch->profile.power_lin[l]/(2.0*TDL_TIME_OSCILLATORS));
        for (int m = 0; m < TDL_TIME_OSCILLATORS; m++) {
            double angle = rotation + 2.0*PHY_PI*m/TDL_TIME_OSCILLATORS;
            state->frequency_hz[l][m] = fd*cos(angle);
            state->coefficient[l][m] = CX_MAKE(sigma*randn(), sigma*randn());
        }
    }
    if (ch->profile.has_los) {
        double phase = atan2(randn(), randn());
        state->los_phasor = sqrt(ch->profile.los_power_lin)*CX_MAKE(cos(phase),sin(phase));
    }
    return 0;
}

int tdl_time_sample(const TDLTimeState *state, double time_s, cx_t *out) {
    if (!state || !out || !isfinite(time_s) || time_s < 0.0 ||
        !isfinite(state->max_doppler_hz*time_s)) return -1;
    for (int l = 0; l < state->profile.num_taps; l++) {
        cx_t tap = 0.0;
        for (int m = 0; m < TDL_TIME_OSCILLATORS; m++) {
            /* Reduce cycles before multiplying by 2*pi to avoid phase overflow. */
            double phase = 2.0*PHY_PI*remainder(state->frequency_hz[l][m]*time_s,1.0);
            tap += state->coefficient[l][m]*CX_MAKE(cos(phase),sin(phase));
        }
        out[l] = tap;
    }
    if (state->profile.has_los) {
        double phase = 2.0*PHY_PI*remainder(0.7*state->max_doppler_hz*time_s,1.0);
        out[0] += state->los_phasor*CX_MAKE(cos(phase),sin(phase));
    }
    return 0;
}
