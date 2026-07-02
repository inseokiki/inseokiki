#ifndef CHANNEL_ESTIMATION_H
#define CHANNEL_ESTIMATION_H

#include "utils.h"

/* LS estimate: h_out[n] = rx_pilots[n] / tx_pilots[n] */
void ls_estimate(const cx_t *rx, const cx_t *tx, int n, cx_t *h_out);

/* Linear freq-domain interpolation.  pilot_pos[] sorted ascending.
   h_out[num_active_sc] must be pre-allocated. */
void interpolate_channel(const cx_t *h_pilots, int num_pilots,
                         const int *pilot_pos,
                         int num_active_sc, cx_t *h_out);

/* ZF equalization: eq[n] = rx[n] / h[n] */
void zf_equalize(const cx_t *rx, const cx_t *h, int n, cx_t *eq);

/* MMSE equalization: w[k]=h*[k]/(|h|^2+N0), eq[n]=w[n]*rx[n].
   If alpha_out != NULL, writes per-SC bias alpha[n]=|h|^2/(|h|^2+N0). */
void mmse_equalize(const cx_t *rx, const cx_t *h, int n,
                   double N0, cx_t *eq, double *alpha_out);

/* ── LMMSE channel estimation ───────────────────────────────────────────────
 * Precomputed filter W = R_dp · (R_pp + N0·I)^{-1}
 * Channel frequency correlation: R(Δk) = exp(-2π·τ_rms·Δf·|Δk|)
 * Build once per SNR point; apply once per HARQ round per antenna.
 */
typedef struct {
    int     np;  /* pilot count    */
    int     nd;  /* data SC count  */
    double *W;   /* [nd × np] real filter matrix, row-major */
} LMMSEFilter;

/* Build filter.  tau_rms_ns: RMS delay spread [ns], scs_hz: SC spacing [Hz] */
void lmmse_filter_build(LMMSEFilter *f,
                        const int *pilot_pos, int np,
                        const int *data_pos,  int nd,
                        double N0, double scs_hz, double tau_rms_ns);

/* Apply filter: h_out[nd] = W · h_ls_pilots[np]  (real weight, complex h) */
void lmmse_filter_apply(const LMMSEFilter *f,
                        const cx_t *h_ls, cx_t *h_out);

void lmmse_filter_free(LMMSEFilter *f);

#endif
