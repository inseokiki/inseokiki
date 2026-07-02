#include "tdl_channel.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
 * Bessel function J0(x) — Numerical Recipes polynomial approximation.
 * Accurate to ~1e-7 for all real x.
 * ----------------------------------------------------------------------- */
static double bessel_j0(double x) {
    double ax = fabs(x);
    if (ax < 8.0) {
        double y = x * x;
        double p = 57568490574.0 + y*(-13362590354.0 + y*(651619640.7
                   + y*(-11214424.18 + y*(77392.33017 + y*(-184.9052456)))));
        double q = 57568490411.0 + y*(1029532985.0   + y*(9494680.718
                   + y*(59272.64853  + y*(267.8532712  + y*1.0))));
        return p / q;
    } else {
        double z  = 8.0 / ax;
        double y  = z * z;
        double xx = ax - 0.785398164;
        double p  = 1.0 + y*(-0.1098628627e-2 + y*(0.2734510407e-4
                    + y*(-0.2073370639e-5 + y*0.2093887211e-6)));
        double q  = -0.1562499995e-1 + y*(0.1430488765e-3
                    + y*(-0.6911147651e-5 + y*(0.7621095161e-6
                    - y*0.934945152e-7)));
        return sqrt(0.636619772 / ax) * (cos(xx)*p - z*sin(xx)*q);
    }
}

/* -----------------------------------------------------------------------
 * TDL tap tables (normalized delays and powers) — TS 38.901
 *
 * Normalized delay: τ_actual [s] = τ_norm × ds_rms [s]
 * Power: stored as dB, converted to linear and normalized in tdl_init().
 * ----------------------------------------------------------------------- */

/* TDL-A (TS 38.901 Table 7.7.2-1, 23 taps, NLOS) */
#define TDLA_NTAPS 23
static const double tdla_delay_norm[TDLA_NTAPS] = {
    0.0000, 0.3819, 0.4025, 0.5868, 0.4610, 0.5375, 0.6708,
    0.5750, 0.7618, 1.5375, 1.8978, 2.2242, 2.1718, 2.4942,
    2.5119, 3.0582, 4.0810, 4.4579, 4.5695, 4.7966, 5.0066,
    5.3043, 9.6586
};
static const double tdla_power_db[TDLA_NTAPS] = {
    -13.4,   0.0,  -2.2,  -4.0,  -6.0,  -8.2,  -9.9,
    -10.5,  -7.5, -15.9,  -6.6, -16.7, -12.4, -15.2,
    -10.8, -11.3, -12.7, -16.2, -18.3, -18.9, -16.6,
    -19.9, -29.7
};

/* TDL-C (TS 38.901 Table 7.7.2-3, 24 taps, NLOS) */
#define TDLC_NTAPS 24
static const double tdlc_delay_norm[TDLC_NTAPS] = {
    0.0000, 0.2099, 0.2219, 0.2329, 0.2176, 0.6366, 0.6448,
    0.6560, 0.6584, 0.7935, 0.8213, 0.9336, 1.2285, 1.3083,
    2.1704, 2.7105, 4.2589, 4.6003, 5.4902, 5.6077, 6.3065,
    6.6374, 7.0427, 8.6523
};
static const double tdlc_power_db[TDLC_NTAPS] = {
     -4.4,  -1.2,  -3.5,  -5.2,  -2.5,   0.0,  -2.2,
     -3.9,  -7.4,  -7.1, -10.7, -11.1,  -5.1,  -6.8,
     -8.7, -13.2, -13.9, -13.9, -15.8, -17.1, -16.0,
    -15.7, -21.6, -22.8
};

/* TDL-D (TS 38.901 Table 7.7.2-4, 13 taps, LOS, K_LOS=13.3 dB) */
#define TDLD_NTAPS 13
#define TDLD_K_DB  13.3
static const double tdld_delay_norm[TDLD_NTAPS] = {
    0.000,  0.035,  0.612,  1.363,  1.405,  1.804,
    2.596,  1.775,  4.042,  7.937,  9.424,  9.708, 12.525
};
static const double tdld_power_db[TDLD_NTAPS] = {
    -0.2, -13.5, -18.8, -21.0, -22.8, -17.9,
    -20.1, -21.9, -22.9, -27.8, -23.6, -24.8, -30.0
};

/* Draw one sample from the CN(0,1) distribution (complex Gaussian, unit variance) */
static cx_t complex_gaussian_01(void) {
    double per_dim_sigma = 1.0 / sqrt(2.0);   /* split variance equally between I and Q */
    return CX_MAKE(randn() * per_dim_sigma, randn() * per_dim_sigma);
}

/* -----------------------------------------------------------------------
 * tdl_init
 * ----------------------------------------------------------------------- */
void tdl_init(TDLChannel *ch, TDLModel model, double ds_rms_ns,
              double fd_hz, double snr_db, double scs_hz, double Tsym_s)
{
    memset(ch, 0, sizeof(TDLChannel));
    ch->model   = model;
    ch->scs_hz  = scs_hz;
    ch->is_LOS  = 0;
    ch->K_factor = 0.0;
    ch->N0      = 1.0 / pow(10.0, snr_db / 10.0);

    /* AR(1) Doppler correlation between consecutive OFDM symbols.
     * rho = J0(2π·fd·Ts): 1.0 = fully correlated (static), 0.0 = IID */
    ch->rho = (fd_hz > 0.0) ? bessel_j0(2.0 * PHY_PI * fd_hz * Tsym_s) : 1.0;

    /* Select the tap delay/power table for the requested model */
    const double *delay_norm_table = NULL;
    const double *power_db_table   = NULL;
    int           num_taps         = 0;

    switch (model) {
        case TDL_A:
            delay_norm_table = tdla_delay_norm;
            power_db_table   = tdla_power_db;
            num_taps         = TDLA_NTAPS;
            break;
        case TDL_C:
            delay_norm_table = tdlc_delay_norm;
            power_db_table   = tdlc_power_db;
            num_taps         = TDLC_NTAPS;
            break;
        case TDL_D:
            delay_norm_table = tdld_delay_norm;
            power_db_table   = tdld_power_db;
            num_taps         = TDLD_NTAPS;
            ch->is_LOS       = 1;
            ch->K_factor     = pow(10.0, TDLD_K_DB / 10.0);
            ch->phi_LOS      = 2.0 * PHY_PI * ((double)rand() / RAND_MAX);
            break;
    }
    ch->num_taps = num_taps;

    /* Convert normalised delays to seconds and powers from dB to linear.
     * Then normalise powers so they sum to 1 (unit average gain). */
    double ds_rms_s     = ds_rms_ns * 1e-9;
    double total_power  = 0.0;
    for (int tap = 0; tap < num_taps; tap++) {
        ch->delays_s[tap]   = delay_norm_table[tap] * ds_rms_s;
        ch->powers_lin[tap] = pow(10.0, power_db_table[tap] / 10.0);
        total_power        += ch->powers_lin[tap];
    }
    for (int tap = 0; tap < num_taps; tap++)
        ch->powers_lin[tap] /= total_power;

    /* Draw the initial tap gains */
    for (int tap = 0; tap < num_taps; tap++) {
        double tap_sigma = sqrt(ch->powers_lin[tap]);

        if (tap == 0 && ch->is_LOS) {
            /* Rician first tap: fixed LOS phasor + random NLOS component */
            double K            = ch->K_factor;
            double los_amp      = sqrt(K / (K + 1.0)) * tap_sigma;
            double nlos_sigma   = sqrt(1.0 / (K + 1.0)) * tap_sigma / sqrt(2.0);
            cx_t   los_phasor   = los_amp * CX_MAKE(cos(ch->phi_LOS), sin(ch->phi_LOS));
            cx_t   nlos_part    = CX_MAKE(randn() * nlos_sigma, randn() * nlos_sigma);
            ch->h[tap] = los_phasor + nlos_part;
        } else {
            ch->h[tap] = tap_sigma * complex_gaussian_01();
        }
    }
}

/* -----------------------------------------------------------------------
 * tdl_next_symbol — AR(1) Doppler evolution per OFDM symbol
 *
 * h[t+1] = rho * h[t] + sqrt(1-rho^2) * w,   w ~ CN(0, P_l)
 * ----------------------------------------------------------------------- */
void tdl_next_symbol(TDLChannel *ch)
{
    double rho            = ch->rho;
    double innovation_gain = sqrt(1.0 - rho * rho);   /* keeps per-tap variance constant */

    for (int tap = 0; tap < ch->num_taps; tap++) {
        double tap_sigma = sqrt(ch->powers_lin[tap]);

        if (tap == 0 && ch->is_LOS) {
            /* Rician tap: AR(1) on the NLOS part only; LOS phasor stays fixed */
            double K          = ch->K_factor;
            double los_amp    = sqrt(K / (K + 1.0)) * tap_sigma;
            double nlos_sigma = sqrt(1.0 / (K + 1.0)) * tap_sigma;

            cx_t los_phasor   = los_amp * CX_MAKE(cos(ch->phi_LOS), sin(ch->phi_LOS));
            cx_t prev_nlos    = ch->h[tap] - los_phasor;
            cx_t new_nlos     = rho * prev_nlos + innovation_gain * nlos_sigma * complex_gaussian_01();
            ch->h[tap] = los_phasor + new_nlos;
        } else {
            /* NLOS tap: standard Rayleigh AR(1) */
            ch->h[tap] = rho * ch->h[tap] + innovation_gain * tap_sigma * complex_gaussian_01();
        }
    }
}

/* -----------------------------------------------------------------------
 * tdl_new_realization — IID channel (independent across HARQ rounds)
 * ----------------------------------------------------------------------- */
void tdl_new_realization(TDLChannel *ch)
{
    double saved_rho = ch->rho;
    ch->rho = 0.0;
    tdl_next_symbol(ch);   /* rho=0 → pure new draw */
    ch->rho = saved_rho;
}

/* -----------------------------------------------------------------------
 * tdl_get_cfr — compute CFR at num_sc active subcarriers
 *
 * H[k] = sum_l h_l * exp(-j * 2*pi * k * scs_hz * delays_s[l])
 *
 * k = 0, 1, ..., num_sc-1 corresponds to active SC indices.
 * ----------------------------------------------------------------------- */
/* H[sc] = Σ_tap  h_tap · exp(−j·2π·sc·Δf·τ_tap) */
void tdl_get_cfr(const TDLChannel *ch, int num_sc, cx_t *h_freq)
{
    for (int sc = 0; sc < num_sc; sc++) {
        cx_t channel_gain = CX_ZERO;
        for (int tap = 0; tap < ch->num_taps; tap++) {
            double phase_rad = -2.0 * PHY_PI * sc * ch->scs_hz * ch->delays_s[tap];
            cx_t   phase_rot = CX_MAKE(cos(phase_rad), sin(phase_rad));
            channel_gain += ch->h[tap] * phase_rot;
        }
        h_freq[sc] = channel_gain;
    }
}

/* -----------------------------------------------------------------------
 * tdl_apply_cfr — apply channel + AWGN to one OFDM symbol
 *
 * rx[k] = H[k] * tx[k] + n[k],  n[k] ~ CN(0, N0)
 * ----------------------------------------------------------------------- */
/* rx[sc] = H[sc] · tx[sc] + noise,   noise ~ CN(0, N0) */
void tdl_apply_cfr(const TDLChannel *ch, const cx_t *tx_freq,
                   int num_sc, cx_t *rx_freq)
{
    /* AWGN: total power N0 split equally between I and Q */
    double noise_sigma = sqrt(ch->N0 / 2.0);

    for (int sc = 0; sc < num_sc; sc++) {
        /* Compute frequency-domain channel gain H[sc] */
        cx_t channel_gain = CX_ZERO;
        for (int tap = 0; tap < ch->num_taps; tap++) {
            double phase_rad = -2.0 * PHY_PI * sc * ch->scs_hz * ch->delays_s[tap];
            cx_t   phase_rot = CX_MAKE(cos(phase_rad), sin(phase_rad));
            channel_gain += ch->h[tap] * phase_rot;
        }

        cx_t noise     = CX_MAKE(randn() * noise_sigma, randn() * noise_sigma);
        rx_freq[sc]    = channel_gain * tx_freq[sc] + noise;
    }
}
