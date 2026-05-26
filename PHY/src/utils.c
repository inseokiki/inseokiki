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

double calc_ber(const int *tx, const int *rx, int n) {
    int e = 0;
    for (int i = 0; i < n; i++) if (tx[i] != rx[i]) e++;
    return (double)e / n;
}
