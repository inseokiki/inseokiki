/* ================================================================
 *  ldpc.c
 *  LDPC encoder/decoder (data channel)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "ldpc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void build_H(LDPCCodec *ldpc) {
    int nc = ldpc->num_parity;
    int ni = ldpc->info_size;
    int nv = ldpc->coded_size;
    int cw = 3;  /* column weight for info bits */

    /* count connections per check node */
    int *cnt = (int *)calloc(nc, sizeof(int));
    for (int i = 0; i < ni; i++)
        for (int j = 0; j < cw; j++)
            cnt[(i * cw + j) % nc]++;
    for (int i = 0; i < nc; i++) cnt[i]++;  /* diagonal parity bit */

    /* H row pointers */
    ldpc->H_nnz = 0;
    ldpc->H_row_ptr = (int *)malloc((nc + 1) * sizeof(int));
    ldpc->H_row_ptr[0] = 0;
    for (int c = 0; c < nc; c++) {
        ldpc->H_row_ptr[c+1] = ldpc->H_row_ptr[c] + cnt[c];
        ldpc->H_nnz += cnt[c];
    }
    ldpc->H_col = (int *)malloc(ldpc->H_nnz * sizeof(int));

    /* fill H_col */
    memset(cnt, 0, nc * sizeof(int));
    for (int i = 0; i < ni; i++) {
        for (int j = 0; j < cw; j++) {
            int ci = (i * cw + j) % nc;
            ldpc->H_col[ldpc->H_row_ptr[ci] + cnt[ci]++] = i;
        }
    }
    for (int i = 0; i < nc; i++)
        ldpc->H_col[ldpc->H_row_ptr[i] + cnt[i]++] = ni + i;
    free(cnt);

    /* build Ht (variable -> check) */
    int *vc = (int *)calloc(nv, sizeof(int));
    for (int c = 0; c < nc; c++)
        for (int p = ldpc->H_row_ptr[c]; p < ldpc->H_row_ptr[c+1]; p++)
            vc[ldpc->H_col[p]]++;

    ldpc->Ht_row_ptr = (int *)malloc((nv + 1) * sizeof(int));
    ldpc->Ht_row_ptr[0] = 0;
    ldpc->Ht_nnz = 0;
    for (int v = 0; v < nv; v++) {
        ldpc->Ht_row_ptr[v+1] = ldpc->Ht_row_ptr[v] + vc[v];
        ldpc->Ht_nnz += vc[v];
    }
    ldpc->Ht_links = (VarLink *)malloc(ldpc->Ht_nnz * sizeof(VarLink));

    memset(vc, 0, nv * sizeof(int));
    for (int c = 0; c < nc; c++) {
        for (int p = ldpc->H_row_ptr[c]; p < ldpc->H_row_ptr[c+1]; p++) {
            int v   = ldpc->H_col[p];
            int pic = p - ldpc->H_row_ptr[c];
            int slot = ldpc->Ht_row_ptr[v] + vc[v]++;
            ldpc->Ht_links[slot].check_idx     = c;
            ldpc->Ht_links[slot].pos_in_check  = pic;
        }
    }
    free(vc);
}

void ldpc_init(LDPCCodec *ldpc, int block_size, double code_rate) {
    ldpc->info_size  = block_size;
    ldpc->code_rate  = code_rate;
    ldpc->num_parity = (int)(block_size * (1.0 / code_rate - 1.0));
    ldpc->coded_size = block_size + ldpc->num_parity;
    build_H(ldpc);
}

void ldpc_free(LDPCCodec *ldpc) {
    free(ldpc->H_row_ptr);  ldpc->H_row_ptr = NULL;
    free(ldpc->H_col);      ldpc->H_col     = NULL;
    free(ldpc->Ht_row_ptr); ldpc->Ht_row_ptr = NULL;
    free(ldpc->Ht_links);   ldpc->Ht_links  = NULL;
}

void ldpc_encode(const LDPCCodec *ldpc, const int *info, int *coded) {
    int ni = ldpc->info_size, nc = ldpc->num_parity;
    for (int i = 0; i < ni; i++) coded[i] = info[i];
    for (int i = 0; i < nc; i++) {
        int p = 0;
        for (int pos = ldpc->H_row_ptr[i]; pos < ldpc->H_row_ptr[i+1]; pos++) {
            int v = ldpc->H_col[pos];
            if (v < ni) p ^= info[v];
        }
        coded[ni + i] = p;
    }
}

int ldpc_decode_soft(const LDPCCodec *ldpc, const double *llr,
                     int max_iter, int *decoded, double *posterior_llr_out) {
    int nc = ldpc->num_parity, nv = ldpc->coded_size;
    double *v2c = (double *)malloc(nv * sizeof(double));
    double *c2v = (double *)calloc(ldpc->H_nnz, sizeof(double));

    for (int i = 0; i < nv; i++) v2c[i] = llr[i];

    for (int iter = 0; iter < max_iter; iter++) {
        /* check node update */
        for (int c = 0; c < nc; c++) {
            int s = ldpc->H_row_ptr[c], e = ldpc->H_row_ptr[c+1];
            int deg = e - s;
            for (int i = 0; i < deg; i++) {
                double prod = 1.0;
                for (int j = 0; j < deg; j++) {
                    if (i == j) continue;
                    double val = v2c[ldpc->H_col[s + j]];
                    prod *= tanh(val / 2.0);
                }
                if (prod >  0.9999) prod =  0.9999;
                if (prod < -0.9999) prod = -0.9999;
                c2v[s + i] = 2.0 * atanh(prod);
            }
        }
        /* variable node update */
        for (int v = 0; v < nv; v++) {
            double sum = llr[v];
            for (int k = ldpc->Ht_row_ptr[v]; k < ldpc->Ht_row_ptr[v+1]; k++) {
                int ci  = ldpc->Ht_links[k].check_idx;
                int pic = ldpc->Ht_links[k].pos_in_check;
                sum += c2v[ldpc->H_row_ptr[ci] + pic];
            }
            v2c[v] = sum;
        }
    }

    for (int i = 0; i < ldpc->info_size; i++)
        decoded[i] = (v2c[i] < 0) ? 1 : 0;
    if (posterior_llr_out)
        memcpy(posterior_llr_out, v2c, nv * sizeof(double));

    free(v2c); free(c2v);
    return 0;
}

int ldpc_decode(const LDPCCodec *ldpc, const double *llr,
                int max_iter, int *decoded) {
    return ldpc_decode_soft(ldpc, llr, max_iter, decoded, NULL);
}
