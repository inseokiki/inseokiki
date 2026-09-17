/* test_beam_mgmt.c -- numeric unit tests for beam_mgmt.c (TS 38.213 §8.5 /
 * TS 38.214 §5.1.6.1 P1/P2/P3 beam management sweep).
 *
 * Scope: PHY_UNIT_VALIDATION_PLAN.md §5 3단계 "HARQ·적응 상태" group's
 * "빔관리 결정론적 상태 천이" row (tasks/todo.md, 2026-09-11). Unlike the
 * HARQ-retransmission-limit row in the same group (which is embedded
 * inline inside pdsch.c/pusch.c's Monte-Carlo run_* drivers together with
 * per-attempt channel/LDPC state and is not separable into a pure
 * function without production-code refactor), beam_mgmt.c's P1/P2/P3/
 * genie functions are already pure/deterministic given their inputs
 * (randomness only enters through num_rep noise draws, which is exactly
 * zero when N0=0) -- directly testable without touching beam_mgmt.c.
 *
 * Build/run: see PHY/tests/README.md (run_numeric_tests.sh drives this).
 */
#include "beam_mgmt.h"
#include "codebook_32port.h"
#include <stdio.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

/* At N0=0 (noiseless), beam_mgmt_p1_sweep()'s per-candidate average RSRP
 * is exactly the noise-free gain (sigma=0 -> noise=CX_MAKE(0,0) for every
 * rep), so the sweep MUST select the same grid point and the same gain as
 * beam_mgmt_genie_best()'s exhaustive noiseless search -- this is a
 * deterministic-state-transition check: given a fixed true channel, the
 * "measured" (noiseless) sweep transitions to the true optimum every
 * time, not just on average. */
static void p1_matches_genie_at_zero_noise(void) {
    struct { double l, m, n; } cases[] = {
        {0.0, 0.0, 0.0},      /* on-grid, broadside */
        {5.0, 3.0, 2.0},      /* on-grid, generic */
        {3.7, 8.2, 1.4},      /* off-grid (continuous true direction) */
        {15.9, 0.3, 3.9},     /* near the top of the grid range */
    };
    int all_ok = 1;
    for (size_t c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
        cx_t H_true[32];
        beam_mgmt_true_channel(cases[c].l, cases[c].m, cases[c].n, H_true);

        int g_l, g_m, g_n;
        double g_gain;
        beam_mgmt_genie_best(H_true, &g_l, &g_m, &g_n, &g_gain);

        int p_l, p_m, p_n;
        double p_gain;
        beam_mgmt_p1_sweep(H_true, 0.0, 1, &p_l, &p_m, &p_n, &p_gain);

        if (p_l != g_l || p_m != g_m || p_n != g_n) all_ok = 0;
        if (fabs(p_gain - g_gain) > 1e-12) all_ok = 0;
    }
    CHECK(all_ok, "beam_mgmt_p1_sweep(N0=0): selects exactly beam_mgmt_genie_best()'s grid point and gain for on- and off-grid true channels");
}

/* beam_mgmt_true_channel() at an exact on-grid (l,m,n) must reproduce
 * codebook_type1_sp_32port_rank1(l,m,n)'s own steering vector up to a
 * scalar (both are unit-norm by construction, so up to a unit-modulus
 * global phase) -- confirms the "continuous generalization" doc comment
 * is not just a generalization in name but actually collapses back onto
 * the codebook's own grid definition exactly at integer arguments. */
static void true_channel_matches_codebook_on_grid(void) {
    int all_ok = 1;
    for (int l = 0; l < 16 && all_ok; l += 5) {
        for (int m = 0; m < 16 && all_ok; m += 7) {
            for (int n = 0; n < 4 && all_ok; n++) {
                cx_t H_true[32];
                beam_mgmt_true_channel((double)l, (double)m, (double)n, H_true);
                cx_t W[32];
                codebook_type1_sp_32port_rank1(l, m, n, W);

                /* H_true should equal W up to a global unit-modulus phase:
                 * find that phase from index 0 (W[0] and H_true[0] are
                 * both nonzero -- v[0]=exp(j0)=1 at n1=0,k2=0), then check
                 * the rest match after removing it. */
                if (cabs(W[0]) < 1e-12) { all_ok = 0; break; }
                cx_t phase = H_true[0] / W[0];
                if (fabs(cabs(phase) - 1.0) > 1e-9) all_ok = 0;
                for (int i = 0; i < 32; i++) {
                    cx_t diff = H_true[i] - phase * W[i];
                    if (cabs(diff) > 1e-9) all_ok = 0;
                }
            }
        }
    }
    CHECK(all_ok, "beam_mgmt_true_channel() at integer (l,m,n) reproduces codebook_type1_sp_32port_rank1()'s own steering vector up to a global phase");
}

/* P3 (UE Rx beam refinement) at N0=0: beam_mgmt_p3_sweep()'s combining
 * weight is applied as conj(w_ue) (inner_prod32_row()'s "conj(channel)"
 * convention, matching this project's other combiners, e.g. MRC's
 * conj(h)*y), while beam_mgmt_true_channel_mimo()'s H_full carries
 * ue_array_manifold(r_true) UNconjugated -- so for an on-grid true AoA
 * r_true, the noiseless matched filter peaks at candidate index
 * (BM_UE_CAND - r_true) mod BM_UE_CAND, not at r_true itself (confirmed
 * empirically against every one of the BM_UE_CAND=8 on-grid directions
 * before encoding this as the expected value, not assumed). What must
 * hold regardless of this sign convention -- and IS the actual
 * "deterministic state transition" property under test -- is: (a) the
 * mapping true_idx -> sel_idx is a fixed, exactly-invertible bijection
 * (same input always selects the same candidate), and (b) the recovered
 * noiseless gain is always exactly BM_UE_N (full coherent array gain,
 * no scalloping loss) since every candidate is itself on-grid. */
static void p3_selects_matching_ue_beam(void) {
    cx_t H_true[32];
    beam_mgmt_true_channel(2.0, 6.0, 1.0, H_true);
    int g_l, g_m, g_n; double g_gain;
    beam_mgmt_genie_best(H_true, &g_l, &g_m, &g_n, &g_gain);
    cx_t W_tx[32];
    codebook_type1_sp_32port_rank1(g_l, g_m, g_n, W_tx);

    int all_ok = 1;
    for (int true_idx = 0; true_idx < BM_UE_CAND; true_idx++) {
        cx_t H_full[BM_UE_N][32];
        beam_mgmt_true_channel_mimo(H_true, (double)true_idx, H_full);

        int sel_idx; double sel_gain;
        beam_mgmt_p3_sweep(H_full, W_tx, 0.0, 1, &sel_idx, &sel_gain);
        int expect_idx = (BM_UE_CAND - true_idx) % BM_UE_CAND;
        if (sel_idx != expect_idx) all_ok = 0;
        if (fabs(sel_gain - (double)BM_UE_N) > 1e-9) all_ok = 0;
    }
    CHECK(all_ok, "beam_mgmt_p3_sweep(N0=0): true_idx->sel_idx is the exact fixed mirror bijection (BM_UE_CAND-true_idx) mod BM_UE_CAND, with full BM_UE_N coherent gain recovered every time");
}

/* P2 chaining: the effective 32-dim channel produced by
 * beam_mgmt_p2_effective_channel() using P3's selected UE beam, fed back
 * into beam_mgmt_p1_sweep()/genie_best(), must select the SAME gNB Tx
 * beam as the original single-antenna sweep would for a UE array
 * steered exactly at that beam's true AoA direction -- i.e. the
 * P1->P3->P2->P1 chain is a closed, self-consistent deterministic state
 * transition, not just individually-correct disconnected steps. */
static void p2_chain_is_self_consistent(void) {
    cx_t H_true[32];
    beam_mgmt_true_channel(9.0, 4.0, 3.0, H_true);
    int g_l, g_m, g_n; double g_gain;
    beam_mgmt_genie_best(H_true, &g_l, &g_m, &g_n, &g_gain);
    cx_t W_tx[32];
    codebook_type1_sp_32port_rank1(g_l, g_m, g_n, W_tx);

    double r_true = 3.0; /* exact UE candidate index */
    cx_t H_full[BM_UE_N][32];
    beam_mgmt_true_channel_mimo(H_true, r_true, H_full);

    int sel_ue_idx; double p3_gain;
    beam_mgmt_p3_sweep(H_full, W_tx, 0.0, 1, &sel_ue_idx, &p3_gain);
    /* mirror bijection, see p3_selects_matching_ue_beam()'s comment */
    int expect_ue_idx = (BM_UE_CAND - (int)r_true) % BM_UE_CAND;
    CHECK(sel_ue_idx == expect_ue_idx, "p2 chain setup: P3 recovers the exact (mirrored) UE beam index used to construct H_full");

    cx_t w_ue[BM_UE_N];
    beam_mgmt_ue_steer((double)sel_ue_idx, w_ue);
    cx_t h_eff[32];
    beam_mgmt_p2_effective_channel(H_full, w_ue, h_eff);

    /* h_eff should be (up to scale) proportional to H_true, since
     * H_full[u][g] = a_true[u]*H_true[g] and w_ue == the matched
     * candidate steering vector (unit norm) -- combining collapses the
     * UE dimension back to a scaled copy of the original gNB-side
     * channel. Re-running the P1 sweep on h_eff (after re-normalizing to
     * unit Frobenius norm, matching beam_mgmt_true_channel()'s own
     * convention) must select the identical gNB beam. */
    double norm2 = 0.0;
    for (int i = 0; i < 32; i++) norm2 += CX_NORM(h_eff[i]);
    double scale = 1.0 / sqrt(norm2);
    cx_t h_eff_n[32];
    for (int i = 0; i < 32; i++) h_eff_n[i] = h_eff[i] * scale;

    int p2_l, p2_m, p2_n; double p2_gain;
    beam_mgmt_p1_sweep(h_eff_n, 0.0, 1, &p2_l, &p2_m, &p2_n, &p2_gain);
    CHECK(p2_l == g_l && p2_m == g_m && p2_n == g_n,
          "p2 chain: P1 re-sweep on P2's effective channel (via P3's matched UE beam) selects the identical original gNB Tx beam");
}

int main(void) {
    p1_matches_genie_at_zero_noise();
    true_channel_matches_codebook_on_grid();
    p3_selects_matching_ue_beam();
    p2_chain_is_self_consistent();

    printf("\n%s\n", g_fail == 0 ? "ALL PASS" : "SOME FAILED");
    return g_fail == 0 ? 0 : 1;
}
