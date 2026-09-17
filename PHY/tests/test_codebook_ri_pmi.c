/* test_codebook_ri_pmi.c -- numeric unit tests for codebook.c's Type I SP
 * 4-port RI/PMI joint selection (TS 38.214 §5.2.2.2.1 rank adaptation).
 *
 * Scope: PHY_UNIT_VALIDATION_PLAN.md §5 3단계 "HARQ·적응 상태" group's
 * "RI/PMI ... 결정론적 상태 천이" row (tasks/todo.md, 2026-09-11) --
 * codebook GEOMETRY (per-codeword unit norm/orthogonality) is a separate,
 * still-open todo item; this file covers only the deterministic
 * rank/PMI DECISION function, codebook_type1_sp_4port_ri_pmi_select().
 *
 * Build/run: see PHY/tests/README.md (run_numeric_tests.sh drives this).
 */
#include "codebook.h"
#include <stdio.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

/* codebook_type1_sp_4port_ri_pmi_select() is a pure function of (H, N0):
 * calling it twice on byte-identical inputs must return byte-identical
 * outputs (no hidden global state, no uninitialized-read nondeterminism)
 * -- the baseline "deterministic state transition" property every other
 * check here builds on. */
static void repeat_call_is_deterministic(void) {
    cx_t H[4][4] = {
        {CX_MAKE(1.0,0.2), CX_MAKE(0.3,-0.1), CX_MAKE(-0.2,0.4), CX_MAKE(0.1,0.1)},
        {CX_MAKE(0.5,-0.3), CX_MAKE(0.8,0.2), CX_MAKE(0.1,-0.2), CX_MAKE(-0.3,0.05)},
        {CX_MAKE(-0.1,0.4), CX_MAKE(0.2,0.3), CX_MAKE(0.6,-0.1), CX_MAKE(0.2,-0.2)},
        {CX_MAKE(0.3,0.1), CX_MAKE(-0.2,0.2), CX_MAKE(0.1,0.3), CX_MAKE(0.9,0.0)},
    };
    double N0 = 0.05;
    int r1,i1,i3,i2, ra1,ra2, rb1,rb3,rb2;
    int r1b,i1b,i3b,i2b, ra1b,ra2b, rb1b,rb3b,rb2b;
    codebook_type1_sp_4port_ri_pmi_select(H, N0, &r1,&i1,&i3,&i2, &ra1,&ra2, &rb1,&rb3,&rb2);
    codebook_type1_sp_4port_ri_pmi_select(H, N0, &r1b,&i1b,&i3b,&i2b, &ra1b,&ra2b, &rb1b,&rb3b,&rb2b);
    int same = (r1==r1b && i1==i1b && i3==i3b && i2==i2b &&
                ra1==ra1b && ra2==ra2b && rb1==rb1b && rb3==rb3b && rb2==rb2b);
    CHECK(same, "codebook_type1_sp_4port_ri_pmi_select(): identical (H,N0) input always produces identical output");
}

/* Exact rank-1 channel (all 4 Rx rows are scalar multiples of one row --
 * H has row-rank 1, so no second layer carries any independent signal
 * energy at all): rank-2's post-MMSE SINR for the second layer must be
 * driven to (numerically) zero capacity contribution, so rank-1's
 * Shannon capacity for the single best-matched beam must equal or
 * exceed any rank-2 capacity, and the selector must choose sel_rank=1.
 * This is the clean deterministic-threshold-crossing case the "RI ...
 * 상태 천이" row asks for: a true channel state that unambiguously
 * belongs on the rank-1 side of the decision boundary. */
static void rank1_channel_selects_rank1(void) {
    /* Row-rank-1 H: every Rx row is c_r times the same base direction
     * vector b (chosen with all 4 components nonzero and unequal so no
     * codebook beam trivially aligns perfectly with it by symmetry). */
    cx_t b[4]  = {CX_MAKE(1.0,0.0), CX_MAKE(0.4,0.3), CX_MAKE(-0.2,0.5), CX_MAKE(0.6,-0.1)};
    double c[4] = {1.0, 0.7, -0.5, 0.3};
    cx_t H[4][4];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++)
            H[r][t] = c[r] * b[t];

    double N0 = 0.01; /* high SNR: rank decision driven by channel rank, not noise floor */
    int sel_rank, sel_i1_1, sel_i1_3, sel_i2, r1_i1_1, r1_i2, r2_i1_1, r2_i1_3, r2_i2;
    codebook_type1_sp_4port_ri_pmi_select(H, N0, &sel_rank, &sel_i1_1, &sel_i1_3, &sel_i2,
                                           &r1_i1_1, &r1_i2, &r2_i1_1, &r2_i1_3, &r2_i2);
    CHECK(sel_rank == 1, "row-rank-1 H at high SNR: RI/PMI selector picks sel_rank=1 (no independent second-layer energy to justify rank-2)");
    CHECK(sel_i1_1 == r1_i1_1 && sel_i1_3 == 0 && sel_i2 == r1_i2,
          "row-rank-1 H: selected PMI exactly matches the rank-1-only search's own optimum (sel_i1_3 forced to 0 for rank-1)");
}

/* Well-conditioned full-rank H (identity-like, strong on every layer) at
 * high SNR: rank-2 should be preferred, since both layers carry
 * comparable independent energy and 2*log2(1+SNR) beats log2(1+SNR) for
 * any SNR level once the second layer isn't noise-starved. This is the
 * other side of the same decision boundary. */
static void well_conditioned_channel_selects_rank2(void) {
    cx_t H[4][4] = {
        {CX_MAKE(1.0,0.0), CX_MAKE(0.05,-0.02), CX_MAKE(0.03,0.01), CX_MAKE(-0.02,0.02)},
        {CX_MAKE(0.02,0.03), CX_MAKE(1.0,0.0), CX_MAKE(-0.04,0.01), CX_MAKE(0.03,-0.01)},
        {CX_MAKE(-0.03,0.01), CX_MAKE(0.02,-0.02), CX_MAKE(1.0,0.0), CX_MAKE(0.01,0.02)},
        {CX_MAKE(0.01,-0.02), CX_MAKE(-0.01,0.03), CX_MAKE(0.02,0.01), CX_MAKE(1.0,0.0)},
    };
    double N0 = 0.01;
    int sel_rank, sel_i1_1, sel_i1_3, sel_i2, r1_i1_1, r1_i2, r2_i1_1, r2_i1_3, r2_i2;
    codebook_type1_sp_4port_ri_pmi_select(H, N0, &sel_rank, &sel_i1_1, &sel_i1_3, &sel_i2,
                                           &r1_i1_1, &r1_i2, &r2_i1_1, &r2_i1_3, &r2_i2);
    CHECK(sel_rank == 2, "near-identity full-rank H at high SNR: RI/PMI selector picks sel_rank=2 (both layers carry comparable independent energy)");
    CHECK(sel_i1_1 == r2_i1_1 && sel_i1_3 == r2_i1_3 && sel_i2 == r2_i2,
          "near-identity H: selected PMI exactly matches the rank-2-only search's own optimum");
}

/* Monotone SNR sweep on a FIXED well-conditioned channel: as N0 shrinks
 * (SNR rises), the selected rank must be monotonically non-decreasing --
 * a state-transition invariant of any sane rank-adaptation rule (more
 * SNR headroom never makes a lower rank strictly better once the channel
 * itself is unchanged), checked directly against the real selector
 * rather than assumed. */
static void rank_selection_is_monotone_in_snr(void) {
    cx_t H[4][4] = {
        {CX_MAKE(1.0,0.0), CX_MAKE(0.1,-0.05), CX_MAKE(0.05,0.02), CX_MAKE(-0.03,0.03)},
        {CX_MAKE(0.04,0.06), CX_MAKE(0.9,0.0), CX_MAKE(-0.08,0.02), CX_MAKE(0.05,-0.02)},
        {CX_MAKE(-0.05,0.02), CX_MAKE(0.03,-0.04), CX_MAKE(0.8,0.0), CX_MAKE(0.02,0.04)},
        {CX_MAKE(0.02,-0.04), CX_MAKE(-0.02,0.06), CX_MAKE(0.04,0.02), CX_MAKE(0.85,0.0)},
    };
    /* N0 descending (SNR ascending) */
    double N0_list[] = {10.0, 3.0, 1.0, 0.3, 0.1, 0.03, 0.01, 0.003, 0.001};
    int prev_rank = 0;
    int monotone = 1;
    for (size_t i = 0; i < sizeof(N0_list)/sizeof(N0_list[0]); i++) {
        int sel_rank, sel_i1_1, sel_i1_3, sel_i2, r1_i1_1, r1_i2, r2_i1_1, r2_i1_3, r2_i2;
        codebook_type1_sp_4port_ri_pmi_select(H, N0_list[i], &sel_rank, &sel_i1_1, &sel_i1_3, &sel_i2,
                                               &r1_i1_1, &r1_i2, &r2_i1_1, &r2_i1_3, &r2_i2);
        if (sel_rank < prev_rank) monotone = 0;
        prev_rank = sel_rank;
    }
    CHECK(monotone, "fixed well-conditioned H, N0 descending (SNR ascending): selected rank is monotonically non-decreasing");
    CHECK(prev_rank == 2, "same sweep: reaches sel_rank=2 by the highest-SNR (N0=0.001) end");
}

int main(void) {
    repeat_call_is_deterministic();
    rank1_channel_selects_rank1();
    well_conditioned_channel_selects_rank2();
    rank_selection_is_monotone_in_snr();

    printf("\n%s\n", g_fail == 0 ? "ALL PASS" : "SOME FAILED");
    return g_fail == 0 ? 0 : 1;
}
