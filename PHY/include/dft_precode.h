/* ================================================================
 *  dft_precode.h
 *  Unitary DFT/IDFT for PUSCH transform precoding (DFT-s-OFDM)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef DFT_PRECODE_H
#define DFT_PRECODE_H

#include "utils.h"

/* Unitary M-point DFT/IDFT for PUSCH transform precoding (TS 38.211
 * Sec 6.3.1.4). Direct O(M^2) summation -- M need not be a power of 2
 * (3GPP requires M to be a product of {2,3,5}, but this simplified LLS
 * already uses a direct DFT elsewhere for the same reason: no FFTW
 * dependency). Both directions scaled by 1/sqrt(M) so power is exactly
 * preserved and idft_precode(dft_precode(x)) == x. */
void dft_precode (const cx_t *in, int M, cx_t *out);
void idft_precode(const cx_t *in, int M, cx_t *out);

#endif
