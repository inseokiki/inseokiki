#ifndef LDPC_H
#define LDPC_H

typedef struct { int check_idx; int pos_in_check; } VarLink;

typedef struct {
    int    info_size;
    int    coded_size;
    double code_rate;
    int    num_parity;

    /* H in CSR (check -> variable) */
    int *H_row_ptr;   /* length: num_parity+1 */
    int *H_col;       /* variable indices */
    int  H_nnz;

    /* Ht in CSR (variable -> check) */
    int     *Ht_row_ptr;  /* length: coded_size+1 */
    VarLink *Ht_links;
    int      Ht_nnz;
} LDPCCodec;

void ldpc_init(LDPCCodec *ldpc, int block_size, double code_rate);
void ldpc_free(LDPCCodec *ldpc);

/* encode: info_bits[info_size] -> coded[coded_size] */
void ldpc_encode(const LDPCCodec *ldpc, const int *info, int *coded);

/* decode: llr[coded_size] -> decoded[info_size].  Returns 0 on success. */
int  ldpc_decode(const LDPCCodec *ldpc, const double *llr,
                 int max_iter, int *decoded);

#endif
