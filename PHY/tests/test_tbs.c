/* test_tbs.c -- numeric unit tests for tbs.c (TS 38.214 §5.1.3.2
 * Transport Block Size determination, Steps 2-4).
 *
 * Scope: tasks/todo.md's "TBS를 TS 38.214 §5.1.3.2 표준 절차로 교체"
 * item. The three hand-derived cases below are computed directly from
 * the confirmed formula (2026-09-14 spec-lookup pass, see tbs.h) by
 * hand in this file's own comments -- not copied from any external
 * "known TBS table" example, to avoid the risk of misremembering one.
 *
 * Build/run: see PHY/tests/README.md (run_numeric_tests.sh drives this).
 */
#include "tbs.h"
#include <stdio.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

/* The 93 Table 5.1.3.2-1 values, independently re-transcribed here (not
 * #include-shared with tbs.c's own static table) so a transcription
 * slip in one copy doesn't silently agree with the same slip in the
 * other. */
static const int TABLE[] = {
      24,  32,  40,  48,  56,  64,  72,  80,  88,  96, 104, 112, 120, 128, 136,
     144, 152, 160, 168, 176, 184, 192, 208, 224, 240, 256, 272, 288, 304, 320,
     336, 352, 368, 384, 408, 432, 456, 480, 504, 528, 552, 576, 608, 640, 672,
     704, 736, 768, 808, 848, 888, 928, 984,1032,1064,1128,1160,1192,1224,1256,
    1288,1320,1352,1416,1480,1544,1608,1672,1736,1800,1864,1928,2024,2088,2152,
    2216,2280,2408,2472,2536,2600,2664,2728,2792,2856,2976,3104,3240,3368,3496,
    3624,3752,3824
};
#define TABLE_N ((int)(sizeof(TABLE)/sizeof(TABLE[0])))

static int is_table_value(int v) {
    for (int i = 0; i < TABLE_N; i++) if (TABLE[i] == v) return 1;
    return 0;
}

/* Step-3 (small N_info) hand-derived example:
 *   n_re_qm=1000, R=0.5 -> N_info = 500.0
 *   n = max(3, floor(log2(500))-6) = max(3, 8-6) = max(3,2) = 3, 2^3=8
 *   N_info' = max(24, 8*floor(500/8)) = max(24, 8*62) = 496
 *   smallest table entry >= 496: table has ...,480,504,528,... -> 504
 * Expected TBS = 504. */
static void step3_hand_derived_example(void) {
    int tbs = nr_determine_tbs(1000, 0.5);
    CHECK(tbs == 504, "Step 3: n_re_qm=1000,R=0.5 (N_info=500) -> TBS=504 (hand-derived: N_info'=496, smallest table entry >=496)");
}

/* Step-4, code_rate>0.25, N_info'>8424 (multi-codeblock) hand-derived
 * example:
 *   n_re_qm=20000, R=0.9 -> N_info = 18000.0
 *   n_info-24 = 17976
 *   n = floor(log2(17976))-5: 2^14=16384<=17976<32768=2^15 -> floor=14, n=9, 2^9=512
 *   raw = 17976/512 = 35.109375 -> round = 35 -> N_info' = 35*512 = 17920
 *   max(3840,17920)=17920; R=0.9>0.25 and 17920>8424 ->
 *   C = ceil((17920+24)/8424) = ceil(17944/8424) = ceil(2.130) = 3
 *   TBS = 8*3*ceil(17944/24) - 24 = 24*748 - 24 = 17952-24 = 17928
 * Expected TBS = 17928. */
static void step4_high_rate_multiblock_example(void) {
    int tbs = nr_determine_tbs(20000, 0.9);
    CHECK(tbs == 17928, "Step 4 (R>0.25, N_info'>8424): n_re_qm=20000,R=0.9 -> TBS=17928 (hand-derived: N_info'=17920, C=3)");
    CHECK((tbs + 24) % (8 * 3) == 0, "same case: (TBS+24) is exactly divisible by 8*C=24, as the final ceiling formula guarantees");
}

/* Step-4, code_rate<=0.25 (low-rate branch, C from the 3816 divisor)
 * hand-derived example:
 *   n_re_qm=100000, R=0.2 -> N_info = 20000.0
 *   n_info-24 = 19976
 *   n = floor(log2(19976))-5: 2^14=16384<=19976<32768 -> floor=14, n=9, 2^9=512
 *   raw = 19976/512 = 39.015625 -> round = 39 -> N_info' = 39*512 = 19968
 *   max(3840,19968)=19968; R=0.2<=0.25 ->
 *   C = ceil((19968+24)/3816) = ceil(19992/3816) = ceil(5.239) = 6
 *   TBS = 8*6*ceil(19992/48) - 24 = 48*417 - 24 = 20016-24 = 19992
 * Expected TBS = 19992. */
static void step4_low_rate_example(void) {
    int tbs = nr_determine_tbs(100000, 0.2);
    CHECK(tbs == 19992, "Step 4 (R<=0.25): n_re_qm=100000,R=0.2 -> TBS=19992 (hand-derived: N_info'=19968, C=6)");
    CHECK((tbs + 24) % (8 * 6) == 0, "same case: (TBS+24) is exactly divisible by 8*C=48");
}

/* Every Step-3 (N_info<=3824) output must be a genuine Table 5.1.3.2-1
 * entry -- swept over a wide range of (n_re_qm, code_rate) so this
 * isn't just the one hand-derived point. */
static void step3_always_returns_table_value(void) {
    int all_ok = 1, count = 0;
    for (int n_re = 10; n_re <= 20000; n_re += 37) {
        for (double r = 0.05; r <= 1.0; r += 0.111) {
            double n_info = (double)n_re * r;
            if (n_info > 3824.0) continue;
            int tbs = nr_determine_tbs(n_re, r);
            if (!is_table_value(tbs)) all_ok = 0;
            count++;
        }
    }
    char m[128];
    snprintf(m, sizeof(m), "Step 3: every one of %d swept (n_re_qm,R) combinations with N_info<=3824 returns an exact Table 5.1.3.2-1 value", count);
    CHECK(all_ok && count > 100, m);
}

/* The minimum-24 guarantee: any tiny positive N_info still returns
 * exactly the table's own minimum entry, never something smaller (the
 * spec's own max(24,...) floor in Step 3). */
static void minimum_tbs_is_24(void) {
    CHECK(nr_determine_tbs(1, 0.01) == 24, "vanishingly small N_info (n_re_qm=1,R=0.01) floors to exactly TBS=24 (Table 5.1.3.2-1's own minimum)");
    CHECK(nr_determine_tbs(5, 0.5) == 24, "small N_info=2.5 also floors to exactly TBS=24");
}

/* Monotonicity: for a fixed code rate, TBS must be non-decreasing as
 * n_re_qm increases (more REs can never yield a smaller TBS) -- checked
 * across the Step3/Step4 boundary, not just within one branch. */
static void tbs_is_monotone_in_n_re_qm(void) {
    double r = 0.6;
    int prev = 0;
    int monotone = 1;
    for (int n_re = 1; n_re <= 50000; n_re += 13) {
        int tbs = nr_determine_tbs(n_re, r);
        if (tbs < prev) monotone = 0;
        prev = tbs;
    }
    CHECK(monotone, "nr_determine_tbs(): TBS is monotonically non-decreasing in n_re_qm at fixed R=0.6, swept across the Step3/Step4 boundary (N_info=3824)");
}

/* Exact boundary: N_info==3824.0 must go through Step 3 (since the
 * branch is "<=3824"), landing on the table's own max entry 3824
 * itself (N_info'=floor-quantized down from 3824 will still round back
 * up to the table's exact 3824 entry, since 3824 IS a table value).
 * N_info infinitesimally above 3824 must go through Step 4 and clear
 * the 3840 floor. */
static void step_boundary_at_3824(void) {
    /* n_re_qm=3824000, R=0.001 -> N_info=3824.0 exactly */
    int tbs_at = nr_determine_tbs(3824000, 0.001);
    CHECK(tbs_at == 3824, "N_info exactly 3824.0 uses Step 3 (boundary is closed, '<=') and resolves to the table's own 3824 entry");

    /* n_re_qm=3825000, R=0.001 -> N_info=3825.0, just over the boundary */
    int tbs_over = nr_determine_tbs(3825000, 0.001);
    CHECK(tbs_over >= 3840, "N_info just over 3824 uses Step 4 and clears the N_info'>=3840 floor");
}

int main(void) {
    step3_hand_derived_example();
    step4_high_rate_multiblock_example();
    step4_low_rate_example();
    step3_always_returns_table_value();
    minimum_tbs_is_24();
    tbs_is_monotone_in_n_re_qm();
    step_boundary_at_3824();

    printf("\n%s\n", g_fail == 0 ? "ALL PASS" : "SOME FAILED");
    return g_fail == 0 ? 0 : 1;
}
