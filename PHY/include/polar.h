/* ================================================================
 *  polar.h
 *  Polar encoder/decoder (control channel)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef POLAR_H
#define POLAR_H

typedef struct {
    int  N;
    int  K;
    int *info_indices;   /* sorted, length K */
    int *frozen_mask;    /* frozen_mask[i]=1 if frozen, length N */
} PolarCodec;

/* E = rate-matched output length (same E passed to polar_rate_match).
   Needed here because TS 38.212 5.4.1.1 shortening forces certain
   coded-bit positions to 0 at the encoder; those positions must be
   selected as frozen (not info) bits, or the decoder's forced-zero
   assumption for them will corrupt real info bits placed there. */
void polar_init(PolarCodec *pc, int N, int K, int E);
void polar_free(PolarCodec *pc);

/* encode: info[K] -> coded[N] */
void polar_encode(const PolarCodec *pc, const int *info, int *coded);

/* SC decode: llr[N] -> decoded[K] */
void polar_decode(const PolarCodec *pc, const double *llr, int *decoded);

#endif
