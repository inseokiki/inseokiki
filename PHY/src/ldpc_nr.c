/* ================================================================
 *  ldpc_nr.c
 *  TS 38.212-conformant NR LDPC internals -- see ldpc_nr.h.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "ldpc_nr.h"
#include "ldpc_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define GF2_WORDS(nbits) (((nbits) + 63) / 64)
static inline int gf2_bit(const uint64_t *row, int bit) {
    return (int)((row[bit >> 6] >> (bit & 63)) & 1ULL);
}
static inline void gf2_set_bit(uint64_t *row, int bit) {
    row[bit >> 6] |= (1ULL << (bit & 63));
}
static inline void gf2_row_xor(uint64_t *dst, const uint64_t *src, int nwords) {
    for (int i = 0; i < nwords; i++) dst[i] ^= src[i];
}

/* Which of the 8 TS 38.212 Table 5.3.2-1 lifting-size sets contains Zc
 * (Zc must be a value nr_select_bg_zc() actually chose, i.e. always
 * present in exactly one set). */
static int zc_to_ils(int Zc) {
    for (int s = 0; s < ZC_NUM_SETS; s++)
        for (int i = 0; i < ZC_SET_LEN[s]; i++)
            if (ZC_SETS[s][i] == Zc) return s;
    return -1;   /* unreachable given nr_select_bg_zc()'s contract */
}

void nr_select_bg_zc(int block_size, double code_rate,
                      int *bg, int *Zc, int *Kb,
                      int *base_rows, int *base_info_cols, int *base_cols,
                      int *filler_size) {
    /* A = payload bits before the outer CRC. This project attaches a
     * fixed 24-bit CRC24A everywhere (crc_bits=24 hardcoded at every
     * call site), unlike the spec's conditional CRC16(A<=3824)/CRC24A
     * split -- see ldpc_nr.h scope note / plan Risk list: this narrows
     * the reachable K' range slightly right at A~3824 but no existing
     * caller gets anywhere near that boundary (all <=~8424 total, and
     * low-rate MCS entries -- the only way to reach BG2 -- always pair
     * with low modulation order, keeping A far below the boundary in
     * practice). */
    double A = (double)block_size - 24.0;
    double R = code_rate;

    if (A <= 292.0 || (A <= 3824.0 && R <= 0.67) || R <= 0.25) {
        *bg = 2;
        *base_rows = BG2_ROWS; *base_info_cols = BG2_INFO_COLS; *base_cols = BG2_COLS;
        if (block_size > 640)      *Kb = 10;
        else if (block_size > 560) *Kb = 9;
        else if (block_size > 192) *Kb = 8;
        else                       *Kb = 6;
    } else {
        *bg = 1;
        *base_rows = BG1_ROWS; *base_info_cols = BG1_INFO_COLS; *base_cols = BG1_COLS;
        *Kb = 22;
    }

    /* minimum Zc (over all 51 values across the 8 sets) with Kb*Zc >= block_size */
    int best_zc = -1;
    for (int s = 0; s < ZC_NUM_SETS; s++) {
        for (int i = 0; i < ZC_SET_LEN[s]; i++) {
            int zc = ZC_SETS[s][i];
            if ((long)(*Kb) * zc >= block_size) {
                if (best_zc < 0 || zc < best_zc) best_zc = zc;
            }
        }
    }
    if (best_zc < 0) {
        fprintf(stderr,
                "ldpc_nr: block_size=%d code_rate=%.4f selected BG%d (Kb=%d) but no "
                "lifting size Zc<=384 satisfies Kb*Zc>=block_size -- this would require "
                "TS 38.212 5.2.2 multi-code-block segmentation (BG%d Kcb=%d), which is "
                "out of scope for this simulator (see tasks/todo.md P0-2c). Not supported.\n",
                block_size, code_rate, *bg, *Kb, *bg, *bg == 1 ? 8448 : 3840);
        exit(1);
    }
    *Zc = best_zc;
    *filler_size = (*base_info_cols) * (*Zc) - block_size;
}

void build_H_nr(LDPCCodec *ldpc) {
    int Zc = ldpc->Zc;
    int nc = ldpc->num_parity;   /* base_rows*Zc */
    int nv = ldpc->coded_size;   /* base_cols*Zc */
    int iLS = zc_to_ils(Zc);
    const BGEntry *table = (ldpc->bg == 1) ? BG1_TABLE : BG2_TABLE;
    int num_entries = (ldpc->bg == 1) ? BG1_NUM_ENTRIES : BG2_NUM_ENTRIES;

    ldpc->H_nnz = num_entries * Zc;

    /* pass 1: count lifted nonzeros per lifted row (check-major CSR,
     * same two-pass count-then-fill pattern as ldpc.c's legacy build_H()) */
    int *cnt = (int *)calloc(nc, sizeof(int));
    for (int e = 0; e < num_entries; e++) {
        int base_row = table[e].row;
        for (int k = 0; k < Zc; k++) cnt[base_row * Zc + k]++;
    }
    ldpc->H_row_ptr = (int *)malloc((nc + 1) * sizeof(int));
    ldpc->H_row_ptr[0] = 0;
    for (int r = 0; r < nc; r++) ldpc->H_row_ptr[r + 1] = ldpc->H_row_ptr[r] + cnt[r];

    /* pass 2: fill H_col. TS 38.212 5.3.2: circular permutation matrix =
     * identity I shifted RIGHT by (table shift mod Zc) -- within a lifted
     * block, block-row k's single 1 lands at block-column (k+s) mod Zc. */
    ldpc->H_col = (int *)malloc(ldpc->H_nnz * sizeof(int));
    memset(cnt, 0, nc * sizeof(int));
    for (int e = 0; e < num_entries; e++) {
        int base_row = table[e].row, base_col = table[e].col;
        int s = table[e].shift[iLS] % Zc;
        for (int k = 0; k < Zc; k++) {
            int lifted_row = base_row * Zc + k;
            int lifted_col = base_col * Zc + (k + s) % Zc;
            ldpc->H_col[ldpc->H_row_ptr[lifted_row] + cnt[lifted_row]++] = lifted_col;
        }
    }
    free(cnt);

    /* Ht (variable-major adjacency), identical construction to the
     * existing ldpc.c build_H() -- unchanged logic, bigger nc/nv/nnz.
     * This is what lets ldpc_decode_soft()'s edge-message BP loop
     * (fixed 2026-09-02, P0-2) consume the lifted matrix with zero
     * algorithmic changes. */
    int *vc = (int *)calloc(nv, sizeof(int));
    for (int c = 0; c < nc; c++)
        for (int p = ldpc->H_row_ptr[c]; p < ldpc->H_row_ptr[c + 1]; p++)
            vc[ldpc->H_col[p]]++;

    ldpc->Ht_row_ptr = (int *)malloc((nv + 1) * sizeof(int));
    ldpc->Ht_row_ptr[0] = 0;
    ldpc->Ht_nnz = 0;
    for (int v = 0; v < nv; v++) {
        ldpc->Ht_row_ptr[v + 1] = ldpc->Ht_row_ptr[v] + vc[v];
        ldpc->Ht_nnz += vc[v];
    }
    ldpc->Ht_links = (VarLink *)malloc(ldpc->Ht_nnz * sizeof(VarLink));

    memset(vc, 0, nv * sizeof(int));
    for (int c = 0; c < nc; c++) {
        for (int p = ldpc->H_row_ptr[c]; p < ldpc->H_row_ptr[c + 1]; p++) {
            int v   = ldpc->H_col[p];
            int pic = p - ldpc->H_row_ptr[c];
            int slot = ldpc->Ht_row_ptr[v] + vc[v]++;
            ldpc->Ht_links[slot].check_idx    = c;
            ldpc->Ht_links[slot].pos_in_check = pic;
        }
    }
    free(vc);
}

/* ── Structured NR LDPC encoding ──────────────────────────────────────
 *
 * Empirically confirmed (see ldpc_tables.h provenance note, verified
 * directly against the transcribed table -- not assumed from memory)
 * for BOTH BG1 and BG2: among the base_rows parity base-columns,
 * columns [4..base_rows-1] each have degree exactly 1 (connect to
 * exactly base row 4+i, i.e. their OWN row, always at shift 0 -- a
 * literal identity block), and base-graph rows [0..3] never reference
 * those columns. This means:
 *   - Rows 0..3 ("core") form a small, self-contained circulant-block
 *     system in the 4 unknowns p0..p3 (each a Zc-bit block) -- solved
 *     once via ldpc_encode_prepare_nr()'s cached GF(2) inverse.
 *   - Rows 4..base_rows-1 ("outer") each solve directly by moving
 *     every other (known) term to the RHS -- no further linear algebra,
 *     since their own diagonal column is a plain identity (shift 0).
 * This is the standard NR/5G "Richardson-Urbanke-style" LDPC encoding
 * structure, discovered here from the actual transcribed table rather
 * than hardcoded from literature, per the reviewed implementation plan. */

void ldpc_encode_prepare_nr(LDPCCodec *ldpc) {
    int Zc = ldpc->Zc;
    int n  = 4 * Zc;
    int nw = GF2_WORDS(n);
    int aw = GF2_WORDS(2 * n);
    int iLS = zc_to_ils(Zc);
    int info_cols = ldpc->base_info_cols;
    const BGEntry *table = (ldpc->bg == 1) ? BG1_TABLE : BG2_TABLE;
    int num_entries = (ldpc->bg == 1) ? BG1_NUM_ENTRIES : BG2_NUM_ENTRIES;

    /* augmented [M | I], n rows x 2n bits (M = core coefficient matrix,
     * I = identity -- Gauss-Jordan reduces this to [I | M^-1]) */
    uint64_t *aug = (uint64_t *)calloc((size_t)n * aw, sizeof(uint64_t));
    #define AROW(i) (aug + (size_t)(i) * aw)

    for (int e = 0; e < num_entries; e++) {
        int row = table[e].row;
        if (row >= 4) continue;
        int col = table[e].col;
        if (col < info_cols || col >= info_cols + 4) continue;
        int j = col - info_cols;
        int s = table[e].shift[iLS] % Zc;
        for (int k = 0; k < Zc; k++)
            gf2_set_bit(AROW(row * Zc + k), j * Zc + (k + s) % Zc);
    }
    for (int i = 0; i < n; i++) gf2_set_bit(AROW(i), n + i);

    for (int col = 0; col < n; col++) {
        int piv = -1;
        for (int r = col; r < n; r++) if (gf2_bit(AROW(r), col)) { piv = r; break; }
        if (piv < 0) {
            fprintf(stderr,
                    "ldpc_nr: core system singular at column %d (bg=%d Zc=%d) -- "
                    "unexpected for a valid NR LDPC base graph, aborting.\n",
                    col, ldpc->bg, Zc);
            exit(1);
        }
        if (piv != col) {
            for (int w = 0; w < aw; w++) {
                uint64_t t = AROW(piv)[w]; AROW(piv)[w] = AROW(col)[w]; AROW(col)[w] = t;
            }
        }
        for (int r = 0; r < n; r++)
            if (r != col && gf2_bit(AROW(r), col)) gf2_row_xor(AROW(r), AROW(col), aw);
    }

    ldpc->core_inv = (uint64_t *)calloc((size_t)n * nw, sizeof(uint64_t));
    for (int i = 0; i < n; i++)
        for (int b = 0; b < n; b++)
            if (gf2_bit(AROW(i), n + b)) gf2_set_bit(&ldpc->core_inv[(size_t)i * nw], b);

    #undef AROW
    free(aug);
}

void ldpc_encode_nr(const LDPCCodec *ldpc, const int *info, int *coded) {
    int Zc = ldpc->Zc;
    int info_cols = ldpc->base_info_cols;
    int base_rows = ldpc->base_rows;
    int nc = ldpc->num_parity;    /* base_rows*Zc */
    int nsys = info_cols * Zc;    /* info bits + filler */
    int K = ldpc->info_size;
    int iLS = zc_to_ils(Zc);
    const BGEntry *table = (ldpc->bg == 1) ? BG1_TABLE : BG2_TABLE;
    int num_entries = (ldpc->bg == 1) ? BG1_NUM_ENTRIES : BG2_NUM_ENTRIES;

    /* systematic vector = caller's info bits, then filler zeros */
    int *sys = (int *)malloc(nsys * sizeof(int));
    for (int i = 0; i < K; i++) sys[i] = info[i];
    for (int i = K; i < nsys; i++) sys[i] = 0;

    /* S[row] = XOR of systematic contributions to each lifted parity row */
    int *S = (int *)calloc(nc, sizeof(int));
    for (int e = 0; e < num_entries; e++) {
        int col = table[e].col;
        if (col >= info_cols) continue;
        int row = table[e].row;
        int s = table[e].shift[iLS] % Zc;
        for (int k = 0; k < Zc; k++)
            S[row * Zc + k] ^= sys[col * Zc + (k + s) % Zc];
    }

    /* core: P = M^-1 * S_core (dense GF(2) mat-vec, cached inverse) */
    int n = 4 * Zc;
    int nw = GF2_WORDS(n);
    int *parity = (int *)malloc(nc * sizeof(int));
    for (int i = 0; i < n; i++) {
        int bit = 0;
        const uint64_t *row = &ldpc->core_inv[(size_t)i * nw];
        for (int w = 0; w < nw; w++) {
            uint64_t word = row[w];
            while (word) {
                int b = __builtin_ctzll(word);
                int idx = w * 64 + b;
                if (idx < n) bit ^= S[idx];
                word &= word - 1;
            }
        }
        parity[i] = bit;
    }

    /* outer (base rows 4..base_rows-1): start from systematic contribution,
     * XOR in any core-column terms, done -- own diagonal column is shift-0
     * identity so this directly yields the parity bit, no further solve */
    for (int r = 4; r < base_rows; r++)
        for (int k = 0; k < Zc; k++)
            parity[r * Zc + k] = S[r * Zc + k];
    for (int e = 0; e < num_entries; e++) {
        int row = table[e].row;
        if (row < 4) continue;
        int col = table[e].col;
        if (col < info_cols || col >= info_cols + 4) continue;
        int j = col - info_cols;
        int s = table[e].shift[iLS] % Zc;
        for (int k = 0; k < Zc; k++)
            parity[row * Zc + k] ^= parity[j * Zc + (k + s) % Zc];
    }
    free(S);

    /* assemble: [K info bits][filler=0][parity] */
    memcpy(coded, sys, K * sizeof(int));
    for (int i = K; i < nsys; i++) coded[i] = 0;
    memcpy(coded + nsys, parity, nc * sizeof(int));

    free(sys);
    free(parity);
}
