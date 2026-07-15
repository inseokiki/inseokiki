/* ================================================================
 *  rate_matching.h
 *  Circular-buffer HARQ rate matching (RV offsets) + soft combining
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef RATE_MATCHING_H
#define RATE_MATCHING_H

/* Simplified circular-buffer rate matching (TS 38.212 5.4.2 in spirit).
 * The LLS's LDPC has no base-graph/lifting structure, so k0(rv) is a
 * uniform 1/4-buffer split rather than the spec's BG-specific formula
 * (implementation-defined simplification). */
#define HARQ_MOTHER_RATE (1.0 / 3.0)

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
