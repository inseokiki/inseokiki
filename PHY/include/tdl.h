/* ================================================================
 *  tdl.h
 *  TDL frequency-selective fading channel (approx. 6-tap NLOS profile)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef TDL_H
#define TDL_H

#include "utils.h"

#define TDL_MAX_TAPS 6

/* Approximate 6-tap exponential-decay NLOS power-delay-profile, scaled by
 * a configurable RMS delay spread. This is NOT the exact TS 38.901
 * Table 7.7.2-x (TDL-A/B/C) coefficient set -- those were not available
 * to cross-check against in this repo, so a simplified illustrative
 * profile is used instead (implementation-defined approximation). LOS/
 * Rician taps (TDL-D/E) are out of scope. */
typedef struct {
    int    num_taps;
    double delay_s[TDL_MAX_TAPS];    /* seconds */
    double power_lin[TDL_MAX_TAPS];  /* linear, sums to 1 */
} TDLProfile;

typedef struct {
    double   snr_db;
    double   scs_hz;
    TDLProfile profile;
} TDLChannel;

void tdl_channel_init(TDLChannel *ch, double delay_spread_ns,
                      double scs_hz, double snr_db);

/* Draw tap gains ~ CN(0, power_lin[l]) iid, held constant for one trial
   (block-flat in time, same convention as flat_fading_apply in channel.c). */
void tdl_draw(const TDLChannel *ch, cx_t *taps_out);

/* H(k) = sum_l taps[l] * exp(-j*2*pi*k*scs_hz*delay_s[l]) */
cx_t tdl_freq_response(const TDLChannel *ch, const cx_t *taps, int k);

/* tx[active] -> rx[active] with per-RE frequency-selective H(k) and
   independent AWGN per RE (Es/N0 = snr_db). h_out[active] optional:
   writes the true per-RE channel (for verification). */
void tdl_channel_apply(const TDLChannel *ch, const cx_t *taps,
                       const cx_t *tx, int active, cx_t *rx, cx_t *h_out);

#endif
