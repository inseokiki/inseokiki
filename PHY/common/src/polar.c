#include "polar.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- sorting helper for Bhattacharyya construction ---- */
typedef struct { double val; int idx; } PairDI;
static int cmp_pair(const void *a, const void *b) {
    const PairDI *pa = (const PairDI *)a;
    const PairDI *pb = (const PairDI *)b;
    return (pa->val > pb->val) - (pa->val < pb->val);
}

static void generate_frozen_bits(PolarCodec *pc) {
    int N = pc->N;
    double *z = (double *)malloc(N * sizeof(double));
    z[0] = 0.5;
    int n = (int)(log2((double)N) + 0.5);
    for (int i = 0; i < n; i++) {
        int cur = 1 << i;
        double *nz = (double *)malloc(2 * cur * sizeof(double));
        for (int j = 0; j < cur; j++) {
            nz[2*j]   = 2*z[j] - z[j]*z[j];
            nz[2*j+1] = z[j]*z[j];
        }
        for (int j = 0; j < 2*cur; j++) z[j] = nz[j];
        free(nz);
    }

    PairDI *order = (PairDI *)malloc(N * sizeof(PairDI));
    for (int i = 0; i < N; i++) { order[i].val = z[i]; order[i].idx = i; }
    qsort(order, N, sizeof(PairDI), cmp_pair);

    for (int i = 0; i < pc->K; i++)
        pc->info_indices[i] = order[i].idx;
    /* sort info_indices ascending */
    for (int i = 0; i < pc->K - 1; i++) {
        for (int j = i+1; j < pc->K; j++) {
            if (pc->info_indices[j] < pc->info_indices[i]) {
                int t = pc->info_indices[i];
                pc->info_indices[i] = pc->info_indices[j];
                pc->info_indices[j] = t;
            }
        }
    }
    /* build frozen mask */
    memset(pc->frozen_mask, 0, N * sizeof(int));
    for (int i = pc->K; i < N; i++)
        pc->frozen_mask[order[i].idx] = 1;

    free(z); free(order);
}

void polar_init(PolarCodec *pc, int N, int K) {
    pc->N           = N;
    pc->K           = K;
    pc->info_indices = (int *)malloc(K * sizeof(int));
    pc->frozen_mask  = (int *)malloc(N * sizeof(int));
    generate_frozen_bits(pc);
}

void polar_free(PolarCodec *pc) {
    free(pc->info_indices); pc->info_indices = NULL;
    free(pc->frozen_mask);  pc->frozen_mask  = NULL;
}

static void polar_transform(int *x, int N) {
    for (int m = 1; m < N; m *= 2)
        for (int i = 0; i < N; i += 2*m)
            for (int j = 0; j < m; j++)
                x[i+j] ^= x[i+j+m];
}

void polar_encode(const PolarCodec *pc, const int *info, int *coded) {
    int *u = (int *)calloc(pc->N, sizeof(int));
    for (int i = 0; i < pc->K; i++) u[pc->info_indices[i]] = info[i];
    polar_transform(u, pc->N);
    memcpy(coded, u, pc->N * sizeof(int));
    free(u);
}

/* SC decoder – L[s*N+i], C[s*N+i] */
static double f_neg(double a, double b) {
    double s = ((a >= 0) == (b >= 0)) ? 1.0 : -1.0;
    double aa = a < 0 ? -a : a, ab = b < 0 ? -b : b;
    return s * (aa < ab ? aa : ab);
}
static double f_pos(double a, double b, int u) { return (1 - 2*u)*a + b; }

static void sc_recurse(double *L, int *C, int N, int stage, int offset,
                        const int *frozen_mask) {
    if (stage == 0) {
        C[offset] = frozen_mask[offset] ? 0 : (L[offset] < 0 ? 1 : 0);
        return;
    }
    int half = 1 << (stage - 1);
    /* f-function (left child) */
    for (int i = 0; i < half; i++)
        L[(stage-1)*N + offset + i] =
            f_neg(L[stage*N + offset + i], L[stage*N + offset + half + i]);
    sc_recurse(L, C, N, stage-1, offset, frozen_mask);
    /* g-function (right child) */
    for (int i = 0; i < half; i++)
        L[(stage-1)*N + offset + half + i] =
            f_pos(L[stage*N + offset + i],
                  L[stage*N + offset + half + i],
                  C[(stage-1)*N + offset + i]);
    sc_recurse(L, C, N, stage-1, offset + half, frozen_mask);
    /* combine partial sums */
    for (int i = 0; i < half; i++) {
        C[stage*N + offset + i] =
            C[(stage-1)*N + offset + i] ^ C[(stage-1)*N + offset + half + i];
        C[stage*N + offset + half + i] = C[(stage-1)*N + offset + half + i];
    }
}

void polar_decode(const PolarCodec *pc, const double *llr, int *decoded) {
    int N = pc->N;
    int n = (int)(log2((double)N) + 0.5);
    double *L = (double *)calloc((n+1) * N, sizeof(double));
    int    *C = (int    *)calloc((n+1) * N, sizeof(int));
    for (int i = 0; i < N; i++) L[n*N + i] = llr[i];
    sc_recurse(L, C, N, n, 0, pc->frozen_mask);
    for (int i = 0; i < pc->K; i++)
        decoded[i] = C[pc->info_indices[i]];  /* from stage 0 */
    free(L); free(C);
}
