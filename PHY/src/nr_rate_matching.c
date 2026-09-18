/* ================================================================
 *  nr_rate_matching.c
 *  TS 38.212 5.4.2.1-conformant LDPC circular-buffer rate matching --
 *  see nr_rate_matching.h.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "nr_rate_matching.h"
#include <assert.h>

int nr_ldpc_k0(int bg, int Zc, int Ncb, int rv) {
    long numer, denom;
    if (bg == 1) {
        switch (rv) {
            case 0:  return 0;
            case 1:  numer = 17L * Ncb; denom = 66L * Zc; break;
            case 2:  numer = 33L * Ncb; denom = 66L * Zc; break;
            default: numer = 56L * Ncb; denom = 66L * Zc; break;
        }
    } else {
        switch (rv) {
            case 0:  return 0;
            case 1:  numer = 13L * Ncb; denom = 50L * Zc; break;
            case 2:  numer = 25L * Ncb; denom = 50L * Zc; break;
            default: numer = 43L * Ncb; denom = 50L * Zc; break;
        }
    }
    /* integer division floors for non-negative operands -- exactly
     * TS 38.212's floor(...)*Zc, no floating-point rounding risk */
    return (int)((numer / denom) * Zc);
}

void nr_ldpc_er_alloc(int G, int Nl, int Qm, int C, int *E) {
    int Gp   = G / (Nl * Qm);
    int base = Gp / C;
    int rem  = Gp % C;
    for (int r = 0; r < C; r++) {
        int units = (r <= C - rem - 1) ? base : base + 1;
        E[r] = Nl * Qm * units;
    }
}

static int is_filler(int idx, int filler_start, int filler_end) {
    return idx >= filler_start && idx < filler_end;
}

void nr_ldpc_rate_match_select(const int *coded, int Ncb, int bg, int Zc,
                                int filler_start, int filler_end,
                                int rv, int e, int *out) {
    int k0 = nr_ldpc_k0(bg, Zc, Ncb, rv);
    int k = 0, j = 0;
    while (k < e) {
        int idx = (k0 + j) % Ncb;
        if (!is_filler(idx, filler_start, filler_end)) {
            out[k] = coded[idx];
            k++;
        }
        j++;
    }
}

void nr_ldpc_rate_match_select_soft(const double *buf, int Ncb, int bg, int Zc,
                                     int filler_start, int filler_end,
                                     int rv, int e, double *out) {
    int k0 = nr_ldpc_k0(bg, Zc, Ncb, rv);
    int k = 0, j = 0;
    while (k < e) {
        int idx = (k0 + j) % Ncb;
        if (!is_filler(idx, filler_start, filler_end)) {
            out[k] = buf[idx];
            k++;
        }
        j++;
    }
}

void nr_ldpc_rate_match_combine(double *soft_buf, int Ncb, int bg, int Zc,
                                 int filler_start, int filler_end,
                                 int rv, int e, const double *llr_in) {
    int k0 = nr_ldpc_k0(bg, Zc, Ncb, rv);
    int k = 0, j = 0;
    while (k < e) {
        int idx = (k0 + j) % Ncb;
        if (!is_filler(idx, filler_start, filler_end)) {
            soft_buf[idx] += llr_in[k];
            k++;
        }
        j++;
    }
}

/* All SCH wrappers share one iterator so Tx, Rx HARQ and turbo feedback
 * cannot silently disagree about puncture/filler/interleaver coordinates. */
typedef struct {
    int offset, ncb, pos, filler_start, filler_end, rows, qm, selected;
} SCHRateIterator;

static SCHRateIterator sch_iterator(int full_n, int bg, int z,
                                    int start, int end, int rv, int e, int qm) {
    assert(bg == 1 || bg == 2);
    assert(z > 0 && full_n == (bg == 1 ? 68 : 52) * z);
    assert(start >= 0 && start <= end && end <= full_n);
    assert(end - (start > 2*z ? start : 2*z) < full_n - 2*z);
    assert(rv >= 0 && rv <= 3);
    assert(qm == 1 || qm == 2 || qm == 4 || qm == 6 || qm == 8);
    assert(e >= 0 && e % qm == 0);
    SCHRateIterator it = {2*z, full_n-2*z, 0, start, end, e/qm, qm, 0};
    it.pos = nr_ldpc_k0(bg,z,it.ncb,rv);
    return it;
}

static int sch_next(SCHRateIterator *it, int *wire_index) {
    int full_index;
    do {
        full_index = it->offset + it->pos;
        it->pos = (it->pos + 1) % it->ncb;
    } while (is_filler(full_index,it->filler_start,it->filler_end));
    int k = it->selected++;
    *wire_index = (k % it->rows) * it->qm + k / it->rows;
    return full_index;
}

void nr_sch_rate_match_select(const int *coded, int full_n, int bg, int Zc,
                             int start, int end, int rv, int e, int Qm, int *out) {
    SCHRateIterator it = sch_iterator(full_n,bg,Zc,start,end,rv,e,Qm);
    for (int k=0; k<e; k++) {
        int wire;
        int idx = sch_next(&it,&wire);
        out[wire] = coded[idx];
    }
}

void nr_sch_rate_match_select_soft(const double *buf, int full_n, int bg, int Zc,
                                  int start, int end, int rv, int e, int Qm, double *out) {
    SCHRateIterator it = sch_iterator(full_n,bg,Zc,start,end,rv,e,Qm);
    for (int k=0; k<e; k++) {
        int wire;
        int idx = sch_next(&it,&wire);
        out[wire] = buf[idx];
    }
}

void nr_sch_rate_match_combine(double *soft_buf, int full_n, int bg, int Zc,
                              int start, int end, int rv, int e, int Qm, const double *llr_in) {
    SCHRateIterator it = sch_iterator(full_n,bg,Zc,start,end,rv,e,Qm);
    for (int k=0; k<e; k++) {
        int wire;
        int idx = sch_next(&it,&wire);
        soft_buf[idx] += llr_in[wire];
    }
}
