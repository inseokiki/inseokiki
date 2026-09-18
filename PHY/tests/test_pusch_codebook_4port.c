#include "pusch_codebook_4port.h"
#include <math.h>
#include <stdio.h>

int main(void) {
    static const int counts[5] = {0, 28, 22, 7, 5};
    for (int rank = 1; rank <= 4; rank++) {
        if (ul_cb4_tpmi_count(rank) != counts[rank]) return 1;
        for (int p = 0; p < counts[rank]; p++) {
            cx_t W[4][4];
            if (ul_cb4_precoder(rank, p, W)) return 1;
            double power = 0.0;
            for (int t = 0; t < 4; t++)
                for (int l = 0; l < rank; l++) power += CX_NORM(W[t][l]);
            double beta = ul_cb4_unit_power_beta((const cx_t (*)[4])W, rank);
            if (fabs(power*beta*beta - 1.0) > 1e-12) return 1;
            for (int a = 0; a < rank; a++)
                for (int b = 0; b < rank; b++) {
                    cx_t dot = 0;
                    for (int t = 0; t < 4; t++) dot += conj(W[t][a]) * W[t][b];
                    if ((a != b && cabs(dot) > 1e-12) ||
                        (a == b && (creal(dot) <= 0.0 || fabs(cimag(dot)) > 1e-12))) {
                        fprintf(stderr, "rank=%d TPMI=%d Gram[%d,%d] mismatch\n", rank,p,a,b);
                        return 1;
                    }
                }
        }
    }
    cx_t W[4][4];
    if (ul_cb4_precoder(4, 5, W) == 0 || ul_cb4_precoder(0, 0, W) == 0) return 1;

    ul_cb4_precoder(1, 15, W);
    if (cabs(W[0][0] - 0.5) > 1e-12 ||
        cabs(W[1][0] - 0.5) > 1e-12 ||
        cabs(W[2][0] + CX_MAKE(0.0,0.5)) > 1e-12 ||
        cabs(W[3][0] + CX_MAKE(0.0,0.5)) > 1e-12) return 1;
    ul_cb4_precoder(2, 21, W);
    if (cabs(W[0][0] - 1.0/(2.0*sqrt(2.0))) > 1e-12 ||
        cabs(W[1][0] + CX_MAKE(0.0,1.0/(2.0*sqrt(2.0)))) > 1e-12) return 1;
    ul_cb4_precoder(3, 6, W);
    if (cabs(W[0][0] - 1.0/(2.0*sqrt(3.0))) > 1e-12 ||
        cabs(W[2][0] - CX_MAKE(0.0,1.0/(2.0*sqrt(3.0)))) > 1e-12) return 1;
    ul_cb4_precoder(3, 1, W);
    if (cabs(W[2][0] - 0.5) > 1e-12 || cabs(W[3][2] - 0.5) > 1e-12 ||
        cabs(W[2][2]) > 1e-12) return 1;
    ul_cb4_precoder(3, 2, W);
    if (cabs(W[2][0] + 0.5) > 1e-12 || cabs(W[3][2] - 0.5) > 1e-12 ||
        cabs(W[2][2]) > 1e-12) return 1;

    /* Independently transcribed entries from TS 38.211 Table 6.3.1.5-7. */
    ul_cb4_precoder(4, 1, W);
    if (cabs(W[0][0] - 1.0/(2.0*sqrt(2.0))) > 1e-12 ||
        cabs(W[2][1] + 1.0/(2.0*sqrt(2.0))) > 1e-12 ||
        cabs(W[1][2] - 1.0/(2.0*sqrt(2.0))) > 1e-12) return 1;
    ul_cb4_precoder(4, 4, W);
    if (cabs(W[0][0] - 0.25) > 1e-12 ||
        cabs(W[2][0] - CX_MAKE(0.0,0.25)) > 1e-12 ||
        cabs(W[3][1] + CX_MAKE(0.0,0.25)) > 1e-12) return 1;

    cx_t H[4][4] = {{0}};
    H[0][0] = 1.0;
    for (int i = 1; i < 4; i++) H[i][i] = 0.1;
    int rank, tpmi;
    ul_cb4_select((const cx_t (*)[4])H, 100.0, &rank, &tpmi);
    if (rank != 1) { fprintf(stderr, "low-SNR rank=%d\n", rank); return 1; }
    for (int i = 1; i < 4; i++) H[i][i] = 1.0;
    ul_cb4_select((const cx_t (*)[4])H, 0.001, &rank, &tpmi);
    if (rank != 4) { fprintf(stderr, "high-SNR rank=%d\n", rank); return 1; }
    puts("UL four-port full TPMI sets and rank selection: PASS");
    return 0;
}
