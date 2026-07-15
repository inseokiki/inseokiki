/* ================================================================
 *  utils.h
 *  Common utilities -- RNG, complex type, misc helpers
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef UTILS_H
#define UTILS_H

#include <complex.h>
#include <math.h>
#include <stddef.h>

typedef double complex cx_t;

#define PHY_PI   3.14159265358979323846
#define CX_ZERO  (0.0 + 0.0*_Complex_I)
#define CX_ONE   (1.0 + 0.0*_Complex_I)
#define CX_NORM(z) (creal(z)*creal(z) + cimag(z)*cimag(z))
#define CX_MAKE(r,i) ((double)(r) + (double)(i)*_Complex_I)

/* RNG */
void rng_seed(unsigned int seed);
double randn(void);

/* Generate n random bits (0 or 1) into out[n] */
void gen_random_bits(int *out, int n);

/* Uniform random integer in [0, n) */
int rand_uniform_int(int n);

/* BER: returns error rate, n must match */
double calc_ber(const int *tx, const int *rx, int n);

#endif
