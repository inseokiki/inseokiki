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

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
