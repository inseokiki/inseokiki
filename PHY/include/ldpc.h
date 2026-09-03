/* ================================================================
 *  ldpc.h
 *  LDPC encoder/decoder (data channel)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef LDPC_H
#define LDPC_H

#include <stdint.h>

typedef struct { int check_idx; int pos_in_check; } VarLink;

typedef struct {
    int    info_size;    /* == block_size argument to ldpc_init(), K incl. outer 24-bit
                             CRC as every caller in this project attaches it -- unchanged
                             meaning whether the legacy or NR-conformant path is active */
    int    coded_size;   /* legacy path: info_size+num_parity. NR path (2026-09-02,
                             see ldpc_nr.h): base_cols*Zc, the full lifted "mother"
                             codeword length (systematic incl. filler + all parity) --
                             standard rate matching to shorten this to E bits is a
                             separate follow-up module (P0-3), not done here */
    double code_rate;    /* echoed back, not read by anything outside ldpc.c/ldpc_nr.c */
    int    num_parity;   /* legacy: coded_size-info_size. NR path: base_rows*Zc --
                             NOTE this is no longer coded_size-info_size once filler
                             bits exist (coded_size = info_size + filler_size +
                             num_parity); safe because nothing external reads this
                             field directly (verified: only .info_size/.coded_size are
                             read by any of the 53+ call sites across the project) */

    /* NR path only (2026-09-02) -- see ldpc_nr.h for the selection/construction
       algorithms. Left at 0 on the legacy (non-NR) path, unused there. */
    int bg;               /* 1 or 2 */
    int Zc;                /* chosen lifting size */
    int Kb;                /* BG2 Zc-selection threshold param (22 always for BG1) */
    int base_rows;          /* 46 (BG1) or 42 (BG2), unlifted */
    int base_info_cols;     /* 22 (BG1) or 10 (BG2), unlifted */
    int base_cols;          /* base_info_cols+base_rows: 68 (BG1) or 52 (BG2) */
    int filler_size;        /* base_info_cols*Zc - info_size, always >= 0 */
    uint64_t *core_inv;      /* GF(2) inverse of the 4*Zc x 4*Zc "core" system
                                (first 4 parity base-columns), bit-packed, n=4*Zc
                                rows of ceil(n/64) words each -- computed once by
                                ldpc_encode_prepare_nr(), reused by every
                                ldpc_encode_nr() call for this codec instance */

    /* H in CSR (check -> variable). Legacy path: build_H()'s modulo-based
       toy matrix. NR path: build_H_nr()'s lifted QC-LDPC matrix. Either
       way, ldpc_decode_soft()'s edge-message BP loop (fixed 2026-09-02)
       consumes these fields identically -- no decoder changes needed. */
    int *H_row_ptr;   /* length: num_parity+1 */
    int *H_col;       /* variable indices */
    int  H_nnz;

    /* Ht in CSR (variable -> check) */
    int     *Ht_row_ptr;  /* length: coded_size+1 */
    VarLink *Ht_links;
    int      Ht_nnz;
} LDPCCodec;

void ldpc_init(LDPCCodec *ldpc, int block_size, double code_rate);

/* Initializes an LDPCCodec from already-resolved TS 38.212 5.2.2/6.2.2
 * parameters, skipping ldpc_init()'s single-code-block nr_select_bg_zc()
 * search. Used by nr_sch.c (P0-2c multi-code-block segmentation): BG/Kb/Zc
 * are resolved once at the transport-block level (nr_select_bg() +
 * nr_select_zc(), see ldpc_nr.h) and shared by every code block of that
 * TB, so info_size here is Kprime (this code block's info bits, incl. its
 * own CRC24B if segmented, excl. filler) rather than the TB size. */
void ldpc_init_resolved(LDPCCodec *ldpc, int info_size, double code_rate,
                         int bg, int Zc, int Kb,
                         int base_rows, int base_info_cols, int base_cols,
                         int filler_size);

void ldpc_free(LDPCCodec *ldpc);

/* encode: info_bits[info_size] -> coded[coded_size] */
void ldpc_encode(const LDPCCodec *ldpc, const int *info, int *coded);

/* decode: llr[coded_size] -> decoded[info_size], belief propagation with
   syndrome-based early stopping (2026-09-02). Returns 0 if H*hard^T==0 was
   reached within max_iter (hard decision is a valid codeword, though not
   necessarily the transmitted one -- caller should still check CRC), 1 if
   max_iter was exhausted without a satisfied syndrome. decoded[] holds the
   best-effort hard decision in both cases. */
int  ldpc_decode(const LDPCCodec *ldpc, const double *llr,
                 int max_iter, int *decoded);

/* Same as ldpc_decode(), but also writes the belief-propagation posterior
   LLR for every coded bit (coded_size, systematic+parity) to
   posterior_llr_out (pass NULL to skip -- ldpc_decode() does this).
   Used for turbo equalization: extrinsic[v] = posterior_llr_out[v] - llr[v]. */
int  ldpc_decode_soft(const LDPCCodec *ldpc, const double *llr,
                      int max_iter, int *decoded, double *posterior_llr_out);

#endif
