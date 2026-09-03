/* ================================================================
 *  nr_sch.c
 *  TS 38.212 5.2.2 code block segmentation, code block CRC
 *  attachment, and 5.5 code block concatenation (P0-2c) -- see
 *  nr_sch.h for scope.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "nr_sch.h"
#include "ldpc_nr.h"
#include "crc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void nr_seg_compute(int B, double code_rate, NRSegInfo *seg) {
    seg->B = B;

    nr_select_bg(B, code_rate, &seg->bg, &seg->Kcb, &seg->Kb,
                 &seg->base_rows, &seg->base_info_cols, &seg->base_cols);

    if (B <= seg->Kcb) {
        seg->C = 1;
        seg->L = 0;
        seg->Bp = B;
    } else {
        seg->L = 24;
        /* C = ceil(B / (Kcb - L)) */
        seg->C = (B + (seg->Kcb - seg->L) - 1) / (seg->Kcb - seg->L);
        seg->Bp = B + seg->C * seg->L;
    }

    if (seg->Bp % seg->C != 0) {
        /* See nr_seg_compute()'s doc comment in nr_sch.h -- this specific
         * B does not divide evenly under the K'=B'/C reading confirmed
         * from the primary source, and no rounding rule for this case
         * was established. Refusing to guess rather than silently
         * rounding to a possibly-wrong K'. */
        fprintf(stderr,
                "nr_sch: B=%d code_rate=%.4f -> BG%d C=%d B'=%d does not "
                "divide evenly (B'%%C=%d) -- TS 38.212 5.2.2's K'=B'/C "
                "rounding for this case is not confirmed (see nr_sch.h "
                "nr_seg_compute() doc comment). Not supported.\n",
                B, code_rate, seg->bg, seg->C, seg->Bp, seg->Bp % seg->C);
        exit(1);
    }
    seg->Kprime = seg->Bp / seg->C;

    nr_select_zc(seg->Kb, seg->Kprime, seg->base_info_cols,
                 &seg->Zc, &seg->K, &seg->filler_size);
}

void nr_seg_split(const int *in, const NRSegInfo *seg, int *cb_bits) {
    int C = seg->C, L = seg->L, Kprime = seg->Kprime;
    int payload_bits = Kprime - L;   /* raw bits per block, before CB CRC */
    int s = 0;                       /* running index into in[seg->B] */

    for (int r = 0; r < C; r++) {
        int *cb = cb_bits + (size_t)r * Kprime;
        for (int k = 0; k < payload_bits; k++)
            cb[k] = in[s++];
        if (C > 1)
            compute_crc(cb, payload_bits, CRC24B, cb + payload_bits);
    }
}

void nr_seg_concat(const NRSegInfo *seg, const int *const *rm, const int *E, int *out) {
    int off = 0;
    for (int r = 0; r < seg->C; r++) {
        memcpy(out + off, rm[r], (size_t)E[r] * sizeof(int));
        off += E[r];
    }
}
