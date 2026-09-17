/* test_codebook_geometry.c -- numeric unit tests for TS 38.214 §5.2.2.2.1
 * Type I Single Panel codebook GEOMETRY (per-codeword unit norm and
 * inter-layer orthogonality), across all three port counts this project
 * implements (4/8/32).
 *
 * Scope: tasks/todo.md's "SU-MIMO codebook 기하 검증(Type I SP 코드북
 * 벡터 단위노름/직교성 등)" item -- the last still-open row of
 * `PHY_UNIT_VALIDATION_PLAN.md` §2's "SU-MIMO 검출·EVD·codebook" table
 * (EVD covered by test_matrix.c, detection by test_mimo_detection.c, and
 * the RI/PMI DECISION logic by test_codebook_ri_pmi.c -- this file is
 * specifically the codeword geometry those other files deliberately left
 * out). codebook_32port.c already has its own exhaustive geometry
 * verification INSIDE codebook_type1_sp_32port_print() (max-error
 * printf, not an assertion) -- this file exercises the exact same
 * exhaustive candidate sets and formulas, but as pass/fail CHECKs
 * against a numeric tolerance instead of a human-read printout, and adds
 * equivalent exhaustive coverage for the 4-port/8-port codebooks (whose
 * print functions only ever displayed per-codeword values, never
 * aggregated a max error either).
 *
 * Build/run: see PHY/tests/README.md (run_numeric_tests.sh drives this).
 */
#include "codebook.h"
#include "codebook_8port.h"
#include "codebook_32port.h"
#include <stdio.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

#define TOL 1e-9

/* ── 4-port (codebook.c) ─────────────────────────────────────────────── */

static void codebook_4port_rank1_geometry(void) {
    double max_err = 0.0;
    long count = 0;
    for (int i1 = 0; i1 < CB_BEAMS; i1++) {
        for (int i2 = 0; i2 < CB_COPHASE; i2++) {
            cx_t W[4];
            codebook_type1_sp_4port_rank1(i1, i2, W);
            double norm2 = 0.0;
            for (int p = 0; p < 4; p++) norm2 += CX_NORM(W[p]);
            double e = fabs(norm2 - 1.0);
            if (e > max_err) max_err = e;
            count++;
        }
    }
    char m[160];
    snprintf(m, sizeof(m), "4-port rank-1: all %ld codewords have ||W||^2==1 (max err=%.3e)", count, max_err);
    CHECK(count == CB_RANK1_TOTAL && max_err < TOL, m);
}

static void codebook_4port_rank2_geometry(void) {
    double max_norm_err = 0.0, max_orth_err = 0.0;
    long count = 0;
    for (int i1 = 0; i1 < CB_BEAMS; i1++) {
        for (int i13 = 0; i13 <= 1; i13++) {
            for (int i2 = 0; i2 < CB_COPHASE; i2++) {
                cx_t W[4][2];
                codebook_type1_sp_4port_rank2(i1, i13, i2, W);
                double n0 = 0.0, n1 = 0.0; cx_t dot = CX_ZERO;
                for (int p = 0; p < 4; p++) {
                    n0  += CX_NORM(W[p][0]);
                    n1  += CX_NORM(W[p][1]);
                    dot += conj(W[p][0]) * W[p][1];
                }
                double ne = fabs(n0 - 0.5) + fabs(n1 - 0.5);
                if (ne > max_norm_err) max_norm_err = ne;
                if (cabs(dot) > max_orth_err) max_orth_err = cabs(dot);
                count++;
            }
        }
    }
    char m[200];
    snprintf(m, sizeof(m), "4-port rank-2: all %ld codewords have ||W[:,c]||^2==0.5 both columns AND W[:,0]^H W[:,1]==0 (max norm err=%.3e, max orth err=%.3e)",
             count, max_norm_err, max_orth_err);
    CHECK(count == CB_BEAMS * 2 * CB_COPHASE && max_norm_err < TOL && max_orth_err < TOL, m);
}

/* ── 8-port (codebook_8port.c) ───────────────────────────────────────── */

static void codebook_8port_rank1_geometry(void) {
    double max_err = 0.0;
    long count = 0;
    for (int i1 = 0; i1 < CB8_BEAMS; i1++) {
        for (int i2 = 0; i2 < CB8_COPHASE; i2++) {
            cx_t W[8];
            codebook_type1_sp_8port_rank1(i1, i2, W);
            double norm2 = 0.0;
            for (int p = 0; p < 8; p++) norm2 += CX_NORM(W[p]);
            double e = fabs(norm2 - 1.0);
            if (e > max_err) max_err = e;
            count++;
        }
    }
    char m[160];
    snprintf(m, sizeof(m), "8-port rank-1: all %ld codewords have ||W||^2==1 (max err=%.3e)", count, max_err);
    CHECK(count == CB8_RANK1_TOTAL && max_err < TOL, m);
}

static void codebook_8port_rank2_geometry(void) {
    double max_norm_err = 0.0, max_orth_err = 0.0;
    long count = 0;
    for (int i1 = 0; i1 < CB8_BEAMS; i1++) {
        for (int i13 = 0; i13 < CB8_I13_COUNT; i13++) {
            for (int i2 = 0; i2 < CB8_COPHASE; i2++) {
                cx_t W[8][2];
                codebook_type1_sp_8port_rank2(i1, i13, i2, W);
                double n0 = 0.0, n1 = 0.0; cx_t dot = CX_ZERO;
                for (int p = 0; p < 8; p++) {
                    n0  += CX_NORM(W[p][0]);
                    n1  += CX_NORM(W[p][1]);
                    dot += conj(W[p][0]) * W[p][1];
                }
                double ne = fabs(n0 - 0.5) + fabs(n1 - 0.5);
                if (ne > max_norm_err) max_norm_err = ne;
                if (cabs(dot) > max_orth_err) max_orth_err = cabs(dot);
                count++;
            }
        }
    }
    char m[200];
    snprintf(m, sizeof(m), "8-port rank-2: all %ld codewords have ||W[:,c]||^2==0.5 both columns AND W[:,0]^H W[:,1]==0 (max norm err=%.3e, max orth err=%.3e)",
             count, max_norm_err, max_orth_err);
    CHECK(count == CB8_RANK2_TOTAL && max_norm_err < TOL && max_orth_err < TOL, m);
}

/* ── 32-port (codebook_32port.c) ─────────────────────────────────────── */

static void codebook_32port_rank1_geometry(void) {
    double max_err = 0.0;
    long count = 0;
    for (int l = 0; l < CB32_L_COUNT; l++) {
        for (int m = 0; m < CB32_M_COUNT; m++) {
            for (int n = 0; n < CB32_I2_R1; n++) {
                cx_t W[32];
                codebook_type1_sp_32port_rank1(l, m, n, W);
                double norm2 = 0.0;
                for (int p = 0; p < 32; p++) norm2 += CX_NORM(W[p]);
                double e = fabs(norm2 - 1.0);
                if (e > max_err) max_err = e;
                count++;
            }
        }
    }
    char msg[160];
    snprintf(msg, sizeof(msg), "32-port rank-1: all %ld codewords have ||W||^2==1 (max err=%.3e)", count, max_err);
    CHECK(count == CB32_RANK1_TOTAL && max_err < TOL, msg);
}

static void codebook_32port_rank2_geometry(void) {
    double max_norm_err = 0.0, max_orth_err = 0.0;
    long count = 0;
    for (int l = 0; l < CB32_L_COUNT; l++) {
        for (int m = 0; m < CB32_M_COUNT; m++) {
            for (int i13 = 0; i13 < CB32_I13_R2; i13++) {
                for (int n = 0; n < CB32_I2_R234; n++) {
                    cx_t W[32][2];
                    codebook_type1_sp_32port_rank2(l, m, i13, n, W);
                    double n0 = 0.0, n1 = 0.0; cx_t dot = CX_ZERO;
                    for (int p = 0; p < 32; p++) {
                        n0  += CX_NORM(W[p][0]);
                        n1  += CX_NORM(W[p][1]);
                        dot += conj(W[p][0]) * W[p][1];
                    }
                    double ne = fabs(n0 - 0.5) + fabs(n1 - 0.5);
                    if (ne > max_norm_err) max_norm_err = ne;
                    if (cabs(dot) > max_orth_err) max_orth_err = cabs(dot);
                    count++;
                }
            }
        }
    }
    char msg[200];
    snprintf(msg, sizeof(msg), "32-port rank-2: all %ld codewords have ||W[:,c]||^2==0.5 both columns AND W[:,0]^H W[:,1]==0 (max norm err=%.3e, max orth err=%.3e)",
             count, max_norm_err, max_orth_err);
    CHECK(count == CB32_RANK2_TOTAL && max_norm_err < TOL && max_orth_err < TOL, msg);
}

/* Rank-3/4 share the same candidate grid and pairwise-orthogonality
 * structure -- one parameterized check, mirroring exactly the loop
 * codebook_type1_sp_32port_print() already runs for rank in {3,4}. */
static void codebook_32port_rank34_geometry(int rank) {
    double max_norm_err = 0.0, max_orth_err = 0.0;
    long count = 0;
    double target = 1.0 / rank;
    for (int l = 0; l < CB32_L34_COUNT; l++) {
        for (int m = 0; m < CB32_M_COUNT; m++) {
            for (int p13 = 0; p13 < CB32_THETA_R34; p13++) {
                for (int n = 0; n < CB32_I2_R234; n++) {
                    double norms[4] = {0,0,0,0};
                    cx_t dots[4][4];
                    for (int a = 0; a < rank; a++)
                        for (int b = 0; b < rank; b++) dots[a][b] = CX_ZERO;

                    if (rank == 3) {
                        cx_t W[32][3];
                        codebook_type1_sp_32port_rank3(l, m, p13, n, W);
                        for (int c = 0; c < 3; c++)
                            for (int k = 0; k < 32; k++) norms[c] += CX_NORM(W[k][c]);
                        for (int a = 0; a < 3; a++)
                            for (int b = 0; b < 3; b++)
                                for (int k = 0; k < 32; k++)
                                    dots[a][b] += conj(W[k][a]) * W[k][b];
                    } else {
                        cx_t W[32][4];
                        codebook_type1_sp_32port_rank4(l, m, p13, n, W);
                        for (int c = 0; c < 4; c++)
                            for (int k = 0; k < 32; k++) norms[c] += CX_NORM(W[k][c]);
                        for (int a = 0; a < 4; a++)
                            for (int b = 0; b < 4; b++)
                                for (int k = 0; k < 32; k++)
                                    dots[a][b] += conj(W[k][a]) * W[k][b];
                    }

                    for (int c = 0; c < rank; c++) {
                        double e = fabs(norms[c] - target);
                        if (e > max_norm_err) max_norm_err = e;
                    }
                    for (int a = 0; a < rank; a++)
                        for (int b = 0; b < rank; b++) {
                            if (a == b) continue;
                            double e = cabs(dots[a][b]);
                            if (e > max_orth_err) max_orth_err = e;
                        }
                    count++;
                }
            }
        }
    }
    char msg[220];
    snprintf(msg, sizeof(msg), "32-port rank-%d: all %ld codewords have ||W[:,c]||^2==1/%d every column AND all %d pairwise cross-column products==0 (max norm err=%.3e, max orth err=%.3e)",
             rank, count, rank, rank * (rank - 1), max_norm_err, max_orth_err);
    long expect_count = rank == 3 ? CB32_RANK3_TOTAL : CB32_RANK4_TOTAL;
    CHECK(count == expect_count && max_norm_err < TOL && max_orth_err < TOL, msg);
}

int main(void) {
    codebook_4port_rank1_geometry();
    codebook_4port_rank2_geometry();
    codebook_8port_rank1_geometry();
    codebook_8port_rank2_geometry();
    codebook_32port_rank1_geometry();
    codebook_32port_rank2_geometry();
    codebook_32port_rank34_geometry(3);
    codebook_32port_rank34_geometry(4);

    printf("\n%s\n", g_fail == 0 ? "ALL PASS" : "SOME FAILED");
    return g_fail == 0 ? 0 : 1;
}
