/* ================================================================
 *  nr_sch.h
 *  TS 38.212 5.2.2 code block segmentation, code block CRC
 *  attachment, and 5.5 code block concatenation (P0-2c).
 *
 *  Scope (2026-09-03, per docs/analysis/phy_development_direction_
 *  validation.md Phase 2 "code-block segmentation and CB CRC", and
 *  tasks/todo.md P0-2c): this module resolves TS 38.212 5.2.2's
 *  segmentation parameters (C, L, B', K') for a transport block,
 *  splits its bits into C code blocks (attaching CRC24B to each when
 *  C>1), and concatenates C rate-matched code blocks back into one
 *  sequence (5.5). It is deliberately standalone and NOT wired into
 *  any pdsch.c/pusch.c run_* function yet -- per the Phase 2 plan,
 *  this is built and unit-tested independently first; actually
 *  integrating it (and, separately, exercising TBS>Kcb at all -- every
 *  existing caller still caps block_size at 8424 bits today) is a
 *  follow-up decision, not made by this module. TS 38.212 5.4.2.1's
 *  per-code-block rate matching output length E_r is
 *  nr_rate_matching.h's nr_ldpc_er_alloc() (2026-09-03), used together
 *  with this module's NRSegInfo.C to drive one nr_ldpc_rate_match_select()
 *  call per code block before nr_seg_concat().
 *
 *  All C code blocks of one transport block share the same bg/Kb/Zc/K
 *  (Kb depends only on B, not on the per-block K' -- TS 38.212 5.2.2),
 *  so callers need only one LDPCCodec instance (via
 *  ldpc_init_resolved(), see ldpc.h) for all C blocks.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef NR_SCH_H
#define NR_SCH_H

typedef struct {
    int bg;               /* 1 or 2 */
    int Kcb;               /* 8448 (BG1) or 3840 (BG2) */
    int Kb;                 /* BG1: 22 always. BG2: B-based, see nr_select_bg() */
    int C;                   /* number of code blocks, >=1 */
    int L;                    /* per-code-block CRC length: 0 if C==1, else 24 (CRC24B) */
    int B;                     /* input length: the transport block's bit count
                                   including its own outer CRC (this project's
                                   universal 24-bit CRC24A convention) */
    int Bp;                     /* B' = B + C*L */
    int Kprime;                  /* K' = B'/C: this transport block's per-code-block
                                     info bit count (incl. CRC24B if C>1, excl. filler) --
                                     identical for every one of the C code blocks */
    int Zc;                       /* lifting size, shared by every code block */
    int K;                         /* base_info_cols*Zc: per-code-block info+filler bits */
    int filler_size;                /* K - Kprime, always >=0 */
    int base_rows, base_info_cols, base_cols;
} NRSegInfo;

/* TS 38.212 5.2.2 segmentation parameters for a transport block of B bits
 * (already includes this project's outer CRC24A) at target code rate R
 * (used only for TS 38.212 6.2.2 base graph selection, same formula as
 * ldpc_nr.c's single-code-block path).
 *
 * KNOWN OPEN QUESTION (2026-09-03, confirmed unresolved via 3gpp-server MCP
 * TS 38.212 v18.8.0 5.2.2 direct image inspection + secondary cross-check,
 * not just assumed): the spec formula is literally K'=B'/C (no visible
 * ceiling/floor operator on the source image, unlike the neighbouring C=
 * formula which clearly shows ceiling brackets) with the segmentation
 * assembly loop using this same K' as an identical, non-varying bound for
 * every one of the C code blocks. Algebraically this requires B'/C (i.e.
 * B/C, since B'=B+C*L) to be an exact integer, which does not hold for
 * every B -- e.g. BG1, B=8449 gives C=2, K'=4248.5. Whether the intended
 * reading is silent floor/ceil rounding, or an unequal per-block split
 * this reading missed, was not resolved from the primary source's tiny
 * inline formula image nor from secondary sources (a WebSearch/WebFetch
 * cross-check corroborated plain division but did not settle the
 * rounding question either). NOT the same mechanism as TS 38.212 5.4.2.1's
 * E_r floor/ceil split (nr_rate_matching.h's nr_ldpc_er_alloc()), which
 * is a separate, later step operating on rate-matched output length, not
 * on segmentation's raw K'.
 *
 * Per user decision (2026-09-03): rather than guess, this function
 * computes K'=B'/C in exact integer arithmetic and requires B' % C == 0:
 * if it does not divide evenly, it prints a diagnostic to stderr and
 * exit(1)s instead of rounding. The C==1 case (L=0, B'=B, K'=B, this
 * project's only exercised path today) is always exact and unaffected. */
void nr_seg_compute(int B, double code_rate, NRSegInfo *seg);

/* Splits in[seg->B] into seg->C code blocks of seg->Kprime bits each,
 * written consecutively into cb_bits[seg->C * seg->Kprime]. When
 * seg->C>1, CRC24B (TS 38.212 5.2.2's L=24 code block CRC) is computed
 * over each (Kprime-24)-bit segment of in[] and appended -- when
 * seg->C==1, cb_bits is simply a copy of in[] (L=0, no CRC attached,
 * matching ldpc_nr.c's existing single-code-block behavior). Filler
 * bits are NOT included here: ldpc_encode()/ldpc_init_resolved() pad
 * each code block from Kprime up to seg->K internally (see ldpc_nr.h). */
void nr_seg_split(const int *in, const NRSegInfo *seg, int *cb_bits);

/* TS 38.212 5.5 code block concatenation: writes the seg->C rate-matched
 * bit sequences rm[0..seg->C-1] (rm[r] has E[r] bits, see
 * nr_rate_matching.h's nr_ldpc_er_alloc()) consecutively into
 * out[sum(E[0..C-1])]. Trivial by construction (5.5 is literally
 * sequential concatenation with no reordering), provided as an explicit
 * primitive so integration call sites don't each reimplement the
 * running-offset bookkeeping. */
void nr_seg_concat(const NRSegInfo *seg, const int *const *rm, const int *E, int *out);

#endif
