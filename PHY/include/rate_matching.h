/* ================================================================
 *  rate_matching.h
 *  Circular-buffer HARQ rate matching (RV offsets) + soft combining
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef RATE_MATCHING_H
#define RATE_MATCHING_H

/* Simplified circular-buffer rate matching (TS 38.212 5.4.2 in spirit).
 * k0(rv) is a uniform 1/4-buffer split rather than the spec's BG/Zc-
 * specific formula (implementation-defined simplification) -- this
 * stays true even though the LDPC codec itself became real NR BG1/BG2
 * QC-LDPC (2026-09-02, see ldpc_nr.h): the mother-code rate now varies
 * per (BG,Zc) instead of being one fixed constant, so a single
 * HARQ_MOTHER_RATE no longer has a well-defined meaning and was removed
 * (ldpc.c's ldpc_init() derives BG/Zc, and hence the achieved rate,
 * directly from block_size/code_rate). A BG/Zc-aware k0 is a separate
 * follow-up (tasks/todo.md P0-3), left untouched here since this module
 * is also shared by PUCCH F3's Polar HARQ circular buffer. */

/* k0 = round(rv/4 * ncb), rv in [0,3] */
int rv_start_offset(int rv, int ncb);

/* Select e bits from coded[ncb] starting at k0(rv), wrapping around
   (repeats if e>ncb) into out[e]. */
void rate_match_select(const int *coded, int ncb, int rv, int e, int *out);

/* Scatter e soft LLRs back into a persistent ncb-length soft buffer at
   the same circular positions used by rate_match_select(), accumulating
   (+=) so repeated RVs (Chase) or new RVs (IR) both combine correctly. */
void rate_match_combine(double *soft_buf, int ncb, int rv, int e,
                        const double *llr_in);

#endif
