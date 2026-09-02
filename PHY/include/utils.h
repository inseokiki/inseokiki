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

/* 4×4 복소 Hermitian 행렬(A_in, 대각 원소는 실수)의 고유분해 — 표준
 * Cyclic Jacobi 고유값 알고리즘의 Hermitian 확장(각 비대각 원소를 위상
 * 회전으로 실수화한 뒤 실수 Jacobi 회전 θ=½·atan2(2r,a_pp−a_qq) 적용,
 * 여러 sweep 반복). 고유값은 내림차순(eigval[0]>=...>=eigval[3])으로
 * 정렬되고 eigvec의 각 열이 대응하는 정규직교 고유벡터다.
 * eigen_16port.c(DL EIGEN_16PORT, Tx측 4×4 SVD)와 ul_eigen_bf.c(UL
 * Eigen-BF 4-Tx 확장, Rx측 4×4 SVD)가 공유하는 범용 선형대수
 * 유틸리티(2026-09-02, eigen_16port.c의 static 함수를 여기로 추출해
 * 재사용 — 둘 다 4×4 Gram 행렬의 완전한 스펙트럼이 필요한 동일 문제). */
void herm4x4_eig(const cx_t A_in[4][4], double eigval[4], cx_t eigvec[4][4]);

#endif
