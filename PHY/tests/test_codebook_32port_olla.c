#include "codebook_32port.h"
#include <math.h>
#include <stdio.h>

int main(void) {
    const double N0 = 0.1;
    const double gain = 2.0;
    for (int rank = 1; rank <= 4; rank++) {
        cx_t W[32][4] = {{0}};
        cx_t H[4][32] = {{0}};
        if (rank == 1) {
            cx_t w[32];
            codebook_type1_sp_32port_rank1(0, 0, 0, w);
            for (int t = 0; t < 32; t++) W[t][0] = w[t];
        } else if (rank == 2) {
            cx_t w[32][2];
            codebook_type1_sp_32port_rank2(0, 0, 0, 0, w);
            for (int t = 0; t < 32; t++)
                for (int l = 0; l < rank; l++) W[t][l] = w[t][l];
        } else if (rank == 3) {
            cx_t w[32][3];
            codebook_type1_sp_32port_rank3(0, 0, 0, 0, w);
            for (int t = 0; t < 32; t++)
                for (int l = 0; l < rank; l++) W[t][l] = w[t][l];
        } else {
            cx_t w[32][4];
            codebook_type1_sp_32port_rank4(0, 0, 0, 0, w);
            for (int t = 0; t < 32; t++)
                for (int l = 0; l < rank; l++) W[t][l] = w[t][l];
        }

        /* H is matched to the selected precoder. Orthogonal columns yield
         * a diagonal effective channel with gain/rank on each active layer. */
        for (int r = 0; r < rank; r++)
            for (int t = 0; t < 32; t++) H[r][t] = gain * conj(W[t][r]);
        for (int i = 0; i < rank; i++)
            for (int j = 0; j < rank; j++) {
                cx_t gram = 0.0;
                for (int t = 0; t < 32; t++) gram += conj(W[t][i]) * W[t][j];
                double expected = i == j ? 1.0 / rank : 0.0;
                if (cabs(gram - expected) > 1e-10) {
                    fprintf(stderr, "rank %d precoder orthogonality failed\n", rank);
                    return 1;
                }
            }
        double got = codebook_type1_sp_32port_effective_snr_db(H, N0, rank, 0, 0, 0, 0);
        double expected = 10.0 * log10(gain * gain / (rank * rank * N0));
        if (!isfinite(got) || fabs(got - expected) > 1e-8) {
            fprintf(stderr, "rank %d effective SNR: got %.12f, expected %.12f\n",
                    rank, got, expected);
            return 1;
        }
    }
    puts("32-port rank 1-4 effective SNR: PASS");
    return 0;
}
