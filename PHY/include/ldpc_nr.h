/* ================================================================
 *  ldpc_nr.h
 *  TS 38.212-conformant NR LDPC internals -- base graph/lifting-size
 *  selection, lifted parity-check construction, structured encoding.
 *
 *  Internal to the LDPC module: not part of ldpc.h's public API.
 *  ldpc.c dispatches into these functions for its single-code-block
 *  path; nr_sch.c (see nr_sch.h, P0-2c) also includes this header
 *  directly for its multi-code-block path, since one transport block's
 *  code blocks all share one bg/Kb/Zc/K and so one LDPCCodec instance.
 *  No other caller in the project includes this header directly.
 *
 *  Scope (2026-09-02, per docs/analysis/phy_development_direction_
 *  validation.md P0-1): this module itself always builds/encodes a
 *  SINGLE code block (info_size=K' bits in, coded_size=base_cols*Zc
 *  out) -- TS 38.212 5.2.2 segmentation into multiple code blocks is
 *  nr_sch.c's job (P0-2c), layered on top, not part of this module.
 *  Every ldpc_init() caller in the project still passes block_size
 *  capped at 8424 bits (so C=1, block_size=K'=B) for its own single
 *  code block today; nr_sch.c is not yet wired into any of them (see
 *  nr_sch.h scope note). Standard BG-aware rate matching (5.4.2 k0) is
 *  a separate follow-up (P0-3); this module's coded_size is the full
 *  "mother" codeword length.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef LDPC_NR_H
#define LDPC_NR_H

#include "ldpc.h"

/* TS 38.212 6.2.2 base graph selection, TB level. B = pre-segmentation
 * bit sequence length (includes the caller's outer 24-bit CRC, this
 * project's universal convention), code_rate = target rate R (A<=292,
 * or A<=3824 && R<=0.67, or R<=0.25 -> BG2, else BG1, A=B-24).
 * Writes bg(1/2), Kcb(8448 BG1 / 3840 BG2), Kb (BG1: fixed 22; BG2:
 * B-based per TS 38.212 5.2.2 -- same for every code block of this TB,
 * see nr_sch.h), base_rows, base_info_cols, base_cols. */
void nr_select_bg(int B, double code_rate, int *bg, int *Kcb, int *Kb,
                   int *base_rows, int *base_info_cols, int *base_cols);

/* TS 38.212 5.2.2 lifting size selection, per code block. Kprime = this
 * code block's info bits (incl. its own CRC24B if segmented, excl.
 * filler). Writes Zc(lifting size), K(=base_info_cols*Zc),
 * filler_size(=K-Kprime, always >=0). If no Table 5.3.2-1 Zc<=384
 * satisfies Kb*Zc>=Kprime, prints a diagnostic to stderr and exit(1)s
 * rather than silently clamping -- see module header. */
void nr_select_zc(int Kb, int Kprime, int base_info_cols,
                   int *Zc, int *K, int *filler_size);

/* Single-code-block convenience wrapper: nr_select_bg()+nr_select_zc()
 * with C=1 (so Kprime=block_size). Used by every caller of ldpc_init()
 * in this project today (see module scope note above). */
void nr_select_bg_zc(int block_size, double code_rate,
                      int *bg, int *Zc, int *Kb,
                      int *base_rows, int *base_info_cols, int *base_cols,
                      int *filler_size);

/* Builds the lifted (per-bit) parity-check matrix into the SAME
 * check-major CSR (H_row_ptr/H_col/H_nnz) and variable-major adjacency
 * (Ht_row_ptr/Ht_links/Ht_nnz) fields that ldpc.c's existing edge-message
 * belief-propagation decoder already consumes -- decoder needs zero
 * changes, only nc/nv grow. `bg`/`Zc` select which BGEntry table and
 * lifting-size-set column to read; base_rows/base_cols size the lifted
 * matrix (lifted_rows=base_rows*Zc, lifted_cols=base_cols*Zc). Shift
 * direction: TS 38.212 5.3.2 defines the circulant permutation matrix by
 * circularly shifting the identity matrix I to the RIGHT by the table's
 * shift value (mod Zc for the chosen lifting size) -- i.e. within a
 * lifted block, block-row k's single 1 lands at block-column
 * (k+shift) mod Zc. */
void build_H_nr(LDPCCodec *ldpc);

/* One-time setup for ldpc_encode_nr(): solves the small closed "core"
 * system (the first 4 parity base-columns) by computing the explicit
 * GF(2) inverse of its n=4*Zc coefficient matrix (n<=1536 for the
 * largest Zc=384) via Gauss-Jordan elimination, caching the result in
 * ldpc->core_inv. Requires ldpc->bg/Zc already populated (by
 * nr_select_bg_zc()); does not depend on build_H_nr() having run.
 * Call once per LDPCCodec instance, before any ldpc_encode_nr() call --
 * the result depends only on (bg, Zc), never on the actual info bits,
 * so redoing this per encode() call would be correct but needlessly
 * slow (this is the expensive O(n^3) elimination; encode itself is
 * only O(n^2) per call once core_inv is cached). */
void ldpc_encode_prepare_nr(LDPCCodec *ldpc);

/* TS 38.212-structured NR LDPC encode: info[block_size] (the systematic
 * bits the caller wants encoded, no filler) -> coded[coded_size]
 * (systematic bits, then filler forced to 0, then base_rows*Zc parity
 * bits). Requires ldpc->bg/Zc/Kb/base_rows/base_info_cols/base_cols/
 * filler_size already populated by nr_select_bg_zc(). Solves the small
 * closed "core" system (the first 4 parity base-columns, empirically
 * confirmed -- see ldpc_tables.h -- to never appear in any row >=4, so
 * they form a self-contained circulant-block system) via generic GF(2)
 * block elimination discovered from the actual table data at ldpc_init
 * time (not hardcoded row indices), then the remaining base_rows-4
 * parity blocks directly (each is a single shift-0 diagonal entry, so
 * solving for it is a plain XOR once the other terms in its row are
 * known). */
void ldpc_encode_nr(const LDPCCodec *ldpc, const int *info, int *coded);

#endif
