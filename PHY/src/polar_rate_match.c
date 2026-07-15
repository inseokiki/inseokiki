/* ================================================================
 *  polar_rate_match.c
 *  Polar rate matching/dematching (shortening/puncturing/repetition)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "polar_rate_match.h"
#include <stdlib.h>
#include <string.h>

static const int sub_block_pattern[32] = {
     0,  1,  2,  4,  3,  5,  6,  7,
     8, 16,  9, 17, 10, 18, 11, 19,
    12, 20, 13, 21, 14, 22, 15, 23,
    24, 25, 26, 28, 27, 29, 30, 31
};

/* Build pi[N]: sub-block interleaver indices for size N (N must be >= 32, power of 2) */
void polar_interleaver(int N, int *pi) {
    int ratio = N / 32;
    int idx   = 0;
    for (int i = 0; i < 32; i++) {
        int base = sub_block_pattern[i] * ratio;
        for (int j = 0; j < ratio; j++)
            pi[idx++] = base + j;
    }
}

void polar_rate_match(const int *coded, int N, int E, int K, int *out) {
    int *interleaved = (int *)malloc(N * sizeof(int));
    if (N >= 32) {
        int *pi = (int *)malloc(N * sizeof(int));
        polar_interleaver(N, pi);
        for (int i = 0; i < N; i++) interleaved[i] = coded[pi[i]];
        free(pi);
    } else {
        memcpy(interleaved, coded, N * sizeof(int));
    }

    if (E >= N) {
        for (int i = 0; i < E; i++) out[i] = interleaved[i % N];
    } else {
        double r = (double)K / E;
        if (r <= 7.0 / 16.0) {
            /* puncturing: last E bits */
            for (int i = 0; i < E; i++) out[i] = interleaved[N - E + i];
        } else {
            /* shortening: first E bits */
            for (int i = 0; i < E; i++) out[i] = interleaved[i];
        }
    }
    free(interleaved);
}

void polar_rate_dematch(const double *llr_in, int E, int N, int K,
                        double *llr_out) {
    double *deinterleaved = (double *)calloc(N, sizeof(double));

    if (E >= N) {
        for (int i = 0; i < E; i++) deinterleaved[i % N] += llr_in[i];
    } else {
        double r = (double)K / E;
        if (r <= 7.0 / 16.0) {
            for (int i = 0; i < E; i++) deinterleaved[N - E + i] = llr_in[i];
        } else {
            for (int i = 0; i < E; i++) deinterleaved[i] = llr_in[i];
            for (int i = E; i < N; i++) deinterleaved[i] = 100.0;
        }
    }

    if (N >= 32) {
        int *pi = (int *)malloc(N * sizeof(int));
        polar_interleaver(N, pi);
        for (int i = 0; i < N; i++) llr_out[pi[i]] = deinterleaved[i];
        free(pi);
    } else {
        memcpy(llr_out, deinterleaved, N * sizeof(double));
    }
    free(deinterleaved);
}
