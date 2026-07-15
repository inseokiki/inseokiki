/* ================================================================
 *  dft_precode.c
 *  Unitary DFT/IDFT for PUSCH transform precoding (DFT-s-OFDM)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "dft_precode.h"
#include <math.h>

void dft_precode(const cx_t *in, int M, cx_t *out) {
    double inv_sqrtM = 1.0 / sqrt((double)M);
    for (int k = 0; k < M; k++) {
        cx_t sum = CX_ZERO;
        for (int n = 0; n < M; n++) {
            double phase = -2.0 * PHY_PI * (double)k * (double)n / (double)M;
            sum += in[n] * CX_MAKE(cos(phase), sin(phase));
        }
        out[k] = sum * inv_sqrtM;
    }
}

void idft_precode(const cx_t *in, int M, cx_t *out) {
    double inv_sqrtM = 1.0 / sqrt((double)M);
    for (int n = 0; n < M; n++) {
        cx_t sum = CX_ZERO;
        for (int k = 0; k < M; k++) {
            double phase = 2.0 * PHY_PI * (double)k * (double)n / (double)M;
            sum += in[k] * CX_MAKE(cos(phase), sin(phase));
        }
        out[n] = sum * inv_sqrtM;
    }
}
