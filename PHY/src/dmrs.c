/* ================================================================
 *  dmrs.c
 *  DMRS pilot sequence generation and RE index mapping
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "dmrs.h"
#include <math.h>
#include <stdlib.h>

void gold_sequence(uint32_t c_init, int length, int *out) {
    uint32_t x1 = 1u;
    uint32_t x2 = c_init & 0x7FFFFFFFu;
    for (int i = 0; i < 1600; i++) {
        uint32_t b1 = ((x1 >> 3) ^ x1) & 1u;
        x1 = (x1 >> 1) | (b1 << 30);
        uint32_t b2 = ((x2 >> 3) ^ (x2 >> 2) ^ (x2 >> 1) ^ x2) & 1u;
        x2 = (x2 >> 1) | (b2 << 30);
    }
    for (int i = 0; i < length; i++) {
        out[i] = (int)((x1 ^ x2) & 1u);
        uint32_t b1 = ((x1 >> 3) ^ x1) & 1u;
        x1 = (x1 >> 1) | (b1 << 30);
        uint32_t b2 = ((x2 >> 3) ^ (x2 >> 2) ^ (x2 >> 1) ^ x2) & 1u;
        x2 = (x2 >> 1) | (b2 << 30);
    }
}

void dmrs_sequence(uint32_t c_init, int num_pilots, cx_t *out) {
    int *c = (int *)malloc(2 * num_pilots * sizeof(int));
    gold_sequence(c_init, 2 * num_pilots, c);
    double sc = 1.0 / sqrt(2.0);
    for (int n = 0; n < num_pilots; n++)
        out[n] = CX_MAKE(sc * (1 - 2*c[2*n]), sc * (1 - 2*c[2*n+1]));
    free(c);
}

void dmrs_pilot_indices(int num_rb, int *out) {
    int idx = 0;
    for (int rb = 0; rb < num_rb; rb++) {
        int base = rb * 12;
        for (int k = 0; k < 12; k += 2) out[idx++] = base + k;
    }
}

void dmrs_data_indices(int num_rb, int *out) {
    int idx = 0;
    for (int rb = 0; rb < num_rb; rb++) {
        int base = rb * 12;
        for (int k = 1; k < 12; k += 2) out[idx++] = base + k;
    }
}
