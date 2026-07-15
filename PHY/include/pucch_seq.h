/* ================================================================
 *  pucch_seq.h
 *  Low-PAPR base sequences (ZC approx.) + cyclic shift for PUCCH F0/F1
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef PUCCH_SEQ_H
#define PUCCH_SEQ_H

#include "utils.h"

/* Zadoff-Chu-based low-PAPR base sequence, r(n) = exp(-j*pi*u*n*(n+1)/len).
 * Used by PUCCH Format 0/1. TS 38.211 Table 5.2.2.2-x defines specific
 * computer-generated sequences for length 12 (non-prime, so the pure ZC
 * formula isn't spec-exact there) -- this is an approximation with the
 * same low-PAPR/good-autocorrelation intent, not the literal table
 * (implementation-defined simplification). */
void pucch_base_sequence(int u, int len, cx_t *seq_out);

/* out[n] = seq[n] * exp(j*alpha*n) */
void pucch_cyclic_shift(const cx_t *seq, int len, double alpha, cx_t *out);

#endif
