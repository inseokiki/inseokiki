#ifndef QAM_H
#define QAM_H

/* Complex baseband symbol */
typedef struct { double re, im; } cx_t;

/* Returns Qm (bits per symbol) from modulation string.
 * "QPSK"->2, "16QAM"->4, "64QAM"->6, "256QAM"->8 */
int qam_order(const char *mod);

/* Gray-coded QAM modulation.
 * nbits must be a multiple of qm. Output symbols have unit average power. */
void qam_mod(const int *bits, int nbits, int qm, cx_t *syms);

/* Gray-coded QAM hard-decision demodulation. */
void qam_demod(const cx_t *syms, int nsym, int qm, int *bits);

/* Theoretical BER for Gray-coded square M-QAM over AWGN.
 * eb_n0_lin: Eb/N0 in linear scale (not dB).
 * Reference: Proakis & Salehi, "Digital Communications" 5th Ed. Eq.(5-2-79).
 * Exact for QPSK; tight approximation for 16/64/256QAM. */
double qam_ber_theory(int qm, double eb_n0_lin);

#endif
