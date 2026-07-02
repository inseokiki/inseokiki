#include "tbs.h"
#include <math.h>

/* TS 38.214 Table 5.1.3.2-1 — 93 TBS values (N_info ≤ 3824 path). */
static const int tbs_table[93] = {
     24,  32,  40,  48,  56,  64,  72,  80,  88,  96,
    104, 112, 120, 128, 136, 144, 152, 160, 168, 176,
    184, 192, 208, 224, 240, 256, 272, 288, 304, 320,
    336, 352, 368, 384, 408, 432, 456, 480, 504, 528,
    552, 576, 608, 640, 672, 704, 736, 768, 808, 848,
    888, 928, 984,1032,1064,1128,1160,1192,1224,1256,
   1288,1320,1352,1416,1480,1544,1608,1672,1736,1800,
   1864,1928,2024,2088,2152,2216,2280,2408,2472,2536,
   2600,2664,2728,2792,2856,2976,3104,3240,3368,3496,
   3624,3752,3824
};
#define TBS_TABLE_LEN 93

/* Return smallest table entry >= target, or last entry if all smaller. */
static int tbs_table_lookup(int target) {
    for (int i = 0; i < TBS_TABLE_LEN; i++)
        if (tbs_table[i] >= target) return tbs_table[i];
    return tbs_table[TBS_TABLE_LEN - 1];
}

/* TS 38.214 Section 5.1.3.2, Steps 3-4. */
static int quantize_ninfo(double N_info, double code_rate) {
    if (N_info <= 3824.0) {
        /* Small TBS path: quantize and look up table */
        int n = (int)floor(log2(N_info)) - 6;
        if (n < 3) n = 3;
        double pow2n  = (double)(1 << n);            /* 2^n */
        double N_info_prime = pow2n * round(N_info / pow2n);
        if (N_info_prime < 24.0) N_info_prime = 24.0;
        return tbs_table_lookup((int)N_info_prime);
    } else {
        /* Large TBS path: formula-based */
        int n = (int)floor(log2(N_info - 24.0)) - 5;
        if (n < 0) n = 0;
        double pow2n  = (double)(1 << n);
        double N_info_prime = pow2n * round((N_info - 24.0) / pow2n);
        if (N_info_prime < 3840.0) N_info_prime = 3840.0;

        int tbs;
        if (code_rate <= 0.25) {
            /* C = ceil((N_info'+24) / 3840) code blocks, each TBS/C ~ 40 bits*/
            tbs = 8 * (int)ceil((N_info_prime + 24.0) / 8.0) - 24;
        } else if (N_info_prime > 8424.0) {
            /* CB segmentation: 2 code blocks */
            tbs = 8 * (int)ceil((N_info_prime + 24.0) / 16.0) * 2 - 24;
        } else {
            tbs = 8 * (int)ceil((N_info_prime + 24.0) / 8.0) - 24;
        }
        return tbs;
    }
}

/* -----------------------------------------------------------------------
 * calc_tbs — full TS 38.214 Section 5.1.3.2 procedure
 * ----------------------------------------------------------------------- */
int calc_tbs(int n_prb, int n_symb, int n_dmrs_prb, int n_oh_prb,
             double code_rate, int Qm, int v)
{
    /* Step 1: REs per PRB, capped at 156 */
    int N_re_prb = 12 * n_symb - n_dmrs_prb - n_oh_prb;
    if (N_re_prb > 156) N_re_prb = 156;
    if (N_re_prb < 0)   N_re_prb = 0;

    int N_re = N_re_prb * n_prb;

    /* Step 2: intermediate N_info */
    double N_info = (double)N_re * code_rate * Qm * v;

    /* Step 3-4: quantize */
    return quantize_ninfo(N_info, code_rate);
}

/* -----------------------------------------------------------------------
 * calc_tbs_from_nre — shortcut when data RE count is already known
 * ----------------------------------------------------------------------- */
int calc_tbs_from_nre(int n_data_re, double code_rate, int Qm, int v)
{
    double N_info = (double)n_data_re * code_rate * Qm * v;
    return quantize_ninfo(N_info, code_rate);
}
