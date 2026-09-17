#ifndef UL_CODEBOOK_4PORT_H
#define UL_CODEBOOK_4PORT_H

#include "utils.h"

/* TS 38.211 V17.10.0 section 6.3.1.5, Tables 6.3.1.5-3/-5/-6/-7.
 * Full four-port TPMI sets: rank 1: 0..27, rank 2: 0..21,
 * rank 3: 0..6, rank 4: 0..4. W is the exact table matrix before
 * the simulator's fixed-total-power scaling. Returns 0 on success. */
int ul_cb4_precoder(int rank, int tpmi, cx_t W[4][4]);
int ul_cb4_tpmi_count(int rank);
double ul_cb4_unit_power_beta(const cx_t W[4][4], int rank);

/* Choose RI/TPMI by summed post-MMSE capacity over all four-port TPMI.
 * The simulator applies beta=1/sqrt(sum|W|^2) after the standard W so every
 * candidate has unit total transmit power. This beta is a link-level power
 * normalization assumption, not a replacement for NR PUSCH power control. */
void ul_cb4_select(const cx_t H[4][4], double N0, int *rank, int *tpmi);

#endif
