/* ================================================================
 *  modulation.h
 *  QAM modulation/demodulation, LLR demapping, soft symbols for turbo eq.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef MODULATION_H
#define MODULATION_H

#include "utils.h"

int  get_bits_per_symbol(const char *mod);

/* QAM modulate bits[num_bits] -> syms[num_bits/bps].  Caller allocates syms. */
void qam_modulate(const int *bits, int num_bits, const char *mod, cx_t *syms);

/* QAM hard demodulate syms[n] -> bits[n*bps]. */
void qam_demodulate(const cx_t *syms, int n, const char *mod, int *bits);

/* Soft LLR: syms[n] -> llr[n*bps].  noise_var = N0. */
void qam_demap_llr(const cx_t *syms, int n, const char *mod,
                   double noise_var, double *llr);

/* MMSE-aware soft LLR.  h_data[n] = channel estimates at each symbol. */
void qam_demap_llr_mmse(const cx_t *rx_syms, int n, const char *mod,
                         const cx_t *h_data, double N0, double *llr);

/* Turbo equalization: a priori bit LLR[n*bps] -> per-symbol soft mean
   (mean_out[n]) and total I+Q variance (var_out[n]). LLR=0 (no a priori)
   gives mean=0, var=symbol energy (uniform over the constellation). */
void qam_soft_symbol(const double *llr_apriori, int n, const char *mod,
                     cx_t *mean_out, double *var_out);

/* Legacy QPSK */
void qpsk_modulate  (const int *bits, int n, cx_t *syms);
void qpsk_demodulate(const cx_t *syms, int n, int *bits);

#endif
