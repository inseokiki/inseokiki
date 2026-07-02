#ifndef TDL_CHANNEL_H
#define TDL_CHANNEL_H

/*
 * 3GPP TS 38.901 TDL (Tapped Delay Line) Channel Models
 * Supports TDL-A (NLOS), TDL-C (NLOS), TDL-D (LOS)
 * Doppler evolution via AR(1) approximation to Jakes spectrum: rho = J0(2*pi*fd*Tsym)
 */

#include "utils.h"

#define TDL_MAX_TAPS 24

typedef enum {
    TDL_A = 0,   /* NLOS, reference DS_rms = 30 ns  (TS 38.901 Table 7.7.2-1) */
    TDL_C,       /* NLOS, reference DS_rms = 300 ns (TS 38.901 Table 7.7.2-3) */
    TDL_D,       /* LOS,  K = 13.3 dB,  DS_rms = 30 ns  (TS 38.901 Table 7.7.2-4) */
} TDLModel;

typedef struct {
    TDLModel model;
    int      num_taps;
    double   delays_s[TDL_MAX_TAPS];    /* actual tap delays [s]             */
    double   powers_lin[TDL_MAX_TAPS];  /* normalized tap powers (sum = 1.0) */
    cx_t     h[TDL_MAX_TAPS];           /* current complex channel tap gains  */

    double   rho;       /* AR(1) correlation: J0(2*pi*fd*Tsym)               */
    int      is_LOS;    /* 1 = first tap has Rician component (TDL-D)        */
    double   K_factor;  /* Rician K (linear) for LOS tap                     */
    double   phi_LOS;   /* random initial LOS phase [rad]                    */

    double   N0;        /* noise variance per subcarrier: 1/SNR_lin           */
    double   scs_hz;    /* subcarrier spacing [Hz]                            */
} TDLChannel;

/*
 * Initialize TDL channel.
 *   model     : TDL_A / TDL_C / TDL_D
 *   ds_rms_ns : desired RMS delay spread [ns]  (scales the normalized delays)
 *   fd_hz     : maximum Doppler frequency [Hz]  (0 = static, same H per slot)
 *   snr_db    : operating SNR [dB]
 *   scs_hz    : subcarrier spacing [Hz]  (e.g. 30e3 for 30 kHz)
 *   Tsym_s    : OFDM symbol duration INCLUDING CP [s] = (NFFT + CP) / Fs
 */
void tdl_init(TDLChannel *ch, TDLModel model, double ds_rms_ns,
              double fd_hz, double snr_db, double scs_hz, double Tsym_s);

/*
 * Advance channel by one OFDM symbol using AR(1) Doppler model.
 * Must be called once per OFDM symbol (or per HARQ round for IID model).
 */
void tdl_next_symbol(TDLChannel *ch);

/*
 * Apply frequency-domain channel to one OFDM symbol's resource grid.
 *   tx_freq  : transmitted symbols [num_sc]  (resource grid, active SCs only)
 *   num_sc   : number of active subcarriers
 *   rx_freq  : output received symbols [num_sc]
 *
 * CFR at SC index k: H[k] = sum_l h_l * exp(-j*2*pi*k*scs_hz*delays_s[l])
 * Adds AWGN with power N0 per (real+imag) component.
 */
void tdl_apply_cfr(const TDLChannel *ch, const cx_t *tx_freq,
                   int num_sc, cx_t *rx_freq);

/*
 * Generate a new independent channel realization (for IID HARQ model).
 * Equivalent to calling tdl_next_symbol() with rho = 0.
 */
void tdl_new_realization(TDLChannel *ch);

/* Return ideal (perfect CSI) CFR at num_sc subcarriers. */
void tdl_get_cfr(const TDLChannel *ch, int num_sc, cx_t *h_freq);

#endif /* TDL_CHANNEL_H */
