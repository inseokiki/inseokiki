#include "awgn.h"
#include <math.h>
#include <stdlib.h>

/* Box-Muller transform: generates i.i.d. N(0,1) samples. */
static double randn(void) {
    static int  has_spare = 0;
    static double spare;
    if (has_spare) { has_spare = 0; return spare; }
    double u, v, s;
    do {
        u = 2.0 * rand() / ((double)RAND_MAX + 1.0) - 1.0;
        v = 2.0 * rand() / ((double)RAND_MAX + 1.0) - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    double m = sqrt(-2.0 * log(s) / s);
    spare = v * m;
    has_spare = 1;
    return u * m;
}

void awgn_add(const cx_t *in, int n, int qm, double eb_n0_lin, cx_t *out) {
    /* For unit-power symbols (Es=1) and given Eb/N0:
     *   Es/N0 = Qm * Eb/N0
     *   Noise variance per real component = 1 / (2 * Es/N0)
     *   sigma = sqrt(1 / (2 * Qm * Eb/N0))               */
    double sigma = sqrt(1.0 / (2.0 * (double)qm * eb_n0_lin));
    for (int i = 0; i < n; i++) {
        out[i].re = in[i].re + sigma * randn();
        out[i].im = in[i].im + sigma * randn();
    }
}
