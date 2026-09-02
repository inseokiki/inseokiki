/* ================================================================
 *  nr_rate_matching.h
 *  TS 38.212 5.4.2.1-conformant LDPC circular-buffer rate matching
 *  (BG/Zc-aware k0, filler-bit-aware bit selection)
 *
 *  Separate from rate_matching.c's generic circular buffer -- that
 *  module is shared with PUCCH F3's Polar HARQ combining and is left
 *  untouched (Polar has no BG/Zc/filler concept). This module is used
 *  only by PDSCH/PUSCH LDPC paths (ldpc_nr.h).
 *
 *  Scope: single code block (C=1, matches ldpc_nr.h's scope -- no TS
 *  38.212 5.2.2 segmentation), and Ncb=N (mother codeword length, no
 *  limited-buffer-rate-matching/LBRM -- this project does not model
 *  the higher-layer parameters LBRM depends on).
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef NR_RATE_MATCHING_H
#define NR_RATE_MATCHING_H

/* TS 38.212 Table 5.4.2.1-2: starting position k0 for redundancy
 * version rv (0-3), as a function of LDPC base graph and lifting size.
 *   BG1: rv0->0, rv1->floor(17*Ncb/(66*Zc))*Zc, rv2->floor(33*Ncb/(66*Zc))*Zc,
 *        rv3->floor(56*Ncb/(66*Zc))*Zc
 *   BG2: rv0->0, rv1->floor(13*Ncb/(50*Zc))*Zc, rv2->floor(25*Ncb/(50*Zc))*Zc,
 *        rv3->floor(43*Ncb/(50*Zc))*Zc
 * (verified directly against TS 38.212 v18.8.0 5.4.2.1 Table 5.4.2.1-2
 * images, 2026-09-02). */
int nr_ldpc_k0(int bg, int Zc, int Ncb, int rv);

/* TS 38.212 5.4.2.1 bit-selection procedure: walks the length-Ncb
 * circular buffer starting at k0(rv), SKIPPING filler-bit positions
 * (indices in [filler_start, filler_end) -- see ldpc_nr.h, these are
 * the caller's LDPCCodec.info_size and .base_info_cols*.Zc), until e
 * bits have been selected into out[e] (wraps around / repeats if
 * e exceeds Ncb minus the filler count). rv=0 with a fresh (non-
 * persistent) buffer covers the non-HARQ single-shot case; rv=0..3
 * with a persistent soft_buf covers HARQ IR/Chase (see
 * nr_ldpc_rate_match_combine()). */
void nr_ldpc_rate_match_select(const int *coded, int Ncb, int bg, int Zc,
                                int filler_start, int filler_end,
                                int rv, int e, int *out);

/* Same selection sequence as nr_ldpc_rate_match_select(), but for a
 * length-Ncb double array (e.g. extrinsic LLR) instead of hard bits --
 * used by turbo equalization to extract the transmitted-position
 * subset of a full-length soft feedback buffer for the next
 * iteration's a priori input. */
void nr_ldpc_rate_match_select_soft(const double *buf, int Ncb, int bg, int Zc,
                                     int filler_start, int filler_end,
                                     int rv, int e, double *out);

/* Reverse of nr_ldpc_rate_match_select(): scatters e soft LLRs back
 * into the same circular positions (skipping the same filler range),
 * accumulating (+=) into a persistent length-Ncb soft_buf so repeated
 * RVs (Chase) or new RVs (IR) combine correctly. For a non-HARQ
 * single-shot decode, zero soft_buf and call once with rv=0. */
void nr_ldpc_rate_match_combine(double *soft_buf, int Ncb, int bg, int Zc,
                                 int filler_start, int filler_end,
                                 int rv, int e, const double *llr_in);

#endif
