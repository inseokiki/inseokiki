/* ================================================================
 *  utils.c
 *  Common utilities -- RNG, complex type, misc helpers
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "utils.h"
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* xorshift64 RNG */
static unsigned long long rng_s = 0x9e3779b97f4a7c15ULL;

void rng_seed(unsigned int seed) {
    rng_s = (unsigned long long)seed | 1ULL;
}

static unsigned long long rng_next(void) {
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 7;
    rng_s ^= rng_s << 17;
    return rng_s;
}

static double rng_uniform(void) {
    return (double)(rng_next() >> 11) * (1.0 / (double)(1ULL << 53));
}

double randn(void) {
    static int spare_valid = 0;
    static double spare;
    if (spare_valid) { spare_valid = 0; return spare; }
    double u, v, s;
    do {
        u = 2.0 * rng_uniform() - 1.0;
        v = 2.0 * rng_uniform() - 1.0;
        s = u*u + v*v;
    } while (s >= 1.0 || s == 0.0);
    double m = sqrt(-2.0 * log(s) / s);
    spare = v * m;
    spare_valid = 1;
    return u * m;
}

void gen_random_bits(int *out, int n) {
    for (int i = 0; i < n; i++)
        out[i] = (int)(rng_next() & 1);
}

int rand_uniform_int(int n) {
    if (n <= 1) return 0;
    return (int)(rng_uniform() * n);
}

double calc_ber(const int *tx, const int *rx, int n) {
    int e = 0;
    for (int i = 0; i < n; i++) if (tx[i] != rx[i]) e++;
    return (double)e / n;
}

void herm4x4_eig(const cx_t A_in[4][4], double eigval[4], cx_t eigvec[4][4]) {
    cx_t A[4][4], V[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            A[i][j] = A_in[i][j];
            V[i][j] = (i == j) ? 1.0 : 0.0;
        }

    for (int sweep = 0; sweep < 60; sweep++) {
        double offdiag = 0.0;
        for (int p = 0; p < 4; p++)
            for (int q = p + 1; q < 4; q++) offdiag += CX_NORM(A[p][q]);
        if (offdiag < 1e-28) break;

        for (int p = 0; p < 3; p++) {
            for (int q = p + 1; q < 4; q++) {
                cx_t apq = A[p][q];
                double r = cabs(apq);
                if (r < 1e-15) continue;

                double beta = carg(apq);
                double app  = creal(A[p][p]);
                double aqq  = creal(A[q][q]);
                double theta = 0.5 * atan2(2.0 * r, app - aqq);
                double c = cos(theta), s = sin(theta);
                cx_t emib = CX_MAKE(cos(-beta), sin(-beta));   /* e^{-iβ} */

                cx_t J[4][4];
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) J[i][j] = (i == j) ? 1.0 : 0.0;
                J[p][p] = c;         J[p][q] = -s;
                J[q][p] = s * emib;  J[q][q] = c * emib;

                /* A <- J^H A J */
                cx_t AJ[4][4];
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) {
                        AJ[i][j] = 0.0;
                        for (int k = 0; k < 4; k++) AJ[i][j] += A[i][k] * J[k][j];
                    }
                cx_t newA[4][4];
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) {
                        newA[i][j] = 0.0;
                        for (int k = 0; k < 4; k++) newA[i][j] += conj(J[k][i]) * AJ[k][j];
                    }
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) A[i][j] = newA[i][j];

                /* V <- V J (고유벡터 누적) */
                cx_t VJ[4][4];
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) {
                        VJ[i][j] = 0.0;
                        for (int k = 0; k < 4; k++) VJ[i][j] += V[i][k] * J[k][j];
                    }
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) V[i][j] = VJ[i][j];
            }
        }
    }

    for (int i = 0; i < 4; i++) eigval[i] = creal(A[i][i]);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) eigvec[i][j] = V[i][j];

    /* 고유값 내림차순 정렬 (고유벡터 열도 함께 치환) */
    for (int i = 0; i < 3; i++) {
        int maxi = i;
        for (int j = i + 1; j < 4; j++) if (eigval[j] > eigval[maxi]) maxi = j;
        if (maxi != i) {
            double tmp = eigval[i]; eigval[i] = eigval[maxi]; eigval[maxi] = tmp;
            for (int r = 0; r < 4; r++) {
                cx_t tv = eigvec[r][i]; eigvec[r][i] = eigvec[r][maxi]; eigvec[r][maxi] = tv;
            }
        }
    }
}
