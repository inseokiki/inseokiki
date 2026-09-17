#include "ul_codebook_4port.h"
#include "mimo.h"
#include <math.h>

int ul_cb4_tpmi_count(int rank) {
    static const int count[5] = {0, 28, 22, 7, 5};
    return rank >= 1 && rank <= 4 ? count[rank] : 0;
}

int ul_cb4_precoder(int rank, int tpmi, cx_t W[4][4]) {
    if (tpmi < 0 || tpmi >= ul_cb4_tpmi_count(rank)) return -1;
    for (int t = 0; t < 4; t++)
        for (int l = 0; l < 4; l++) W[t][l] = 0.0;

    if (rank == 1) {
        static const cx_t phase[4] = {1.0, -1.0, I, -I};
        if (tpmi < 4) W[tpmi][0] = 0.5;
        else if (tpmi < 8) {
            W[0][0] = 0.5; W[2][0] = 0.5 * phase[tpmi-4];
        } else if (tpmi < 12) {
            W[1][0] = 0.5; W[3][0] = 0.5 * phase[tpmi-8];
        } else {
            int q = (tpmi-12)/4, ph = (tpmi-12)%4;
            static const cx_t step[4] = {1.0, I, -1.0, -I};
            cx_t front = step[q];
            W[0][0] = 0.5; W[1][0] = 0.5*front;
            W[2][0] = 0.5*step[ph]; W[3][0] = 0.5*front*step[ph];
        }
    } else if (rank == 2) {
        static const int pair[6][2] = {
            {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}
        };
        if (tpmi < 6) {
            for (int l = 0; l < 2; l++) W[pair[tpmi][l]][l] = 0.5;
        } else if (tpmi < 14) {
            cx_t ph2[8] = {1.0, 1.0, -I, -I, -1.0, -1.0, I, I};
            cx_t ph3[8] = {-I, I, 1.0, -1.0, -I, I, 1.0, -1.0};
            int p = tpmi - 6;
            W[0][0] = 0.5; W[1][1] = 0.5;
            W[2][0] = 0.5*ph2[p]; W[3][1] = 0.5*ph3[p];
        } else {
            static const int re[8][4][2] = {
                {{1,1},{1,1},{1,-1},{1,-1}},
                {{1,1},{1,1},{0,0},{0,0}},
                {{1,1},{0,0},{1,-1},{0,0}},
                {{1,1},{0,0},{0,0},{-1,1}},
                {{1,1},{-1,-1},{1,-1},{-1,1}},
                {{1,1},{-1,-1},{0,0},{0,0}},
                {{1,1},{0,0},{1,-1},{0,0}},
                {{1,1},{0,0},{0,0},{1,-1}}
            };
            static const int im[8][4][2] = {
                {{0,0},{0,0},{0,0},{0,0}},
                {{0,0},{0,0},{1,-1},{1,-1}},
                {{0,0},{1,1},{0,0},{1,-1}},
                {{0,0},{1,1},{1,-1},{0,0}},
                {{0,0},{0,0},{0,0},{0,0}},
                {{0,0},{0,0},{1,-1},{-1,1}},
                {{0,0},{-1,-1},{0,0},{-1,1}},
                {{0,0},{-1,-1},{1,-1},{0,0}}
            };
            double a = 1.0/(2.0*sqrt(2.0));
            for (int t = 0; t < 4; t++)
                for (int l = 0; l < 2; l++)
                    W[t][l] = a * CX_MAKE(re[tpmi-14][t][l], im[tpmi-14][t][l]);
        }
    } else if (rank == 3) {
        if (tpmi == 0) {
            for (int l = 0; l < 3; l++) W[l][l] = 0.5;
        } else if (tpmi < 3) {
            W[0][0] = 0.5; W[1][1] = 0.5; W[3][2] = 0.5;
            W[2][0] = tpmi == 1 ? 0.5 : -0.5;
        } else {
            static const int re[4][4][3] = {
                {{1,1,1},{1,-1,1},{1,1,-1},{1,-1,-1}},
                {{1,1,1},{1,-1,1},{0,0,0},{0,0,0}},
                {{1,1,1},{-1,1,-1},{1,1,-1},{-1,1,1}},
                {{1,1,1},{-1,1,-1},{0,0,0},{0,0,0}}
            };
            static const int im[4][4][3] = {
                {{0,0,0},{0,0,0},{0,0,0},{0,0,0}},
                {{0,0,0},{0,0,0},{1,1,-1},{1,-1,-1}},
                {{0,0,0},{0,0,0},{0,0,0},{0,0,0}},
                {{0,0,0},{0,0,0},{1,1,-1},{-1,1,1}}
            };
            double a = 1.0/(2.0*sqrt(3.0));
            for (int t = 0; t < 4; t++)
                for (int l = 0; l < 3; l++)
                    W[t][l] = a * CX_MAKE(re[tpmi-3][t][l], im[tpmi-3][t][l]);
        }
    } else if (tpmi == 0) {
        for (int l = 0; l < 4; l++) W[l][l] = 0.5;
    } else if (tpmi == 1 || tpmi == 2) {
        cx_t ph = tpmi == 1 ? 1.0 : CX_MAKE(0.0, 1.0);
        double a = 1.0 / (2.0 * sqrt(2.0));
        W[0][0]=a; W[0][1]=a; W[2][0]=a*ph; W[2][1]=-a*ph;
        W[1][2]=a; W[1][3]=a; W[3][2]=a*ph; W[3][3]=-a*ph;
    } else {
        static const int sign[4][4] = {
            {1,1,1,1}, {1,-1,1,-1}, {1,1,-1,-1}, {1,-1,-1,1}
        };
        for (int t = 0; t < 4; t++)
            for (int l = 0; l < 4; l++) {
                cx_t ph = (tpmi == 4 && t >= 2) ? CX_MAKE(0.0, 1.0) : 1.0;
                W[t][l] = 0.25 * sign[t][l] * ph;
            }
    }
    return 0;
}

double ul_cb4_unit_power_beta(const cx_t W[4][4], int rank) {
    double power = 0.0;
    for (int t = 0; t < 4; t++)
        for (int l = 0; l < rank; l++) power += CX_NORM(W[t][l]);
    return power > 0.0 ? 1.0/sqrt(power) : 0.0;
}

void ul_cb4_select(const cx_t H[4][4], double N0, int *rank, int *tpmi) {
    double best = -INFINITY;
    *rank = 1; *tpmi = 0;
    for (int nl = 1; nl <= 4; nl++)
        for (int p = 0; p < ul_cb4_tpmi_count(nl); p++) {
            cx_t W[4][4], he[4][4] = {{0}};
            ul_cb4_precoder(nl, p, W);
            double beta = ul_cb4_unit_power_beta((const cx_t (*)[4])W, nl);
            for (int r = 0; r < 4; r++)
                for (int l = 0; l < nl; l++)
                    for (int t = 0; t < 4; t++) he[r][l] += H[r][t] * W[t][l] * beta;
            cx_t zero[4] = {0}, xhat[4];
            double nv[4];
            mimo_mmse_detect_4x4(he, zero, N0, xhat, nv);
            double cap = 0.0;
            for (int l = 0; l < nl; l++) {
                double var = nv[l] > 1e-12 ? nv[l] : 1e-12;
                cap += log2(1.0 + 1.0 / var);
            }
            if (cap > best) { best = cap; *rank = nl; *tpmi = p; }
        }
}
