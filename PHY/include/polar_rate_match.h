/* ================================================================
 *  polar_rate_match.h
 *  Polar rate matching/dematching (shortening/puncturing/repetition)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef POLAR_RATE_MATCH_H
#define POLAR_RATE_MATCH_H

/* Sub-block interleaver used by rate matching (N must be a power of 2,
   N>=32). Exposed so polar.c's frozen-bit construction can determine
   which original bit indices land in the shortened-away tail. */
void polar_interleaver(int N, int *pi);

/* Rate match: coded_bits[N] -> out[E].  K = info bits (chooses puncture/shorten). */
void polar_rate_match(const int *coded_bits, int N, int E, int K, int *out);

/* Rate dematch: llr_in[E] -> llr_out[N].  Caller allocates out[N]. */
void polar_rate_dematch(const double *llr_in, int E, int N, int K, double *llr_out);

#endif
