#ifndef POLAR_RATE_MATCH_H
#define POLAR_RATE_MATCH_H

/* Rate match: coded_bits[N] -> out[E].  K = info bits (chooses puncture/shorten). */
void polar_rate_match(const int *coded_bits, int N, int E, int K, int *out);

/* Rate dematch: llr_in[E] -> llr_out[N].  Caller allocates out[N]. */
void polar_rate_dematch(const double *llr_in, int E, int N, int K, double *llr_out);

#endif
