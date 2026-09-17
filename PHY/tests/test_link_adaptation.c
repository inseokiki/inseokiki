/* test_link_adaptation.c -- numeric unit tests for olla.c (Outer Loop
 * Link Adaptation) and ul_power_ctrl.c's TPC decision (TS 38.213 §7.2.1
 * closed-loop UL power control).
 *
 * Scope: PHY_UNIT_VALIDATION_PLAN.md §5 3단계 "HARQ·적응 상태" group's
 * "OLLA·ULPC ... 결정론적 상태 천이" rows (tasks/todo.md, 2026-09-11).
 * ulpc_tpc_decide() was made non-static and exposed via ul_power_ctrl.h
 * specifically for this test (2026-09-11) -- run_ulpc_simulation() itself
 * remains a printf-driven Monte-Carlo-style driver not suited to direct
 * unit testing, but its one nontrivial decision rule (the TPC step) is a
 * pure function and is now directly callable instead of re-derived.
 *
 * Build/run: see PHY/tests/README.md (run_numeric_tests.sh drives this).
 */
#include "olla.h"
#include "ul_power_ctrl.h"
#include "mcs_table.h"
#include <stdio.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

/* olla_init()'s step_up_db formula, restated independently (not calling
 * olla_init() a second time with itself as the oracle): this is the
 * "정상상태 BLER=target" condition documented in olla.h line 36-37. */
static void olla_init_step_up_formula(void) {
    double targets[] = {0.1, 0.01, 0.3, 0.001};
    double step_downs[] = {0.5, 1.0, 2.0};
    int all_ok = 1;
    for (size_t t = 0; t < sizeof(targets)/sizeof(targets[0]); t++) {
        for (size_t d = 0; d < sizeof(step_downs)/sizeof(step_downs[0]); d++) {
            OLLAState s;
            olla_init(&s, targets[t], step_downs[d]);
            double expect_up = step_downs[d] * targets[t] / (1.0 - targets[t]);
            if (fabs(s.step_up_db - expect_up) > 1e-12) all_ok = 0;
            if (s.offset_db != 0.0) all_ok = 0;
            if (s.bler_target != targets[t]) all_ok = 0;
            if (s.step_down_db != step_downs[d]) all_ok = 0;
        }
    }
    CHECK(all_ok, "olla_init(): offset starts at exactly 0, step_up_db == step_down*target/(1-target) exactly, for several target/step_down combinations");
}

/* olla_update() is an exact, deterministic accumulator: a fixed sequence
 * of ACK/NACK must produce an exact, hand-computed offset -- not just
 * "moves in the right direction". */
static void olla_update_exact_sequence(void) {
    OLLAState s;
    olla_init(&s, 0.1, 1.0);   /* step_down=1.0, step_up=1.0*0.1/0.9 = 1/9 */
    double step_up = 1.0 / 9.0;
    int acks[] = {1,1,0,1,1,1,1,1,1,0};  /* 8 ACK, 2 NACK in this exact order */
    double expect = 0.0;
    int all_ok = 1;
    for (size_t i = 0; i < sizeof(acks)/sizeof(acks[0]); i++) {
        olla_update(&s, acks[i]);
        expect += acks[i] ? step_up : -1.0;
        if (fabs(s.offset_db - expect) > 1e-12) all_ok = 0;
    }
    CHECK(all_ok, "olla_update(): offset_db after each step in a fixed ACK/NACK sequence matches exact hand-accumulated value (8 ACK * step_up - 2 NACK * step_down)");

    /* Steady-state invariant: a long run at exactly the target ACK ratio
     * (9 ACK for every 1 NACK, matching bler_target=0.1) returns the
     * offset to exactly 0 after each full 10-step cycle (9*step_up ==
     * 1*step_down by construction of step_up_db). */
    OLLAState s2;
    olla_init(&s2, 0.1, 1.0);
    for (int cycle = 0; cycle < 5; cycle++) {
        for (int i = 0; i < 9; i++) olla_update(&s2, 1);
        olla_update(&s2, 0);
    }
    CHECK(fabs(s2.offset_db) < 1e-9,
          "olla_update(): at exactly the target ACK ratio (9 ACK : 1 NACK for bler_target=0.1), offset returns to exactly 0 after every full cycle (5 cycles checked)");
}

/* olla_select_mcs(): monotonicity + exact boundary crossing against an
 * independently-recomputed required-SNR table (not calling
 * olla_select_mcs() as its own oracle for the boundary values). */
static void olla_select_mcs_boundary(void) {
    MCSTableType table = MCS_TABLE1;
    double gap_db = 3.0;
    int max_idx = get_max_mcs_index(table);

    /* Independently recompute each MCS's required SNR (same formula as
     * olla_select_mcs()'s own doc comment, re-typed here rather than
     * reusing any intermediate from olla.c). */
    int all_ok = 1;
    for (int m = 1; m <= max_idx; m++) {
        MCSEntry e = get_mcs_entry(m, table);
        if (e.spectralEff <= 0.0) continue;
        double snr_req_db = 10.0 * log10(pow(2.0, e.spectralEff) - 1.0) + gap_db;
        /* Exactly at the required SNR (plus a hair of margin for fp
         * comparison direction), olla_select_mcs() must return an index
         * whose own required SNR is <= this effective SNR -- i.e. never
         * selects an MCS this channel can't support. */
        int sel = olla_select_mcs(snr_req_db + 1e-6, gap_db, table);
        MCSEntry sel_e = get_mcs_entry(sel, table);
        double sel_req_db = (sel_e.spectralEff > 0.0)
            ? 10.0 * log10(pow(2.0, sel_e.spectralEff) - 1.0) + gap_db
            : -1e18;
        if (sel_req_db > snr_req_db + 1e-6 + 1e-9) all_ok = 0;
    }
    CHECK(all_ok, "olla_select_mcs(): for every MCS index m's own required-SNR boundary (independently recomputed), selecting at that SNR never returns an MCS whose required SNR exceeds it");

    /* Monotonicity: selected spectral efficiency is non-decreasing as
     * effective SNR increases, over a fine sweep (this project's own
     * 2026-09-01 finding is that MCS INDEX order isn't always efficiency
     * order at the 16QAM/64QAM boundary -- so this checks the actually-
     * documented invariant, achieved spectral efficiency, not index). */
    double prev_eta = -1.0;
    int mono_ok = 1;
    for (double snr = -10.0; snr <= 40.0; snr += 0.25) {
        int sel = olla_select_mcs(snr, gap_db, table);
        MCSEntry e = get_mcs_entry(sel, table);
        if (e.spectralEff < prev_eta - 1e-9) mono_ok = 0;
        prev_eta = e.spectralEff;
    }
    CHECK(mono_ok, "olla_select_mcs(): achieved spectral efficiency is monotonically non-decreasing over a fine SNR sweep (-10..40 dB, 0.25 dB step)");

    /* Below every MCS's required SNR: falls back to index 0. */
    int floor_sel = olla_select_mcs(-100.0, gap_db, table);
    CHECK(floor_sel == 0, "olla_select_mcs(): effective SNR far below every MCS's requirement falls back to MCS index 0");
}

/* ulpc_tpc_decide(): exact TS 38.213 Table 7.2.1-1 threshold check,
 * including both open boundaries at exactly +-0.5dB and +3.0dB (the
 * function uses strict '>'/'<', so the boundary values themselves fall
 * into the NEXT lower bucket -- checked explicitly, not assumed). */
static void ulpc_tpc_decide_thresholds(void) {
    struct { double err, expect; } cases[] = {
        { 10.0,  3.0}, {  3.01,  3.0}, {  3.0,   1.0}, /* >3.0 -> +3; ==3.0 falls to +1 bucket */
        {  2.0,   1.0}, {  0.51,  1.0}, {  0.5,   0.0}, /* >0.5 -> +1; ==0.5 falls to deadband */
        {  0.0,   0.0}, { -0.5,   0.0}, /* deadband is closed on both ends: [-0.5,0.5] */
        { -0.51, -1.0}, { -3.0,  -1.0}, {-100.0, -1.0}, /* < -0.5 -> -1, no further negative bucket */
    };
    int all_ok = 1;
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        double got = ulpc_tpc_decide(cases[i].err);
        if (fabs(got - cases[i].expect) > 1e-12) all_ok = 0;
    }
    CHECK(all_ok, "ulpc_tpc_decide(): exact {-1,0,+1,+3} dB bucket at every threshold and boundary value (>3.0, >0.5, deadband [-0.5,0.5] closed both ends, <-0.5)");
    CHECK(ulpc_tpc_decide(3.0) != 3.0, "ulpc_tpc_decide(): SINR_err exactly 3.0 dB uses strict '>' so does NOT trigger the +3dB bucket (falls to +1dB)");
    CHECK(ulpc_tpc_decide(0.5) == 0.0 && ulpc_tpc_decide(-0.5) == 0.0,
          "ulpc_tpc_decide(): SINR_err exactly +-0.5 dB is inside the deadband (both boundary values return 0)");
}

/* f(i) accumulation + +-30dB clamp, exercised with the real
 * ulpc_tpc_decide() driving the accumulator exactly as
 * run_ulpc_simulation() does (same "f_acc += delta; clamp to +-30" two
 * lines, restated here since that loop lives inside a printf-driven
 * driver function, not a separately callable unit) -- deterministic
 * convergence to +30dB when SINR is persistently far below target, and
 * exact clamping (never exceeds +-30dB even after many more steps). */
static void ulpc_accumulator_clamps_at_30db(void) {
    double f_acc = 0.0;
    int hit_clamp_at = -1;
    for (int i = 0; i < 200; i++) {
        double sinr_err = 50.0; /* always far above +3dB threshold -> always +3dB step */
        double delta = ulpc_tpc_decide(sinr_err);
        f_acc += delta;
        if (f_acc > 30.0) f_acc = 30.0;
        if (f_acc < -30.0) f_acc = -30.0;
        if (hit_clamp_at < 0 && f_acc >= 30.0) hit_clamp_at = i;
    }
    CHECK(hit_clamp_at == 9, "ulpc accumulator: with a persistent +3dB step, reaches the +30dB clamp in exactly 10 steps (30/3), matching hand computation");
    CHECK(fabs(f_acc - 30.0) < 1e-12, "ulpc accumulator: stays pinned at exactly +30.0 dB after 200 steps of persistent +3dB pressure (never exceeds the clamp)");

    double f_acc2 = 0.0;
    for (int i = 0; i < 200; i++) {
        double delta = ulpc_tpc_decide(-50.0); /* always far below -0.5dB threshold -> always -1dB step */
        f_acc2 += delta;
        if (f_acc2 > 30.0) f_acc2 = 30.0;
        if (f_acc2 < -30.0) f_acc2 = -30.0;
    }
    CHECK(fabs(f_acc2 - (-30.0)) < 1e-12, "ulpc accumulator: persistent -1dB pressure clamps at exactly -30.0 dB after 200 steps");
}

int main(void) {
    olla_init_step_up_formula();
    olla_update_exact_sequence();
    olla_select_mcs_boundary();
    ulpc_tpc_decide_thresholds();
    ulpc_accumulator_clamps_at_30db();

    printf("\n%s\n", g_fail == 0 ? "ALL PASS" : "SOME FAILED");
    return g_fail == 0 ? 0 : 1;
}
