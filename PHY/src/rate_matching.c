/* ================================================================
 *  rate_matching.c
 *  Circular-buffer HARQ rate matching (RV offsets) + soft combining
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "rate_matching.h"
#include <math.h>

int rv_start_offset(int rv, int ncb) {
    return (int)((double)rv / 4.0 * ncb + 0.5);
}

void rate_match_select(const int *coded, int ncb, int rv, int e, int *out) {
    int k0 = rv_start_offset(rv, ncb);
    for (int i = 0; i < e; i++)
        out[i] = coded[(k0 + i) % ncb];
}

void rate_match_combine(double *soft_buf, int ncb, int rv, int e,
                        const double *llr_in) {
    int k0 = rv_start_offset(rv, ncb);
    for (int i = 0; i < e; i++)
        soft_buf[(k0 + i) % ncb] += llr_in[i];
}
