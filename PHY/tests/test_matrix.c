/* test_matrix.c -- permanent numeric regression test for utils.c's
 * herm4x4_eig() (Cyclic Jacobi eigendecomposition of a 4x4 Hermitian
 * matrix). lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10 §5 2단계 "기초
 * 행렬" row -- eigen_16port.c (DL EIGEN_16PORT Tx-side SVD) and
 * ul_eigen_bf.c (UL Eigen-BF 4-Tx Rx-side SVD) both depend on this
 * being a correct spectral decomposition.
 *
 * Independence notes (UT-06 spirit): trace and Frobenius-norm
 * conservation under a unitary similarity transform are standard linear
 * algebra invariants (trace(A) = sum(eigenvalues), ||A||_F^2 =
 * sum(eigenvalues^2) for Hermitian A) that hold for ANY correct
 * eigendecomposition -- computed directly from the input matrix,
 * independent of herm4x4_eig()'s own Jacobi-rotation arithmetic. The
 * reconstruction check (A == V*diag(eigval)*V^H) and orthonormality
 * (V^H V == I) are the defining properties of an eigendecomposition
 * itself. The diagonal-input and rank-1-outer-product cases are
 * hand-computable closed forms, independent of the algorithm's
 * intermediate rotation steps.
 */
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

static void mat_zero(cx_t M[4][4]) {
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) M[i][j] = CX_ZERO;
}

/* A already diagonal (offdiag energy is exactly 0) -> herm4x4_eig's
 * Jacobi sweep loop breaks on its very first offdiag check, so V stays
 * the identity and only the final descending-sort permutation acts --
 * i.e. eigval must be the diagonal entries sorted descending, and
 * eigvec must be the EXACT corresponding permutation of identity
 * columns (all entries exactly 0 or 1, no numerical residue). */
static void test_diagonal_exact(void) {
    cx_t A[4][4]; mat_zero(A);
    double d[4] = { 5.0, 1.0, 9.0, 3.0 };
    for (int i = 0; i < 4; i++) A[i][i] = CX_MAKE(d[i], 0.0);

    double eigval[4]; cx_t eigvec[4][4];
    herm4x4_eig(A, eigval, eigvec);

    double expect_val[4] = { 9.0, 5.0, 3.0, 1.0 };
    int perm[4] = { 2, 0, 3, 1 };  /* original index feeding sorted slot k */
    int val_ok = 1, vec_ok = 1;
    for (int k = 0; k < 4; k++) {
        if (fabs(eigval[k] - expect_val[k]) > 1e-12) val_ok = 0;
        for (int r = 0; r < 4; r++) {
            cx_t expect = (r == perm[k]) ? CX_ONE : CX_ZERO;
            if (cabs(eigvec[r][k] - expect) > 1e-12) vec_ok = 0;
        }
    }
    CHECK(val_ok, "diagonal input: eigval == diagonal entries sorted descending, exact");
    CHECK(vec_ok, "diagonal input: eigvec == exact permutation of identity columns (no rotation applied)");
}

static void test_identity(void) {
    cx_t A[4][4]; mat_zero(A);
    for (int i = 0; i < 4; i++) A[i][i] = CX_ONE;
    double eigval[4]; cx_t eigvec[4][4];
    herm4x4_eig(A, eigval, eigvec);
    int ok = 1;
    for (int i = 0; i < 4; i++) if (fabs(eigval[i] - 1.0) > 1e-12) ok = 0;
    CHECK(ok, "identity input: all 4 eigenvalues == 1.0 exactly");
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            cx_t expect = (i == j) ? CX_ONE : CX_ZERO;
            if (cabs(eigvec[i][j] - expect) > 1e-12) ok = 0;
        }
    CHECK(ok, "identity input: eigvec == identity matrix exactly");
}

/* Rank-1 outer product A = v*v^H for a known real vector v: closed-form
 * spectrum is [|v|^2, 0, 0, 0] with the top eigenvector exactly v/|v|
 * (up to an unobservable global phase) -- a standard linear-algebra
 * fact independent of the Jacobi rotation path taken to get there. */
static void test_rank1_outer_product(void) {
    double v[4] = { 1.0, 2.0, 3.0, 4.0 };
    double vnorm2 = 0.0;
    for (int i = 0; i < 4; i++) vnorm2 += v[i] * v[i];   /* = 30 */

    cx_t A[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) A[i][j] = CX_MAKE(v[i] * v[j], 0.0);

    double eigval[4]; cx_t eigvec[4][4];
    herm4x4_eig(A, eigval, eigvec);

    char m[128];
    snprintf(m, sizeof(m), "rank-1 outer product v*v^H: top eigenvalue == |v|^2=%.1f (got %.9f)", vnorm2, eigval[0]);
    CHECK(fabs(eigval[0] - vnorm2) < 1e-8, m);

    int rest_zero = 1;
    for (int i = 1; i < 4; i++) if (fabs(eigval[i]) > 1e-8) rest_zero = 0;
    CHECK(rest_zero, "rank-1 outer product v*v^H: remaining 3 eigenvalues are exactly 0 (matrix is singular of rank 1)");

    /* |<eigvec_col0, v>|^2 should equal |v|^2 (perfect alignment, up to
     * a global phase eigvec_col0 already absorbs since v is real). */
    cx_t dot = CX_ZERO;
    for (int i = 0; i < 4; i++) dot += conj(eigvec[i][0]) * v[i];
    double align = CX_NORM(dot);
    snprintf(m, sizeof(m), "rank-1 outer product v*v^H: top eigenvector is exactly v/|v| (|<eigvec0,v>|^2=%.6f, expect %.6f)", align, vnorm2);
    CHECK(fabs(align - vnorm2) < 1e-6, m);
}

static void rand_hermitian(cx_t A[4][4]) {
    cx_t G[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            G[i][j] = CX_MAKE(randn(), randn());
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            A[i][j] = G[i][j] + conj(G[j][i]);   /* A = G + G^H is always Hermitian */
}

/* trace(A) = sum(eigenvalues) and ||A||_F^2 = sum(eigenvalues^2) hold
 * for ANY correct spectral decomposition of a Hermitian matrix -- these
 * are computed straight from the input A, so they don't depend on
 * herm4x4_eig()'s own arithmetic being right in any particular step. */
static void test_trace_and_frobenius_invariants(int ntrials) {
    int trace_ok = 1, frob_ok = 1;
    double max_trace_err = 0.0, max_frob_err = 0.0;
    for (int t = 0; t < ntrials; t++) {
        cx_t A[4][4]; rand_hermitian(A);
        double eigval[4]; cx_t eigvec[4][4];
        herm4x4_eig(A, eigval, eigvec);

        double trace_A = 0.0;
        for (int i = 0; i < 4; i++) trace_A += creal(A[i][i]);
        double trace_eig = 0.0;
        for (int i = 0; i < 4; i++) trace_eig += eigval[i];
        double te = fabs(trace_A - trace_eig);
        if (te > max_trace_err) max_trace_err = te;
        if (te > 1e-6) trace_ok = 0;

        double frob_A = 0.0;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) frob_A += CX_NORM(A[i][j]);
        double frob_eig = 0.0;
        for (int i = 0; i < 4; i++) frob_eig += eigval[i] * eigval[i];
        double fe = fabs(frob_A - frob_eig);
        if (fe > max_frob_err) max_frob_err = fe;
        if (fe > 1e-4) frob_ok = 0;
    }
    char m[160];
    snprintf(m, sizeof(m), "%d random Hermitian trials: trace(A) == sum(eigenvalues) (max err %.3e)", ntrials, max_trace_err);
    CHECK(trace_ok, m);
    snprintf(m, sizeof(m), "%d random Hermitian trials: ||A||_F^2 == sum(eigenvalues^2) (max err %.3e)", ntrials, max_frob_err);
    CHECK(frob_ok, m);
}

/* The defining property of an eigendecomposition: A == V*diag(eigval)*V^H,
 * and V must have orthonormal columns (V^H V == I). */
static void test_reconstruction_and_orthonormal(int ntrials) {
    int recon_ok = 1, orth_ok = 1;
    double max_recon_err = 0.0, max_orth_err = 0.0;
    for (int t = 0; t < ntrials; t++) {
        cx_t A[4][4]; rand_hermitian(A);
        double eigval[4]; cx_t eigvec[4][4];
        herm4x4_eig(A, eigval, eigvec);

        /* VtV = V^H V */
        cx_t VtV[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                cx_t s = CX_ZERO;
                for (int k = 0; k < 4; k++) s += conj(eigvec[k][i]) * eigvec[k][j];
                VtV[i][j] = s;
            }
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                cx_t expect = (i == j) ? CX_ONE : CX_ZERO;
                double e = cabs(VtV[i][j] - expect);
                if (e > max_orth_err) max_orth_err = e;
                if (e > 1e-8) orth_ok = 0;
            }

        /* A_rec = V*diag(eigval)*V^H */
        cx_t A_rec[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                cx_t s = CX_ZERO;
                for (int k = 0; k < 4; k++) s += eigvec[i][k] * eigval[k] * conj(eigvec[j][k]);
                A_rec[i][j] = s;
            }
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                double e = cabs(A_rec[i][j] - A[i][j]);
                if (e > max_recon_err) max_recon_err = e;
                if (e > 1e-6) recon_ok = 0;
            }
    }
    char m[160];
    snprintf(m, sizeof(m), "%d random Hermitian trials: V^H V == I (orthonormal columns, max err %.3e)", ntrials, max_orth_err);
    CHECK(orth_ok, m);
    snprintf(m, sizeof(m), "%d random Hermitian trials: V*diag(eigval)*V^H == A (max err %.3e)", ntrials, max_recon_err);
    CHECK(recon_ok, m);
}

static void test_eigval_sorted_descending(int ntrials) {
    int ok = 1;
    for (int t = 0; t < ntrials; t++) {
        cx_t A[4][4]; rand_hermitian(A);
        double eigval[4]; cx_t eigvec[4][4];
        herm4x4_eig(A, eigval, eigvec);
        for (int i = 0; i < 3; i++) if (eigval[i] < eigval[i + 1] - 1e-12) ok = 0;
    }
    char m[96]; snprintf(m, sizeof(m), "%d random Hermitian trials: eigval[0] >= eigval[1] >= eigval[2] >= eigval[3]", ntrials);
    CHECK(ok, m);
}

int main(void) {
    test_diagonal_exact();
    test_identity();
    test_rank1_outer_product();

    rng_seed(4747);
    test_trace_and_frobenius_invariants(500);
    test_reconstruction_and_orthonormal(500);
    test_eigval_sorted_descending(500);

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
