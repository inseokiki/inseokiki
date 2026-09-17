#include "codebook_8port.h"
#include <math.h>
#include <stdio.h>

static int selected_rank(const cx_t H[4][8], double N0,
                         int *i1, int *i13, int *i2) {
    int rank, r1_i1, r1_i2, r2_i1, r2_i13, r2_i2;
    codebook_type1_sp_8port_ri_pmi_select(H, N0, &rank, i1, i13, i2,
        &r1_i1, &r1_i2, &r2_i1, &r2_i13, &r2_i2);
    return rank;
}

int main(void) {
    int i1, i13, i2;
    const double N0 = 0.01;

    /* One nonzero channel coefficient: every rank-1 beam has power 1/8. */
    cx_t rank1_H[4][8] = {{0}};
    rank1_H[0][0] = 1.0;
    int rank = selected_rank(rank1_H, N0, &i1, &i13, &i2);
    double eff_db = codebook_type1_sp_8port_effective_snr_db(
        rank1_H, N0, rank, i1, i13, i2);
    double expected = 10.0 * log10(1.0 / (8.0 * N0));
    if (rank != 1 || fabs(eff_db - expected) > 1e-10) {
        fprintf(stderr, "rank-1 effective SNR: rank=%d, got %.12f, expected %.12f\n",
                rank, eff_db, expected);
        return 1;
    }

    /* Four independent Rx directions support two well separated layers. */
    cx_t rank2_H[4][8] = {{0}};
    rank2_H[0][0] = 1.0;
    rank2_H[1][1] = 1.0;
    rank2_H[2][4] = 1.0;
    rank2_H[3][5] = 1.0;
    rank = selected_rank(rank2_H, N0, &i1, &i13, &i2);
    eff_db = codebook_type1_sp_8port_effective_snr_db(
        rank2_H, N0, rank, i1, i13, i2);
    if (rank != 2 || !isfinite(eff_db)) {
        fprintf(stderr, "rank-2 selection/effective SNR: rank=%d, eff=%.12f\n",
                rank, eff_db);
        return 1;
    }

    /* Recompute post-MMSE capacity from the selected 8x2 precoder. */
    cx_t W[8][2], he[4][2] = {{0}};
    codebook_type1_sp_8port_rank2(i1, i13, i2, W);
    for (int r = 0; r < 4; r++)
        for (int l = 0; l < 2; l++)
            for (int t = 0; t < 8; t++) he[r][l] += rank2_H[r][t] * W[t][l];
    double a = N0, d = N0;
    cx_t b = 0.0;
    for (int r = 0; r < 4; r++) {
        a += CX_NORM(he[r][0]);
        d += CX_NORM(he[r][1]);
        b += conj(he[r][0]) * he[r][1];
    }
    double det = a * d - CX_NORM(b);
    double layer0 = 1.0 / (N0 * d / det);
    double layer1 = 1.0 / (N0 * a / det);
    expected = 10.0 * log10(sqrt(layer0 * layer1) - 1.0);
    if (fabs(eff_db - expected) > 1e-8) {
        fprintf(stderr, "rank-2 effective SNR: got %.12f, expected %.12f\n",
                eff_db, expected);
        return 1;
    }
    puts("8-port OLLA RI/PMI and rank-aware effective SNR: PASS");
    return 0;
}
