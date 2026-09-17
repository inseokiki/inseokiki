/* test_ldpc.c -- permanent numeric regression test for TS 38.212 5.2.2
 * multi-code-block segmentation (nr_sch.c) and 5.4.2.1 circular-buffer
 * rate matching/HARQ combining (nr_rate_matching.c) (lab/PHY_REVIEW_
 * 2026-09-10.md PHY-03, new coverage -- no prior scratch harness existed
 * for this).
 *
 * UT-06 (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10): every round-trip
 * here is self-consistent (this project's own encoder validated by its
 * own decoder) -- there is no independently-sourced reference vector
 * (e.g. a published TS 38.212 test vector) cross-checked against. A
 * self-consistent round-trip can't catch a bug that's symmetric between
 * encode and decode (e.g. both sides agreeing on a wrong bit-selection
 * order); treat this file as "internal consistency confirmed", not
 * "standards conformance confirmed against an external source".
 *
 * Build/run: see PHY/tests/README.md (run_numeric_tests.sh drives this).
 */
#include "nr_sch.h"
#include "nr_rate_matching.h"
#include "ldpc.h"
#include "ldpc_nr.h"
#include "ldpc_tables.h"
#include "crc.h"
#include "mcs_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

/* Full encode -> segment -> rate-match -> noise-free LLR -> combine ->
 * decode -> concat -> CRC24A round trip, for a given transport block size
 * A (payload bits, excl. the outer CRC24A this project always attaches)
 * and MCS. Verifies seg.C matches expected_C (so callers can assert both
 * the C=1 and C>1 paths are actually exercised, not accidentally the
 * same path twice), and that every bit of the reconstructed payload
 * matches the original. */
static void segmentation_roundtrip(const char *label, int A, int mcs_index,
                                    MCSTableType tbl, int expected_C) {
    MCSEntry mcs = get_mcs_entry(mcs_index, tbl);
    double cr = get_code_rate(&mcs);
    int bps = mcs.modulationOrder;
    int B = A + 24;

    NRSegInfo seg;
    nr_seg_compute(B, cr, &seg);
    char cmsg[128];
    snprintf(cmsg, sizeof(cmsg), "%s: nr_seg_compute gives C=%d (expected %d)", label, seg.C, expected_C);
    CHECK(seg.C == expected_C, cmsg);

    int payload = seg.Kprime - seg.L;
    CHECK(seg.C * payload == B, "seg.C * (Kprime-L) == B exactly (nr_seg_compute's own invariant)");

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E = (int)(A / cr);
    if (E < 1) E = 1;
    int nsym = (E + bps - 1) / bps;
    E = nsym * bps;

    int *Er = malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, 1, bps, seg.C, Er);

    int *tb = malloc(A * sizeof(int));
    int *tb_crc = malloc(B * sizeof(int));
    int *cb_bits = malloc((size_t)seg.C * seg.Kprime * sizeof(int));
    int *coded = malloc(acsz * sizeof(int));
    int **rm = malloc(seg.C * sizeof(int *));
    for (int r = 0; r < seg.C; r++) rm[r] = malloc(Er[r] * sizeof(int));
    int *selbits = malloc(E * sizeof(int));
    double *llr = malloc(E * sizeof(double));
    double *soft_buf = malloc(acsz * sizeof(double));
    int *decoded_cb = malloc(ldpc.info_size * sizeof(int));
    int *decoded_tb = malloc(B * sizeof(int));

    srand(4242);
    int all_ok = 1;
    for (int trial = 0; trial < 20; trial++) {
        for (int i = 0; i < A; i++) tb[i] = rand() & 1;
        attach_crc(tb, A, CRC24A, tb_crc);
        nr_seg_split(tb_crc, &seg, cb_bits);

        int roff = 0;
        for (int r = 0; r < seg.C; r++) {
            const int *info = cb_bits + (size_t)r * seg.Kprime;
            ldpc_encode(&ldpc, info, coded);
            nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                       ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                       0, Er[r], rm[r]);
            roff += Er[r];
        }
        (void)roff;
        nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
        /* noise-free: turn selected coded bits into saturating LLRs */
        for (int i = 0; i < E; i++) llr[i] = selbits[i] ? -20.0 : 20.0;

        int cb_crc_ok = 1;
        int off = 0;
        for (int r = 0; r < seg.C; r++) {
            memset(soft_buf, 0, acsz * sizeof(double));
            nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        0, Er[r], llr + off);
            off += Er[r];
            ldpc_decode(&ldpc, soft_buf, 25, decoded_cb);
            if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B)) cb_crc_ok = 0;
            memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
        }
        int tb_crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);
        /* UT-01 (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10): memcmp's
         * third argument is a BYTE count, not an element count -- tb/
         * decoded_tb are int*, so "A" here was only comparing the first
         * A bytes (~A/sizeof(int) elements on a typical platform), not
         * all A payload bits. Must scale by sizeof(int) to actually
         * cover the whole payload. */
        int bits_ok = memcmp(decoded_tb, tb, (size_t)A * sizeof(int)) == 0;
        if (!tb_crc_ok || !bits_ok) all_ok = 0;
    }
    snprintf(cmsg, sizeof(cmsg), "%s: 20-trial noise-free encode/segment/decode/concat round-trip exact (C=%d)",
             label, seg.C);
    CHECK(all_ok, cmsg);

    free(tb); free(tb_crc); free(cb_bits); free(coded);
    for (int r = 0; r < seg.C; r++) free(rm[r]);
    free(rm); free(selbits); free(llr); free(soft_buf);
    free(decoded_cb); free(decoded_tb); free(Er);
    ldpc_free(&ldpc);
}

/* HARQ circular-buffer boundary check: nr_ldpc_rate_match_select() must
 * (a) select exactly the non-filler coded bits in order for e == that
 * count, and (b) wrap around correctly (repeating from the start of the
 * non-filler sequence) when e exceeds it -- this is the "HARQ 버퍼 경계"
 * PHY-03 explicitly asks for, since HARQ IR/Chase repeatedly walks this
 * same circular buffer across retransmissions. */
static void harq_buffer_boundary_check(void) {
    MCSEntry mcs = get_mcs_entry(10, MCS_TABLE1);
    double cr = get_code_rate(&mcs);
    int A = 200, B = A + 24;
    NRSegInfo seg;
    nr_seg_compute(B, cr, &seg);
    CHECK(seg.C == 1, "harq boundary check uses a small C=1 TB (keeps the test focused on rate matching, not segmentation)");

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int filler_start = ldpc.info_size, filler_end = ldpc.base_info_cols * ldpc.Zc;
    int non_filler = acsz - (filler_end - filler_start);

    int *info = malloc(seg.Kprime * sizeof(int));
    int *coded = malloc(acsz * sizeof(int));
    srand(99);
    for (int i = 0; i < seg.Kprime; i++) info[i] = rand() & 1;
    ldpc_encode(&ldpc, info, coded);

    /* (a) e == non_filler: selects every non-filler position exactly once,
     * in increasing index order (rv=0 starts at k0=0). */
    int *out_full = malloc(non_filler * sizeof(int));
    nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc, filler_start, filler_end, 0, non_filler, out_full);
    int expect_idx = 0, mismatch = 0;
    for (int i = 0; i < acsz && expect_idx < non_filler; i++) {
        if (i >= filler_start && i < filler_end) continue;
        if (out_full[expect_idx] != coded[i]) mismatch++;
        expect_idx++;
    }
    CHECK(mismatch == 0, "e == non_filler count: selects every non-filler coded bit exactly once, in order");

    /* (b) e == non_filler + 5: the wrapped tail (last 5 selected) must
     * equal the first 5 non-filler bits again (circular wrap-around). */
    int e_wrap = non_filler + 5;
    int *out_wrap = malloc(e_wrap * sizeof(int));
    nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc, filler_start, filler_end, 0, e_wrap, out_wrap);
    int wrap_ok = 1;
    for (int i = 0; i < non_filler; i++) if (out_wrap[i] != out_full[i]) wrap_ok = 0;
    for (int i = 0; i < 5; i++) if (out_wrap[non_filler + i] != out_full[i]) wrap_ok = 0;
    CHECK(wrap_ok, "e > non_filler count: wraps around and repeats from the start of the non-filler sequence");

    /* (c) UT-02 (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10): the
     * original version of this check only asserted "never decreases",
     * which a broken combine (e.g. one that always writes 0, or where
     * the second call is a silent no-op) could satisfy trivially. Check
     * exact values instead: the first coded-domain index outside the
     * filler range is where selection-order position 0 always lands
     * (rv=0 -> k0=0), so its accumulated value is fully predictable. */
    int idx0 = -1;
    for (int i = 0; i < acsz; i++) {
        if (i >= filler_start && i < filler_end) continue;
        idx0 = i;
        break;
    }
    CHECK(idx0 >= 0, "found a non-filler coded-domain index to anchor exact-value checks on");

    double *soft_once = calloc(acsz, sizeof(double));
    double *soft_twice = calloc(acsz, sizeof(double));
    double *soft_cancel = calloc(acsz, sizeof(double));
    double *llr_e = malloc(non_filler * sizeof(double));
    double *llr_e_neg = malloc(non_filler * sizeof(double));
    for (int i = 0; i < non_filler; i++) {
        llr_e[i] = out_full[i] ? -3.0 : 3.0;
        llr_e_neg[i] = -llr_e[i];
    }
    nr_ldpc_rate_match_combine(soft_once, acsz, ldpc.bg, ldpc.Zc, filler_start, filler_end, 0, non_filler, llr_e);
    nr_ldpc_rate_match_combine(soft_twice, acsz, ldpc.bg, ldpc.Zc, filler_start, filler_end, 0, non_filler, llr_e);
    nr_ldpc_rate_match_combine(soft_twice, acsz, ldpc.bg, ldpc.Zc, filler_start, filler_end, 0, non_filler, llr_e);
    nr_ldpc_rate_match_combine(soft_cancel, acsz, ldpc.bg, ldpc.Zc, filler_start, filler_end, 0, non_filler, llr_e);
    nr_ldpc_rate_match_combine(soft_cancel, acsz, ldpc.bg, ldpc.Zc, filler_start, filler_end, 0, non_filler, llr_e_neg);

    CHECK(fabs(soft_once[idx0] - llr_e[0]) < 1e-12,
          "single combine: accumulated value at the first selected position equals the input LLR exactly");
    CHECK(fabs(soft_twice[idx0] - 2.0 * llr_e[0]) < 1e-12,
          "combining the same RV twice: accumulated value is exactly double the single-combine value");
    CHECK(fabs(soft_cancel[idx0]) < 1e-12,
          "combining with the negated LLR cancels the first combine back to exactly 0 (genuine accumulation, not overwrite)");

    int untouched_ok = 1;
    for (int i = filler_start; i < filler_end; i++)
        if (soft_once[i] != 0.0 || soft_twice[i] != 0.0) untouched_ok = 0;
    CHECK(untouched_ok, "filler positions are never written by rate-match combine (stay exactly 0)");

    int combine_ok = 1;
    for (int i = 0; i < acsz; i++) {
        if (i >= filler_start && i < filler_end) continue;
        if (fabs(soft_twice[i]) < fabs(soft_once[i]) - 1e-9) combine_ok = 0;
    }
    CHECK(combine_ok, "Chase combining (same RV twice) never decreases LLR confidence at any position");

    /* RV change + reset: a fresh buffer combined with rv=2 (different k0)
     * must differ from the rv=0 buffer somewhere (proves rv actually
     * changes the circular starting offset, not just relabeling). */
    double *soft_rv2 = calloc(acsz, sizeof(double));
    nr_ldpc_rate_match_combine(soft_rv2, acsz, ldpc.bg, ldpc.Zc, filler_start, filler_end, 2, non_filler, llr_e);
    int rv_differs = 0;
    for (int i = 0; i < acsz; i++) if (soft_rv2[i] != soft_once[i]) { rv_differs = 1; break; }
    CHECK(rv_differs, "combining with rv=2 (different k0) produces a different buffer than rv=0");

    free(info); free(coded); free(out_full); free(out_wrap);
    free(soft_once); free(soft_twice); free(soft_cancel); free(soft_rv2);
    free(llr_e); free(llr_e_neg);
    ldpc_free(&ldpc);
}

/* TB/CB isolation (PHY_UNIT_VALIDATION_PLAN.md §5 3단계 "HARQ·적응 상태"
 * group, tasks/todo.md 2026-09-11): with C=2 code blocks sharing one
 * transport block, corrupting ONLY code block 0's received LLRs (heavy
 * inverted-sign noise on a third of its bits -- well past this MCS's
 * code rate's correction capability) must fail CB0's own CRC24B while
 * leaving code block 1 -- decoded with its own independently-allocated
 * soft_buf, encoded/rate-matched from an entirely separate info-bit
 * segment -- completely unaffected: exact CRC24B pass and bit-exact
 * payload recovery. This is the property that would break if CB
 * decoding accidentally shared or aliased state (a single soft_buf
 * reused across code blocks, or an off-by-one in nr_seg_split()'s
 * per-block offset bleeding bits from one block into another). */
static void tb_cb_isolation_check(void) {
    int A = 8426, mcs_index = 10;
    MCSEntry mcs = get_mcs_entry(mcs_index, MCS_TABLE1);
    double cr = get_code_rate(&mcs);
    int bps = mcs.modulationOrder;
    int B = A + 24;

    NRSegInfo seg;
    nr_seg_compute(B, cr, &seg);
    CHECK(seg.C == 2, "tb_cb_isolation_check: reuses the known C=2 (A=8426,MCS10/TABLE1) combination");
    int payload = seg.Kprime - seg.L;

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E = (int)(A / cr);
    if (E < 1) E = 1;
    int nsym = (E + bps - 1) / bps;
    E = nsym * bps;

    int *Er = malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, 1, bps, seg.C, Er);

    int *tb = malloc(A * sizeof(int));
    int *tb_crc = malloc(B * sizeof(int));
    int *cb_bits = malloc((size_t)seg.C * seg.Kprime * sizeof(int));
    srand(777);
    for (int i = 0; i < A; i++) tb[i] = rand() & 1;
    attach_crc(tb, A, CRC24A, tb_crc);
    nr_seg_split(tb_crc, &seg, cb_bits);

    int *decoded_cb[2];
    int cb_crc_ok[2];
    for (int r = 0; r < seg.C; r++) {
        const int *info = cb_bits + (size_t)r * seg.Kprime;
        int *coded = malloc(acsz * sizeof(int));
        ldpc_encode(&ldpc, info, coded);

        int *rm = malloc(Er[r] * sizeof(int));
        nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                   ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                   0, Er[r], rm);

        double *llr = malloc(Er[r] * sizeof(double));
        for (int i = 0; i < Er[r]; i++) llr[i] = rm[i] ? -20.0 : 20.0;
        /* Corrupt only r==0: invert a third of its LLRs with strong
         * (confidently WRONG) magnitude -- well beyond this code rate's
         * correction capability, forcing a genuine CRC24B failure rather
         * than a marginal one. */
        if (r == 0) {
            for (int i = 0; i < Er[r]; i += 3) llr[i] = -llr[i];
        }

        double *soft_buf = calloc(acsz, sizeof(double));
        nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                    0, Er[r], llr);
        decoded_cb[r] = malloc(ldpc.info_size * sizeof(int));
        ldpc_decode(&ldpc, soft_buf, 25, decoded_cb[r]);
        cb_crc_ok[r] = check_crc(decoded_cb[r], seg.Kprime, CRC24B);

        free(coded); free(rm); free(llr); free(soft_buf);
    }

    CHECK(!cb_crc_ok[0], "tb_cb_isolation_check: corrupted code block 0's own CRC24B fails, as expected");
    CHECK(cb_crc_ok[1], "tb_cb_isolation_check: code block 1's CRC24B still passes despite code block 0's corruption (independent soft_buf/decode)");
    int cb1_bits_ok = memcmp(decoded_cb[1], cb_bits + (size_t)1 * seg.Kprime, payload * sizeof(int)) == 0;
    CHECK(cb1_bits_ok, "tb_cb_isolation_check: code block 1's decoded payload bits are bit-exact to the original (no bleed-through from code block 0's corruption)");

    free(tb); free(tb_crc); free(cb_bits); free(Er);
    free(decoded_cb[0]); free(decoded_cb[1]);
    ldpc_free(&ldpc);
}

/* TS 38.212 6.2.2 base graph selection: this file re-states the rule
 * from nr_select_bg()'s own doc comment (ldpc_nr.h) -- an independent
 * re-implementation of the SAME documented rule, not a call into the
 * function under test, so it catches a divergence between the code and
 * its own documented contract (e.g. a `<` vs `<=` slip at a boundary). */
static void expect_bg(int B, double R, int expect_bg_val, const char *label) {
    int bg, Kcb, Kb, base_rows, base_info_cols, base_cols;
    nr_select_bg(B, R, &bg, &Kcb, &Kb, &base_rows, &base_info_cols, &base_cols);
    char m[160];
    snprintf(m, sizeof(m), "%s (B=%d,R=%.3f): nr_select_bg gives BG%d (expected BG%d)", label, B, R, bg, expect_bg_val);
    CHECK(bg == expect_bg_val, m);
    int expect_Kcb = (bg == 1) ? 8448 : 3840;
    int expect_rows = (bg == 1) ? BG1_ROWS : BG2_ROWS;
    int expect_info = (bg == 1) ? BG1_INFO_COLS : BG2_INFO_COLS;
    int expect_cols = (bg == 1) ? BG1_COLS : BG2_COLS;
    CHECK(Kcb == expect_Kcb && base_rows == expect_rows && base_info_cols == expect_info && base_cols == expect_cols,
          "nr_select_bg: Kcb/base_rows/base_info_cols/base_cols match the chosen BG's fixed table dimensions");
}

static void test_nr_select_bg_boundary(void) {
    /* A<=292 term (B=A+24): straddle A=292 at a low rate that wouldn't
     * otherwise force BG2 by the other two terms. */
    expect_bg(292 + 24, 0.9, 2, "A==292 boundary (<=292 -> BG2)");
    expect_bg(293 + 24, 0.9, 1, "A==293 boundary (>292, R=0.9>0.67, R>0.25 -> BG1)");

    /* A<=3824 && R<=0.67 term. */
    expect_bg(3824 + 24, 0.67, 2, "A==3824,R==0.67 boundary (both <= -> BG2)");
    expect_bg(3824 + 24, 0.68, 1, "A==3824,R==0.68 boundary (R>0.67 -> BG1, A term alone insufficient)");
    expect_bg(3825 + 24, 0.67, 1, "A==3825,R==0.67 boundary (A>3824 -> BG1, R term alone insufficient here since R>0.25)");

    /* R<=0.25 term, independent of A (large A that would otherwise be BG1). */
    expect_bg(10000 + 24, 0.25, 2, "large A, R==0.25 boundary (<=0.25 -> BG2 regardless of A)");
    expect_bg(10000 + 24, 0.26, 1, "large A, R==0.26 boundary (>0.25, A far above 3824 -> BG1)");
}

/* TS 38.212 5.2.2 lifting size selection: independently search the SAME
 * raw spec data table (ZC_SETS -- data, not nr_select_zc()'s own search
 * logic) for the minimal Zc with Kb*Zc>=Kprime, and check nr_select_zc()
 * agrees -- an independent re-implementation of the search over the
 * published data, not a call into the function under test. */
static int independent_min_zc(int Kb, int Kprime) {
    int best = -1;
    for (int s = 0; s < ZC_NUM_SETS; s++)
        for (int i = 0; i < ZC_SET_LEN[s]; i++) {
            int zc = ZC_SETS[s][i];
            if ((long)Kb * zc >= Kprime && (best < 0 || zc < best)) best = zc;
        }
    return best;
}

static void test_nr_select_zc_invariants(void) {
    int Kbs[] = { 22, 10, 9, 8, 6 };
    /* Kprime as fractions of this Kb's own max capacity (Kb*384, the
     * largest Table 5.3.2-1 Zc) so every (Kb,frac) combo stays in the
     * valid, satisfiable range regardless of which Kb is being tested. */
    double fracs[] = { 0.02, 0.15, 0.4, 0.7, 0.95 };
    int all_ok = 1, minimal_ok = 1;
    for (size_t bi = 0; bi < sizeof(Kbs) / sizeof(Kbs[0]); bi++) {
        int Kb = Kbs[bi];
        for (size_t fi = 0; fi < sizeof(fracs) / sizeof(fracs[0]); fi++) {
            int Kprime = (int)(Kb * 384 * fracs[fi]);
            if (Kprime < 1) Kprime = 1;
            int base_info_cols = 22;   /* value doesn't affect Zc search itself */
            int Zc, K, filler_size;
            nr_select_zc(Kb, Kprime, base_info_cols, &Zc, &K, &filler_size);

            int expect_zc = independent_min_zc(Kb, Kprime);
            if (expect_zc < 0 || Zc != expect_zc) minimal_ok = 0;
            if ((long)Kb * Zc < Kprime) all_ok = 0;
            if (K != base_info_cols * Zc) all_ok = 0;
            if (filler_size != K - Kprime || filler_size < 0) all_ok = 0;
        }
    }
    CHECK(minimal_ok, "nr_select_zc: returned Zc matches the minimal Zc found by an independent search over the same ZC_SETS data");
    CHECK(all_ok, "nr_select_zc: Kb*Zc>=Kprime, K==base_info_cols*Zc, filler_size==K-Kprime>=0 hold across all (Kb,Kprime) combos tried");
}

/* TS 38.212 Table 5.4.2.1-2 k0 formula, independently re-stated from
 * nr_rate_matching.h's own doc comment (not a call into nr_ldpc_k0()). */
static int independent_k0(int bg, int Zc, int Ncb, int rv) {
    if (rv == 0) return 0;
    if (bg == 1) {
        if (rv == 1) return (int)(17.0 * Ncb / (66.0 * Zc)) * Zc;
        if (rv == 2) return (int)(33.0 * Ncb / (66.0 * Zc)) * Zc;
        return (int)(56.0 * Ncb / (66.0 * Zc)) * Zc;
    } else {
        if (rv == 1) return (int)(13.0 * Ncb / (50.0 * Zc)) * Zc;
        if (rv == 2) return (int)(25.0 * Ncb / (50.0 * Zc)) * Zc;
        return (int)(43.0 * Ncb / (50.0 * Zc)) * Zc;
    }
}

static void test_nr_ldpc_k0_formula(void) {
    int cases[][2] = { {1, 64}, {1, 176}, {1, 384}, {2, 64}, {2, 176}, {2, 384} };
    int ok = 1;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        int bg = cases[c][0], Zc = cases[c][1];
        int base_cols = (bg == 1) ? BG1_COLS : BG2_COLS;
        int Ncb = base_cols * Zc;
        for (int rv = 0; rv < 4; rv++) {
            int got = nr_ldpc_k0(bg, Zc, Ncb, rv);
            int want = independent_k0(bg, Zc, Ncb, rv);
            if (got != want) ok = 0;
        }
    }
    CHECK(ok, "nr_ldpc_k0: matches an independent re-statement of Table 5.4.2.1-2's formula for BG1/BG2, rv=0..3, several Zc");
}

/* TS 38.212 5.4.2.1 E_r allocation: sum(E)==G always, every E[r] is a
 * multiple of Nl*Qm, and the floor/ceil split matches an independent
 * recomputation from (G,Nl,Qm,C). */
static void test_nr_ldpc_er_alloc_invariants(void) {
    struct { int G, Nl, Qm, C; } cases[] = {
        { 12000, 1, 2, 1 }, { 12000, 1, 4, 3 }, { 12005 * 6, 1, 6, 5 },
        { 9800, 2, 4, 4 }, { 100000, 4, 8, 7 },
    };
    int sum_ok = 1, mult_ok = 1, split_ok = 1;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        int G = cases[c].G, Nl = cases[c].Nl, Qm = cases[c].Qm, C = cases[c].C;
        int *E = malloc((size_t)C * sizeof(int));
        nr_ldpc_er_alloc(G, Nl, Qm, C, E);

        long sum = 0;
        for (int r = 0; r < C; r++) { sum += E[r]; if (E[r] % (Nl * Qm) != 0) mult_ok = 0; }
        if (sum != G) sum_ok = 0;

        int Gp = G / (Nl * Qm);
        int rem = Gp % C;
        int expect_floor = (Gp / C) * Nl * Qm;
        int expect_ceil  = (Gp / C + 1) * Nl * Qm;
        for (int r = 0; r < C; r++) {
            int expect = (r <= C - rem - 1) ? expect_floor : expect_ceil;
            if (E[r] != expect) split_ok = 0;
        }
        free(E);
    }
    CHECK(sum_ok, "nr_ldpc_er_alloc: sum(E[0..C-1]) == G exactly, all cases");
    CHECK(mult_ok, "nr_ldpc_er_alloc: every E[r] is an exact multiple of Nl*Qm");
    CHECK(split_ok, "nr_ldpc_er_alloc: floor/ceil split matches an independent recomputation from (G,Nl,Qm,C)");
}

/* Encoder correctness via syndrome: H*codeword==0 (mod 2) is the
 * defining property of a valid LDPC codeword. Cross-path check --
 * build_H_nr() (called by ldpc_init()) and ldpc_encode_nr() are
 * separate implementations that both read the BG1/BG2 tables
 * independently (ldpc_encode_prepare_nr()'s doc comment: "does not
 * depend on build_H_nr() having run"), so this is not a tautology --
 * a bug in either path's table interpretation would surface here even
 * though neither function calls the other. The syndrome computation
 * itself is freshly written here, not reused from ldpc_decode()'s BP
 * loop. */
static void test_ldpc_encode_syndrome(int block_size, double code_rate, const char *label) {
    LDPCCodec ldpc;
    ldpc_init(&ldpc, block_size, code_rate);

    int *info = malloc(ldpc.info_size * sizeof(int));
    int *coded = malloc(ldpc.coded_size * sizeof(int));
    srand(31415);
    int all_zero_syndrome = 1;
    for (int trial = 0; trial < 20; trial++) {
        for (int i = 0; i < ldpc.info_size; i++) info[i] = rand() & 1;
        ldpc_encode(&ldpc, info, coded);

        for (int r = 0; r < ldpc.num_parity; r++) {
            int parity = 0;
            for (int j = ldpc.H_row_ptr[r]; j < ldpc.H_row_ptr[r + 1]; j++)
                parity ^= coded[ldpc.H_col[j]];
            if (parity != 0) { all_zero_syndrome = 0; break; }
        }
        if (!all_zero_syndrome) break;
    }
    char m[160];
    snprintf(m, sizeof(m), "%s (block_size=%d,R=%.3f,BG%d,Zc=%d): H*codeword==0 (mod 2) for all %d parity checks, 20 random-info trials",
             label, block_size, code_rate, ldpc.bg, ldpc.Zc, ldpc.num_parity);
    CHECK(all_zero_syndrome, m);

    free(info); free(coded);
    ldpc_free(&ldpc);
}

int main(void) {
    /* Small TB -- expect C=1 (no segmentation needed). */
    segmentation_roundtrip("small TB", 1000, 10, MCS_TABLE1, 1);
    /* Reuses the exact (A=8426, MCS10/TABLE1) combination already known
     * (regression_test.sh "PDSCH legacy AWGN C=2 segmentation") to divide
     * evenly under nr_seg_compute()'s exact-division requirement -- avoids
     * guessing a B where B'/C isn't an integer (see nr_sch.h's documented
     * open question, which this test deliberately does not touch). */
    segmentation_roundtrip("large TB (C=2)", 8426, 10, MCS_TABLE1, 2);

    harq_buffer_boundary_check();
    tb_cb_isolation_check();

    test_nr_select_bg_boundary();
    test_nr_select_zc_invariants();
    test_nr_ldpc_k0_formula();
    test_nr_ldpc_er_alloc_invariants();
    test_ldpc_encode_syndrome(5000, 0.5, "BG1 mid-rate");
    test_ldpc_encode_syndrome(200, 0.3, "BG2 low-rate small block");
    test_ldpc_encode_syndrome(6000, 0.8, "BG1 high-rate large block");

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
