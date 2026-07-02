#include "harq.h"
#include <stdlib.h>
#include <string.h>

void harq_init(HARQBuffer *h, int N_cb) {
    h->N_cb    = N_cb;
    h->llr_buf = (double *)calloc(N_cb, sizeof(double));
}

void harq_free(HARQBuffer *h) {
    free(h->llr_buf);
    h->llr_buf = NULL;
}

void harq_reset(HARQBuffer *h) {
    memset(h->llr_buf, 0, h->N_cb * sizeof(double));
}

/*
 * RV start positions.
 * For RV=0: start=0 (systematic bits → highest decodability at 1st Tx).
 * For RV=1,2,3: rotated by N_cb/4 increments (incremental parity).
 */
int harq_rv_start(int N_cb, int rv) {
    switch (rv) {
        case 0: return 0;
        case 1: return N_cb / 4;
        case 2: return N_cb / 2;
        case 3: return (3 * N_cb) / 4;
        default: return 0;
    }
}

void harq_rate_match(const int *coded, int N_cb, int E, int rv, int *out) {
    int start = harq_rv_start(N_cb, rv);
    for (int e = 0; e < E; e++)
        out[e] = coded[(start + e) % N_cb];
}

void harq_combine(HARQBuffer *h, const double *llr_rx, int E, int rv) {
    int start = harq_rv_start(h->N_cb, rv);
    for (int e = 0; e < E; e++)
        h->llr_buf[(start + e) % h->N_cb] += llr_rx[e];
}

const double *harq_get_buf(const HARQBuffer *h) {
    return h->llr_buf;
}
