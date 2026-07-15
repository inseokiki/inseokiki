/* ================================================================
 *  pucch_seq.c
 *  Low-PAPR base sequences (ZC approx.) + cyclic shift for PUCCH F0/F1
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "pucch_seq.h"
#include <math.h>

void pucch_base_sequence(int u, int len, cx_t *seq_out) {
    for (int n = 0; n < len; n++) {
        double phase = -PHY_PI * (double)u * (double)n * (double)(n + 1) / (double)len;
        seq_out[n] = CX_MAKE(cos(phase), sin(phase));
    }
}

void pucch_cyclic_shift(const cx_t *seq, int len, double alpha, cx_t *out) {
    for (int n = 0; n < len; n++) {
        double phase = alpha * (double)n;
        out[n] = seq[n] * CX_MAKE(cos(phase), sin(phase));
    }
}
