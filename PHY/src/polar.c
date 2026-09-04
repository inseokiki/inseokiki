/* ================================================================
 *  polar.c
 *  Polar encoder/decoder (control channel), TS 38.212 5.3.1-conformant
 *  (2026-09-04): real Table 5.3.1.2-1 reliability sequence (replacing
 *  the earlier generic Bhattacharyya-parameter approximation), TS
 *  38.212 5.3.1.1 input interleaving, and 5.3.1.2 parity-check (PC)
 *  bit placement/generation. All three confirmed directly against the
 *  local 3gpp/38212-hc0/38212-hc0.docx primary source via 3gpp-server
 *  MCP image inspection -- see polar.h/polar_tables.h doc comments.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "polar.h"
#include "polar_tables.h"
#include "polar_rate_match.h"
#include "crc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void polar_uci_npc(int K, int E, int *n_PC, int *n_PC_wm) {
    if (K >= 18 && K <= 25) {
        *n_PC = 3;
        *n_PC_wm = (E - K + 3 > 192) ? 1 : 0;
    } else {
        /* K>30 confirmed n_PC=0 from the primary source; 26<=K<=30 is
         * the open question documented in polar.h -- applying the same
         * n_PC=0 result rather than guessing a different one. */
        *n_PC = 0;
        *n_PC_wm = 0;
    }
}

static int popcount32(unsigned int x) {
    int c = 0;
    while (x) { c += x & 1u; x >>= 1; }
    return c;
}

/* ---- sorting helper: ascending by val (low val = more reliable) ---- */
typedef struct { double val; int idx; } PairDI;
static int cmp_pair(const void *a, const void *b) {
    const PairDI *pa = (const PairDI *)a;
    const PairDI *pb = (const PairDI *)b;
    return (pa->val > pb->val) - (pa->val < pb->val);
}

static void generate_frozen_bits(PolarCodec *pc, int E) {
    int N = pc->N, K = pc->K, n_PC = pc->n_PC, n_PC_wm = pc->n_PC_wm;
    int n_QI = K + n_PC;   /* |Q_I^N| */

    /* TS 38.212 5.4.1.1 shortening (E<N, K/E>7/16): rate matching drops
     * the interleaved tail interleaved[E..N-1] and the decoder treats
     * those original bit positions as known-zero. They must be forced
     * frozen here -- ranked as maximally unreliable -- so they're never
     * chosen as info/PC bits (see polar.h comment on polar_init). */
    int *forced_zero = (int *)calloc(N, sizeof(int));
    if (E > 0 && E < N) {
        double r = (double)K / E;
        if (r > 7.0 / 16.0) {
            int *pi = (int *)malloc(N * sizeof(int));
            polar_interleaver(N, pi);
            for (int i = E; i < N; i++) forced_zero[pi[i]] = 1;
            free(pi);
        }
    }

    /* Q_N: TS 38.212 5.3.1.2 subsequence of Q_Nmax with elements < N,
     * order preserved (already ascending reliability) -- Q_Nmax[r] for
     * increasing r is increasing reliability (Q_Nmax[0]=0 is the
     * classically least-reliable bit index, confirmed against known
     * reference values, see polar_tables.h). */
    int *rank = (int *)malloc(N * sizeof(int));   /* rank[bit_idx] = reliability rank, 0=least */
    {
        int r = 0;
        for (int i = 0; i < POLAR_NMAX; i++) {
            int q = POLAR_Q_NMAX[i];
            if (q < N) rank[q] = r++;
        }
    }

    /* order[]: ascending val => index 0 = most reliable ... N-1 = least
     * reliable (forced_zero pushed to the very end), mirroring the
     * pre-2026-09-04 Bhattacharyya-value convention this replaces. */
    PairDI *order = (PairDI *)malloc(N * sizeof(PairDI));
    for (int i = 0; i < N; i++) {
        order[i].idx = i;
        order[i].val = forced_zero[i] ? (double)(2 * N) : (double)(N - 1 - rank[i]);
    }
    qsort(order, N, sizeof(PairDI), cmp_pair);
    free(forced_zero);
    free(rank);

    /* frozen_mask / pc_mask init */
    memset(pc->frozen_mask, 0, N * sizeof(int));
    memset(pc->pc_mask, 0, N * sizeof(int));
    for (int i = n_QI; i < N; i++) pc->frozen_mask[order[i].idx] = 1;

    if (n_PC > 0) {
        /* Group A: (n_PC - n_PC_wm) least-reliable positions WITHIN
         * Q_I^N (the tail of order[0..n_QI-1]). */
        int n_tail = n_PC - n_PC_wm;
        for (int i = n_QI - n_tail; i < n_QI; i++)
            pc->pc_mask[order[i].idx] = 1;

        /* Group B: n_PC_wm positions of minimum row weight wt(G_N row i)
         * = 2^popcount(i) (standard Arikan polar-transform row-weight
         * identity, not a spec table), searched within order[0..K-1]
         * (= Q~_I^N, the K most reliable of Q_I^N per TS 38.212 5.3.1.2)
         * -- ties broken by earliest occurrence in order[] (= highest
         * reliability among the tied positions, per spec text). */
        if (n_PC_wm > 0) {
            int minw = -1;
            for (int i = 0; i < K; i++) {
                int w = popcount32((unsigned int)order[i].idx);
                if (minw < 0 || w < minw) minw = w;
            }
            int cnt = 0;
            for (int i = 0; i < K && cnt < n_PC_wm; i++) {
                if (popcount32((unsigned int)order[i].idx) == minw) {
                    pc->pc_mask[order[i].idx] = 1;
                    cnt++;
                }
            }
        }
    }

    free(order);
}

void polar_init(PolarCodec *pc, int N, int K, int E,
                 int I_IL, int n_PC, int n_PC_wm) {
    pc->N       = N;
    pc->K       = K;
    pc->I_IL    = I_IL;
    pc->n_PC    = n_PC;
    pc->n_PC_wm = n_PC_wm;
    pc->pc_mask     = (int *)malloc(N * sizeof(int));
    pc->frozen_mask = (int *)malloc(N * sizeof(int));
    generate_frozen_bits(pc, E);
}

void polar_free(PolarCodec *pc) {
    free(pc->pc_mask);     pc->pc_mask     = NULL;
    free(pc->frozen_mask); pc->frozen_mask = NULL;
}

/* TS 38.212 5.3.1.1: c'_k = c_{Pi(k)}, k=0..K-1. I_IL=0 -> Pi(k)=k
 * (identity, UCI Polar). I_IL=1 -> Pi built from Table 5.3.1.1-1
 * (PBCH/PDCCH): k=0; for m=0..K_ILmax-1: if Pi_IL_max[m] >= K_ILmax-K:
 * Pi(k)=Pi_IL_max[m]-(K_ILmax-K); k=k+1. */
static void input_interleave(const int *c, int K, int I_IL, int *c_out) {
    if (!I_IL) {
        memcpy(c_out, c, K * sizeof(int));
        return;
    }
    int k = 0;
    for (int m = 0; m < POLAR_KILMAX; m++) {
        int v = POLAR_PI_IL_MAX[m];
        if (v >= POLAR_KILMAX - K) {
            int pik = v - (POLAR_KILMAX - K);
            c_out[k] = c[pik];
            k++;
        }
    }
}

static void polar_transform(int *x, int N) {
    for (int m = 1; m < N; m *= 2)
        for (int i = 0; i < N; i += 2*m)
            for (int j = 0; j < m; j++)
                x[i+j] ^= x[i+j+m];
}

void polar_encode(const PolarCodec *pc, const int *info, int *coded) {
    int N = pc->N, K = pc->K;
    int *cp = (int *)malloc(K * sizeof(int));
    input_interleave(info, K, pc->I_IL, cp);

    /* TS 38.212 5.3.1.2 u-sequence generation: 5-stage shift register
     * (y0..y4, all init 0), shifted once per n=0..N-1 regardless of
     * branch; PC bit positions take the shift register's current output
     * y0 as their value; real info/CRC positions take c'_k (k
     * increments) and additionally XOR their bit into y0; frozen
     * positions are 0. */
    int *u = (int *)calloc(N, sizeof(int));
    int y[5] = {0,0,0,0,0};
    int k = 0;
    for (int n = 0; n < N; n++) {
        int yt = y[0];
        y[0]=y[1]; y[1]=y[2]; y[2]=y[3]; y[3]=y[4]; y[4]=yt;
        if (pc->frozen_mask[n]) {
            u[n] = 0;
        } else if (pc->pc_mask[n]) {
            u[n] = y[0];
        } else {
            u[n] = cp[k++];
            y[0] ^= u[n];
        }
    }
    free(cp);

    polar_transform(u, N);
    memcpy(coded, u, N * sizeof(int));
    free(u);
}

/* SC decoder – L[s*N+i], C[s*N+i]. sh[5]/k thread the same shift
 * register + running info-bit index through the recursion so PC-bit
 * leaves (stage 0) can use the decoder's own prior hard decisions,
 * exactly mirroring the encoder's u-sequence construction -- this
 * recursion visits stage-0 leaves in strictly increasing offset order
 * (standard property of this left-then-right divide-and-conquer SC
 * structure), matching the encoder's n=0..N-1 scan order. */
static double f_neg(double a, double b) {
    double s = ((a >= 0) == (b >= 0)) ? 1.0 : -1.0;
    double aa = a < 0 ? -a : a, ab = b < 0 ? -b : b;
    return s * (aa < ab ? aa : ab);
}
static double f_pos(double a, double b, int u) { return (1 - 2*u)*a + b; }

static void sc_recurse(double *L, int *C, int N, int stage, int offset,
                        const PolarCodec *pc, int *sh, int *info_k, int *decoded) {
    if (stage == 0) {
        int bit;
        if (pc->frozen_mask[offset]) {
            bit = 0;
        } else if (pc->pc_mask[offset]) {
            bit = sh[0];
        } else {
            bit = (L[offset] < 0) ? 1 : 0;
            sh[0] ^= bit;
            decoded[(*info_k)++] = bit;
        }
        C[offset] = bit;
        int t = sh[0];
        sh[0]=sh[1]; sh[1]=sh[2]; sh[2]=sh[3]; sh[3]=sh[4]; sh[4]=t;
        return;
    }
    int half = 1 << (stage - 1);
    for (int i = 0; i < half; i++)
        L[(stage-1)*N + offset + i] =
            f_neg(L[stage*N + offset + i], L[stage*N + offset + half + i]);
    sc_recurse(L, C, N, stage-1, offset, pc, sh, info_k, decoded);
    for (int i = 0; i < half; i++)
        L[(stage-1)*N + offset + half + i] =
            f_pos(L[stage*N + offset + i],
                  L[stage*N + offset + half + i],
                  C[(stage-1)*N + offset + i]);
    sc_recurse(L, C, N, stage-1, offset + half, pc, sh, info_k, decoded);
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

    /* decode straight into interleaved-domain c'[K] (in ascending bit-
     * index / k order, matching encode's assignment order), then undo
     * TS 38.212 5.3.1.1's interleaving to recover the caller's info[K]
     * order. */
    int *cp = (int *)malloc(pc->K * sizeof(int));
    int sh[5] = {0,0,0,0,0};
    int info_k = 0;
    sc_recurse(L, C, N, n, 0, pc, sh, &info_k, cp);
    free(L); free(C);

    if (!pc->I_IL) {
        memcpy(decoded, cp, pc->K * sizeof(int));
    } else {
        /* invert c'_k = c_{Pi(k)}: same Pi(k) construction as the
         * encoder, scatter cp[k] to decoded[Pi(k)]. */
        int k = 0;
        for (int m = 0; m < POLAR_KILMAX; m++) {
            int v = POLAR_PI_IL_MAX[m];
            if (v >= POLAR_KILMAX - pc->K) {
                int pik = v - (POLAR_KILMAX - pc->K);
                decoded[pik] = cp[k];
                k++;
            }
        }
    }
    free(cp);
}

/* ================================================================
 *  CA-SCL (CRC-Aided Successive Cancellation List) decoder.
 *  Implementation-defined -- see polar.h doc comment (not a 3GPP claim).
 * ================================================================ */

typedef struct {
    double *Lb;    /* (n+1)*N, this path's own L buffer */
    int    *Cb;    /* (n+1)*N, this path's own C buffer */
    int     sh[5]; /* this path's own shift-register state */
    int    *cp;    /* K, this path's decoded info/CRC bits (interleaved-domain, k-order) */
    int     k;     /* how many of cp[] filled so far */
    double  pm;    /* path metric, lower = more likely */
} SCLPath;

/* LLR-domain path-metric cost of deciding `bit` at a leaf with channel
 * LLR value L (this project's convention: L>0 favors bit=0, L<0 favors
 * bit=1) -- Balatsoukas-Stimming et al.'s numerically-stable formulation,
 * cost = log(1+exp((2*bit-1)*L)), avoiding the raw-probability-product
 * underflow of the original Tal-Vardy path metric. */
static double scl_pm_cost(double Lval, int bit) {
    double x = (2*bit - 1) * Lval;
    if (x > 30.0) return x;           /* log(1+exp(x)) ~= x for large x */
    return log(1.0 + exp(x));
}

static int cmp_path_pm(const void *a, const void *b) {
    const SCLPath *pa = *(SCLPath *const *)a;
    const SCLPath *pb = *(SCLPath *const *)b;
    return (pa->pm > pb->pm) - (pa->pm < pb->pm);
}

static void scl_shift(int *sh) {
    int t = sh[0];
    sh[0]=sh[1]; sh[1]=sh[2]; sh[2]=sh[3]; sh[3]=sh[4]; sh[4]=t;
}

static void scl_recurse(SCLPath **paths, int *num_active, int N, int stage, int offset,
                         const PolarCodec *pc, int L_target) {
    if (stage == 0) {
        if (pc->frozen_mask[offset]) {
            for (int p = 0; p < *num_active; p++) {
                SCLPath *P = paths[p];
                P->Cb[offset] = 0;
                scl_shift(P->sh);
            }
            return;
        }
        if (pc->pc_mask[offset]) {
            for (int p = 0; p < *num_active; p++) {
                SCLPath *P = paths[p];
                int bit = P->sh[0];
                P->Cb[offset] = bit;
                P->pm += scl_pm_cost(P->Lb[offset], bit);
                scl_shift(P->sh);
            }
            return;
        }

        /* real info/CRC bit: split every active path into a bit=0 and a
         * bit=1 child, then prune to the L_target lowest-pm survivors. */
        int na = *num_active;
        for (int p = 0; p < na; p++) {
            SCLPath *P0 = paths[p];
            SCLPath *P1 = paths[na + p];   /* pre-allocated clone slot */
            int nk = (int)(log2((double)N) + 0.5) + 1;
            memcpy(P1->Lb, P0->Lb, (size_t)nk * N * sizeof(double));
            memcpy(P1->Cb, P0->Cb, (size_t)nk * N * sizeof(int));
            memcpy(P1->sh, P0->sh, sizeof(P0->sh));
            memcpy(P1->cp, P0->cp, (size_t)P0->k * sizeof(int));
            P1->k = P0->k;
            double base_pm = P0->pm;
            double llr_here = P0->Lb[offset];

            P0->Cb[offset] = 0;
            P0->pm = base_pm + scl_pm_cost(llr_here, 0);
            P0->cp[P0->k++] = 0;
            P0->sh[0] ^= 0;

            P1->Cb[offset] = 1;
            P1->pm = base_pm + scl_pm_cost(llr_here, 1);
            P1->cp[P1->k++] = 1;
            P1->sh[0] ^= 1;
        }
        *num_active = na * 2;
        if (*num_active > L_target) {
            qsort(paths, (size_t)(*num_active), sizeof(SCLPath *), cmp_path_pm);
            *num_active = L_target;
        }
        for (int p = 0; p < *num_active; p++) scl_shift(paths[p]->sh);
        return;
    }

    int half = 1 << (stage - 1);
    for (int p = 0; p < *num_active; p++) {
        SCLPath *P = paths[p];
        for (int i = 0; i < half; i++)
            P->Lb[(stage-1)*N + offset + i] =
                f_neg(P->Lb[stage*N + offset + i], P->Lb[stage*N + offset + half + i]);
    }
    scl_recurse(paths, num_active, N, stage-1, offset, pc, L_target);

    for (int p = 0; p < *num_active; p++) {
        SCLPath *P = paths[p];
        for (int i = 0; i < half; i++)
            P->Lb[(stage-1)*N + offset + half + i] =
                f_pos(P->Lb[stage*N + offset + i],
                      P->Lb[stage*N + offset + half + i],
                      P->Cb[(stage-1)*N + offset + i]);
    }
    scl_recurse(paths, num_active, N, stage-1, offset + half, pc, L_target);

    for (int p = 0; p < *num_active; p++) {
        SCLPath *P = paths[p];
        for (int i = 0; i < half; i++) {
            P->Cb[stage*N + offset + i] =
                P->Cb[(stage-1)*N + offset + i] ^ P->Cb[(stage-1)*N + offset + half + i];
            P->Cb[stage*N + offset + half + i] = P->Cb[(stage-1)*N + offset + half + i];
        }
    }
}

int polar_decode_scl(const PolarCodec *pc, const double *llr, int L,
                      int use_crc, CRCType crc_type,
                      int use_rnti, uint16_t rnti, int *decoded) {
    int N = pc->N, K = pc->K;
    int n = (int)(log2((double)N) + 0.5);
    int nk = n + 1;
    int cap = 2 * L;

    SCLPath *store = (SCLPath *)malloc((size_t)cap * sizeof(SCLPath));
    SCLPath **paths = (SCLPath **)malloc((size_t)cap * sizeof(SCLPath *));
    for (int i = 0; i < cap; i++) {
        store[i].Lb = (double *)calloc((size_t)nk * N, sizeof(double));
        store[i].Cb = (int *)calloc((size_t)nk * N, sizeof(int));
        store[i].cp = (int *)malloc((size_t)K * sizeof(int));
        store[i].k = 0;
        store[i].pm = 0.0;
        memset(store[i].sh, 0, sizeof(store[i].sh));
        paths[i] = &store[i];
    }
    for (int i = 0; i < N; i++) paths[0]->Lb[n*N + i] = llr[i];
    int num_active = 1;

    scl_recurse(paths, &num_active, N, n, 0, pc, L);

    /* rank surviving candidates by path metric, undo interleaving, and
     * pick the best CRC-passing one (or the single best if use_crc==0
     * or none pass). */
    qsort(paths, (size_t)num_active, sizeof(SCLPath *), cmp_path_pm);

    int *cand = (int *)malloc((size_t)K * sizeof(int));
    int found = 0;
    for (int p = 0; p < num_active && !found; p++) {
        SCLPath *P = paths[p];
        if (!pc->I_IL) {
            memcpy(cand, P->cp, (size_t)K * sizeof(int));
        } else {
            int k = 0;
            for (int m = 0; m < POLAR_KILMAX; m++) {
                int v = POLAR_PI_IL_MAX[m];
                if (v >= POLAR_KILMAX - K) {
                    int pik = v - (POLAR_KILMAX - K);
                    cand[pik] = P->cp[k];
                    k++;
                }
            }
        }
        if (p == 0) memcpy(decoded, cand, (size_t)K * sizeof(int));
        int crc_ok = !use_crc ||
                     (use_rnti ? check_crc_rnti(cand, K, crc_type, rnti)
                               : check_crc(cand, K, crc_type));
        if (crc_ok) {
            memcpy(decoded, cand, (size_t)K * sizeof(int));
            found = 1;
        }
    }
    /* if no candidate passed CRC, decoded[] already holds the p=0
     * (lowest-path-metric) candidate saved above -- best-effort fallback. */
    free(cand);

    for (int i = 0; i < cap; i++) { free(store[i].Lb); free(store[i].Cb); free(store[i].cp); }
    free(store);
    free(paths);

    return (use_crc ? found : 1);
}
