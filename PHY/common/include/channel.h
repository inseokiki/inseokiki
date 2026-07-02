#ifndef CHANNEL_H
#define CHANNEL_H

#include "utils.h"

typedef struct { double snr_db; } AWGNChannel;
typedef struct { double snr_db; } FlatFadingChannel;

void awgn_init(AWGNChannel *ch, double snr_db);
void awgn_set_snr(AWGNChannel *ch, double snr_db);

/* Add noise to sig[n] -> out[n].  nfft>0: OFDM per-subcarrier scaling. */
void awgn_add_noise(AWGNChannel *ch, const cx_t *sig, int n,
                    int nfft, cx_t *out);

void flat_fading_init(FlatFadingChannel *ch, double snr_db);
void flat_fading_set_snr(FlatFadingChannel *ch, double snr_db);

/* Apply h*tx + AWGN.  h~CN(0,1), same for all syms.
   If true_h != NULL, writes the drawn coefficient. */
void flat_fading_apply(FlatFadingChannel *ch, const cx_t *tx, int n,
                       cx_t *out, cx_t *true_h);

#endif
