/* ================================================================
 *  tbs.c
 *  TS 38.214 §5.1.3.2 Transport Block Size determination (Steps 2-4)
 *  -- see tbs.h for full scope notes and spec-confirmation caveats.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "tbs.h"
#include <math.h>

/* TS 38.214 Table 5.1.3.2-1 -- all 93 defined TBS values, confirmed
 * 2026-09-14 against two independent sources (local TS 38.214
 * V17.14.0 primary-source text + itecspec.com Rel-19 rendering, which
 * agreed exactly) -- this table is unchanged across 3GPP releases. */
static const int TBS_TABLE[] = {
      24,  32,  40,  48,  56,  64,  72,  80,  88,  96, 104, 112, 120, 128, 136,
     144, 152, 160, 168, 176, 184, 192, 208, 224, 240, 256, 272, 288, 304, 320,
     336, 352, 368, 384, 408, 432, 456, 480, 504, 528, 552, 576, 608, 640, 672,
     704, 736, 768, 808, 848, 888, 928, 984,1032,1064,1128,1160,1192,1224,1256,
    1288,1320,1352,1416,1480,1544,1608,1672,1736,1800,1864,1928,2024,2088,2152,
    2216,2280,2408,2472,2536,2600,2664,2728,2792,2856,2976,3104,3240,3368,3496,
    3624,3752,3824
};
#define TBS_TABLE_N ((int)(sizeof(TBS_TABLE) / sizeof(TBS_TABLE[0])))

/* floor(log2(x)) for x>0, via frexp() rather than log2() directly --
 * frexp's exact binary decomposition (x == m*2^e, 0.5<=m<1.0) avoids
 * log2()'s floating-point rounding landing on the wrong side of an
 * exact power-of-2 boundary, which this table-index-sensitive
 * quantization cannot tolerate (e.g. log2(4096.0) evaluating to
 * 11.999999999998 on some libm implementations would floor to 11
 * instead of the correct 12). x<=0 is not a valid input (§5.1.3.2 only
 * ever calls this on strictly-positive N_info/N_info-24) and is
 * guarded by the caller instead. */
static int ilog2_floor(double x) {
    int e;
    frexp(x, &e);
    return e - 1;
}

int nr_determine_tbs(int n_re_qm, double code_rate) {
    double n_info = (double)n_re_qm * code_rate;
    /* Defensive floor, not spec-mandated (see tbs.h): this project's
     * callers already guarantee n_re_qm>=1 and code_rate>0 in practice,
     * but a near-zero product would otherwise send a non-positive value
     * into ilog2_floor()'s frexp(), which is undefined for x<=0. */
    if (n_info < 1.0) n_info = 1.0;

    if (n_info <= 3824.0) {
        /* Step 3: n = max(3, floor(log2(N_info)) - 6),
         *         N_info' = max(24, 2^n * floor(N_info/2^n)),
         *         TBS = smallest Table 5.1.3.2-1 entry >= N_info'. */
        int n = ilog2_floor(n_info) - 6;
        if (n < 3) n = 3;
        long pow2n = 1L << n;
        long n_info_q = (long)(pow2n * floor(n_info / (double)pow2n));
        if (n_info_q < 24) n_info_q = 24;
        for (int i = 0; i < TBS_TABLE_N; i++)
            if (TBS_TABLE[i] >= n_info_q) return TBS_TABLE[i];
        return TBS_TABLE[TBS_TABLE_N - 1]; /* unreachable: n_info<=3824 implies n_info_q<=3824==table max */
    }

    /* Step 4: n = floor(log2(N_info-24)) - 5,
     *         N_info' = max(3840, 2^n * round((N_info-24)/2^n))
     *           (round()'s ties-away-from-zero matches the spec's
     *           documented "ties broken towards the next largest
     *           integer" for this always-positive argument),
     *         then a code-rate-dependent C(code-block-count) and a
     *         final 8*C-multiple ceiling -- both cross-verified against
     *         two independent secondary sources, see tbs.h. */
    int n = ilog2_floor(n_info - 24.0) - 5;
    long pow2n = 1L << n;
    double raw = (n_info - 24.0) / (double)pow2n;
    long n_info_q = (long)llround(raw) * pow2n;
    if (n_info_q < 3840) n_info_q = 3840;

    long tbs;
    if (code_rate <= 0.25) {
        long C = (n_info_q + 24 + 3816 - 1) / 3816;
        long denom = 8 * C;
        tbs = denom * ((n_info_q + 24 + denom - 1) / denom) - 24;
    } else if (n_info_q > 8424) {
        long C = (n_info_q + 24 + 8424 - 1) / 8424;
        long denom = 8 * C;
        tbs = denom * ((n_info_q + 24 + denom - 1) / denom) - 24;
    } else {
        tbs = 8 * ((n_info_q + 24 + 7) / 8) - 24;
    }
    return (int)tbs;
}
