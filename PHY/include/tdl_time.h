#ifndef TDL_TIME_H
#define TDL_TIME_H

#include "tdl.h"

/* Implementation-defined finite spectral approximation, not a spec constant. */
#define TDL_TIME_OSCILLATORS 32

typedef struct {
    TDLProfile profile;
    double max_doppler_hz;
    double frequency_hz[TDL_MAX_TAPS][TDL_TIME_OSCILLATORS];
    cx_t coefficient[TDL_MAX_TAPS][TDL_TIME_OSCILLATORS];
    cx_t los_phasor;
} TDLTimeState;

/* Initialization consumes the project's seeded RNG. Sampling consumes none.
 * Jakes diffuse spectrum is approximated by a finite Gaussian spectral sum.
 * LOS D/E rotates at 0.7*f_D (TR 38.901 V17.1.0 7.7.2).
 * Returns 0 on success, -1 for invalid arguments. State must be initialized
 * successfully before sampling. Times are absolute seconds, nonnegative. */
int tdl_time_init(TDLTimeState *state, const TDLChannel *channel, double max_doppler_hz);
int tdl_time_sample(const TDLTimeState *state, double time_s, cx_t *taps_out);

#endif
