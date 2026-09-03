/* ================================================================
 *  ldpc.c
 *  LDPC encoder/decoder (data channel)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "ldpc.h"
#include "ldpc_nr.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Large-magnitude LLR used to force filler-bit positions (see ldpc_nr.h)
 * to their known value (bit 0, this codebase's LLR>0 convention) at the
 * decoder -- comfortably dominates any realistic AWGN channel LLR. */
#define FILLER_FORCE_LLR 100.0

void ldpc_init(LDPCCodec *ldpc, int block_size, double code_rate) {
    int bg, Zc, Kb, base_rows, base_info_cols, base_cols, filler_size;
    nr_select_bg_zc(block_size, code_rate, &bg, &Zc, &Kb,
                     &base_rows, &base_info_cols, &base_cols, &filler_size);
    ldpc_init_resolved(ldpc, block_size, code_rate, bg, Zc, Kb,
                        base_rows, base_info_cols, base_cols, filler_size);
}

void ldpc_init_resolved(LDPCCodec *ldpc, int info_size, double code_rate,
                         int bg, int Zc, int Kb,
                         int base_rows, int base_info_cols, int base_cols,
                         int filler_size) {
    ldpc->info_size = info_size;
    ldpc->code_rate = code_rate;
    ldpc->bg = bg;
    ldpc->Zc = Zc;
    ldpc->Kb = Kb;
    ldpc->base_rows = base_rows;
    ldpc->base_info_cols = base_info_cols;
    ldpc->base_cols = base_cols;
    ldpc->filler_size = filler_size;
    ldpc->num_parity = base_rows * Zc;
    ldpc->coded_size = base_cols * Zc;
    build_H_nr(ldpc);
    ldpc_encode_prepare_nr(ldpc);
}

void ldpc_free(LDPCCodec *ldpc) {
    free(ldpc->H_row_ptr);  ldpc->H_row_ptr = NULL;
    free(ldpc->H_col);      ldpc->H_col     = NULL;
    free(ldpc->Ht_row_ptr); ldpc->Ht_row_ptr = NULL;
    free(ldpc->Ht_links);   ldpc->Ht_links  = NULL;
    free(ldpc->core_inv);   ldpc->core_inv  = NULL;
}

void ldpc_encode(const LDPCCodec *ldpc, const int *info, int *coded) {
    ldpc_encode_nr(ldpc, info, coded);
}

int ldpc_decode_soft(const LDPCCodec *ldpc, const double *llr,
                     int max_iter, int *decoded, double *posterior_llr_out) {
    int nc = ldpc->num_parity, nv = ldpc->coded_size;
    double *full_llr = (double *)malloc(nv * sizeof(double));
    double *q_edge   = (double *)malloc(ldpc->H_nnz * sizeof(double));
    double *c2v      = (double *)calloc(ldpc->H_nnz, sizeof(double));
    int    *hard     = (int    *)malloc(nv * sizeof(int));
    double *ch_llr   = (double *)malloc(nv * sizeof(double));

    /* filler bits (positions [info_size, base_info_cols*Zc), see ldpc_nr.h)
     * are known-perfect at the receiver -- force certain-bit-0 confidence
     * on a local copy rather than mutating the caller's llr[] (needed
     * as-is for e.g. pusch.c's turbo-equalization extrinsic = posterior
     * - llr). Legacy call sites with filler_size==0 are unaffected. */
    memcpy(ch_llr, llr, nv * sizeof(double));
    for (int i = ldpc->info_size; i < ldpc->base_info_cols * ldpc->Zc; i++)
        ch_llr[i] = FILLER_FORCE_LLR;

    for (int i = 0; i < nv; i++) full_llr[i] = ch_llr[i];

    int converged = 0;
    for (int iter = 0; iter < max_iter; iter++) {
        /* variable->check extrinsic message per edge: previous iteration's
         * full posterior minus THIS edge's own check->variable contribution
         * -- excluding the target check is required (q_{v->c} must not
         * include r_{c->v}) or the check-node update below self-feeds and
         * the algorithm is no longer belief propagation. */
        for (int p = 0; p < ldpc->H_nnz; p++)
            q_edge[p] = full_llr[ldpc->H_col[p]] - c2v[p];

        /* check node update */
        for (int c = 0; c < nc; c++) {
            int s = ldpc->H_row_ptr[c], e = ldpc->H_row_ptr[c+1];
            int deg = e - s;
            for (int i = 0; i < deg; i++) {
                double prod = 1.0;
                for (int j = 0; j < deg; j++) {
                    if (i == j) continue;
                    prod *= tanh(q_edge[s + j] / 2.0);
                }
                if (prod >  0.9999) prod =  0.9999;
                if (prod < -0.9999) prod = -0.9999;
                c2v[s + i] = 2.0 * atanh(prod);
            }
        }

        /* variable node update: fresh full posterior for this iteration,
         * also used as next iteration's extrinsic-subtraction base above */
        for (int v = 0; v < nv; v++) {
            double sum = ch_llr[v];
            for (int k = ldpc->Ht_row_ptr[v]; k < ldpc->Ht_row_ptr[v+1]; k++) {
                int ci  = ldpc->Ht_links[k].check_idx;
                int pic = ldpc->Ht_links[k].pos_in_check;
                sum += c2v[ldpc->H_row_ptr[ci] + pic];
            }
            full_llr[v] = sum;
            hard[v] = (sum < 0.0) ? 1 : 0;
        }

        /* syndrome early-stopping: H * hard^T == 0 (mod 2) for every check.
         * A satisfied syndrome is proof the current hard decision is a
         * valid codeword (though not necessarily the transmitted one). */
        int syn_ok = 1;
        for (int c = 0; c < nc && syn_ok; c++) {
            int par = 0;
            for (int p = ldpc->H_row_ptr[c]; p < ldpc->H_row_ptr[c+1]; p++)
                par ^= hard[ldpc->H_col[p]];
            if (par) syn_ok = 0;
        }
        if (syn_ok) { converged = 1; break; }
    }

    for (int i = 0; i < ldpc->info_size; i++)
        decoded[i] = (full_llr[i] < 0) ? 1 : 0;
    if (posterior_llr_out)
        memcpy(posterior_llr_out, full_llr, nv * sizeof(double));

    free(full_llr); free(q_edge); free(c2v); free(hard); free(ch_llr);
    return converged ? 0 : 1;
}

int ldpc_decode(const LDPCCodec *ldpc, const double *llr,
                int max_iter, int *decoded) {
    return ldpc_decode_soft(ldpc, llr, max_iter, decoded, NULL);
}
