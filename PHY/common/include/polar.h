#ifndef POLAR_H
#define POLAR_H

typedef struct {
    int  N;
    int  K;
    int *info_indices;   /* sorted, length K */
    int *frozen_mask;    /* frozen_mask[i]=1 if frozen, length N */
} PolarCodec;

void polar_init(PolarCodec *pc, int N, int K);
void polar_free(PolarCodec *pc);

/* encode: info[K] -> coded[N] */
void polar_encode(const PolarCodec *pc, const int *info, int *coded);

/* SC decode: llr[N] -> decoded[K] */
void polar_decode(const PolarCodec *pc, const double *llr, int *decoded);

#endif
