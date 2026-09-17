/* ================================================================
 *  tdl.h
 *  TDL frequency-selective fading channel -- TS 38.901 §7.7.2/7.7.3
 *  TDL-A/B/C/D/E tap delay/power profiles (exact per-model tables)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef TDL_H
#define TDL_H

#include "utils.h"

/* Largest Rayleigh/diffuse tap count across the 5 profiles (TDL-C, 24
   taps) -- see tdl_tables.h for the per-profile counts (A/B=23, C=24,
   D=13, E=14). TDL-D/E's extra LOS (Rician) component does not consume
   an array slot -- see TDLProfile.has_los below. */
#define TDL_MAX_TAPS 24

/* TS 38.901 Table 7.7.2-1..5 profile, resolved to actual delays/powers
 * for one TDL_DELAY_SPREAD_NS value (§7.7.3 eq.(7.7-1): delay_s =
 * delay_norm * desired_delay_spread_ns, linear scaling -- the equation
 * itself is an embedded image in the primary source, corroborated only
 * by the surrounding descriptive text, not pixel-read; see tdl_tables.h).
 * power_lin[] holds only the Rayleigh/diffuse component of each tap;
 * has_los/los_power_lin (TDL-D/E only) is deterministic-mean-plus-
 * Rayleigh (Rician) fading co-located with tap 0's delay. Both are
 * jointly normalized so total average power (all Rayleigh taps +
 * los_power_lin if present) sums to exactly 1 -- the raw table dB values
 * do NOT already sum to 0dB (checked: TDL-A/B/C raw sums are +5.4/+8.5/
 * +7.7dB, TDL-D/E are within ~0.5dB since those are LOS-dominated), so
 * this renormalization is required, not optional. */
typedef struct {
    int    num_taps;
    double delay_s[TDL_MAX_TAPS];
    double power_lin[TDL_MAX_TAPS];
    int    has_los;
    double los_power_lin;
} TDLProfile;

typedef struct {
    double   snr_db;
    double   scs_hz;
    TDLProfile profile;
} TDLChannel;

/* profile_letter: 'A'..'E' (case-insensitive), TS 38.901 Table 7.7.2-1..5.
   Aborts via CFG_ERR-style caller-side validation is NOT done here --
   config_parser.c validates TDL_PROFILE before this is ever called, so
   an invalid letter here is a programming error, not a user input error
   (matches this project's convention for post-validation internal calls:
   see e.g. mcs_table_from_str()'s callers). */
void tdl_channel_init(TDLChannel *ch, char profile_letter,
                      double delay_spread_ns, double scs_hz, double snr_db);

/* Draw tap gains, held constant for one trial (block-flat in time, same
   convention as flat_fading_apply in channel.c). Rayleigh taps ~
   CN(0, power_lin[l]) iid. If has_los, tap 0 additionally gets a Rician
   mean component: magnitude sqrt(los_power_lin), phase drawn uniform on
   [0,2pi) via atan2(randn(),randn()) (a 2D isotropic Gaussian vector has
   a uniform angle) -- independently re-drawn every call along with the
   Rayleigh taps, since each tdl_draw() call represents one fresh
   independent channel realization (a new trial or HARQ attempt) in this
   project's existing convention, not a continuously-tracked single UE
   trajectory; TS 38.901 itself does not mandate a specific LOS-phase
   re-randomization convention across such per-trial realizations, so
   this is this project's own implementation choice. */
void tdl_draw(const TDLChannel *ch, cx_t *taps_out);

/* H(k) = sum_l taps[l] * exp(-j*2*pi*k*scs_hz*delay_s[l]) */
cx_t tdl_freq_response(const TDLChannel *ch, const cx_t *taps, int k);

/* tx[active] -> rx[active] with per-RE frequency-selective H(k) and
   independent AWGN per RE (Es/N0 = snr_db). h_out[active] optional:
   writes the true per-RE channel (for verification). */
void tdl_channel_apply(const TDLChannel *ch, const cx_t *taps,
                       const cx_t *tx, int active, cx_t *rx, cx_t *h_out);

#endif
