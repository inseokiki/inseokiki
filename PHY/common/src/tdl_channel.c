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

/* -----------------------------------------------------------------------
 * Helper: generate a CN(0,1) complex Gaussian sample.
 * ----------------------------------------------------------------------- */
static cx_t cn01(void) {
    double inv_sq2 = 1.0 / sqrt(2.0);
    return CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
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

    /* Doppler AR(1) coefficient */
    ch->rho = (fd_hz > 0.0) ? bessel_j0(2.0 * PHY_PI * fd_hz * Tsym_s) : 1.0;

    /* Select tap table */
    const double *dly_norm = NULL;
    const double *pwr_db   = NULL;
    int           ntaps    = 0;

    switch (model) {
        case TDL_A:
            dly_norm = tdla_delay_norm; pwr_db = tdla_power_db; ntaps = TDLA_NTAPS;
            break;
        case TDL_C:
            dly_norm = tdlc_delay_norm; pwr_db = tdlc_power_db; ntaps = TDLC_NTAPS;
            break;
        case TDL_D:
            dly_norm = tdld_delay_norm; pwr_db = tdld_power_db; ntaps = TDLD_NTAPS;
            ch->is_LOS  = 1;
            ch->K_factor = pow(10.0, TDLD_K_DB / 10.0);
            ch->phi_LOS  = 2.0 * PHY_PI * ((double)rand() / RAND_MAX);
            break;
    }
    ch->num_taps = ntaps;

    /* Convert powers dB → linear and normalize so sum = 1 */
    double ds_rms_s  = ds_rms_ns * 1e-9;
    double psum = 0.0;
    for (int l = 0; l < ntaps; l++) {
        ch->delays_s[l]    = dly_norm[l] * ds_rms_s;
        ch->powers_lin[l]  = pow(10.0, pwr_db[l] / 10.0);
        psum              += ch->powers_lin[l];
    }
    for (int l = 0; l < ntaps; l++) ch->powers_lin[l] /= psum;

    /* Initialize tap gains: fresh CN(0, P_l) realization */
    for (int l = 0; l < ntaps; l++) {
        double sigma = sqrt(ch->powers_lin[l]);
        if (l == 0 && ch->is_LOS) {
            /* Rician first tap: LOS (deterministic) + NLOS (Rayleigh) */
            double K = ch->K_factor;
            double los_amp  = sqrt(K / (K + 1.0)) * sigma;
            double nlos_sig = sqrt(1.0 / (K + 1.0)) * sigma / sqrt(2.0);
            ch->h[0] = los_amp * CX_MAKE(cos(ch->phi_LOS), sin(ch->phi_LOS))
                     + CX_MAKE(randn() * nlos_sig, randn() * nlos_sig);
        } else {
            ch->h[l] = sigma * cn01();
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
    double rho  = ch->rho;
    double rho2 = sqrt(1.0 - rho * rho);
    for (int l = 0; l < ch->num_taps; l++) {
        double sigma = sqrt(ch->powers_lin[l]);
        if (l == 0 && ch->is_LOS) {
            double K       = ch->K_factor;
            double los_amp = sqrt(K / (K + 1.0)) * sigma;
            double ns      = sqrt(1.0 / (K + 1.0)) * sigma;
            cx_t prev_nlos = ch->h[l] - los_amp * CX_MAKE(cos(ch->phi_LOS), sin(ch->phi_LOS));
            cx_t new_nlos  = rho * prev_nlos + rho2 * ns * cn01();
            ch->h[l] = los_amp * CX_MAKE(cos(ch->phi_LOS), sin(ch->phi_LOS)) + new_nlos;
        } else {
            ch->h[l] = rho * ch->h[l] + rho2 * sigma * cn01();
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
void tdl_get_cfr(const TDLChannel *ch, int num_sc, cx_t *h_freq)
{
    for (int k = 0; k < num_sc; k++) {
        cx_t hk = CX_ZERO;
        for (int l = 0; l < ch->num_taps; l++) {
            double phi = -2.0 * PHY_PI * k * ch->scs_hz * ch->delays_s[l];
            hk += ch->h[l] * CX_MAKE(cos(phi), sin(phi));
        }
        h_freq[k] = hk;
    }
}

/* -----------------------------------------------------------------------
 * tdl_apply_cfr — apply channel + AWGN to one OFDM symbol
 *
 * rx[k] = H[k] * tx[k] + n[k],  n[k] ~ CN(0, N0)
 * ----------------------------------------------------------------------- */
void tdl_apply_cfr(const TDLChannel *ch, const cx_t *tx_freq,
                   int num_sc, cx_t *rx_freq)
{
    double sigma_n = sqrt(ch->N0 / 2.0);   /* per real/imag component */
    for (int k = 0; k < num_sc; k++) {
        cx_t hk = CX_ZERO;
        for (int l = 0; l < ch->num_taps; l++) {
            double phi = -2.0 * PHY_PI * k * ch->scs_hz * ch->delays_s[l];
            hk += ch->h[l] * CX_MAKE(cos(phi), sin(phi));
        }
        rx_freq[k] = hk * tx_freq[k]
                   + CX_MAKE(randn() * sigma_n, randn() * sigma_n);
    }
}
