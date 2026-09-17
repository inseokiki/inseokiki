/* test_modulation.c -- permanent numeric regression test for
 * modulation.c (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10 "QAM
 * 변조/LLR" row: "전 constellation 매핑, 평균전력, LLR 부호/크기,
 * 잡음 분산 입력, 독립 기대값").
 *
 * Independence notes (UT-06 spirit): the per-order closed-form formulas
 * below are a non-recursive unrolling of the SAME TS 38.211 §5.1
 * recursive bit-to-symbol definition modulation.c's compute_pam()
 * implements -- i.e. this is the same underlying spec math, re-derived
 * and written as separate, structurally different (non-recursive) code,
 * not an externally-sourced test vector. It still independently catches
 * bugs in compute_pam()'s generic recursive loop (off-by-one in bit
 * indexing, wrong recursion direction, etc.) that a self-consistent
 * modulate/demodulate round-trip alone would never reveal (a
 * consistently-wrong mapping round-trips with itself perfectly fine).
 * The average-power==1.0 check, by contrast, IS an independent property:
 * TS 38.211's modulation mapper is defined to produce unit average
 * energy per constellation (that's the documented reason for the
 * 1/sqrt(2), 1/sqrt(10), 1/sqrt(42) normalizers), checkable by
 * exhaustively averaging |symbol|^2 over every equiprobable bit pattern
 * without referring to modulation.c's own formula at all.
 */
#include "modulation.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

/* Independent (non-recursive) closed-form per TS 38.211 Table
 * 5.1.2-1/5.1.3-1/5.1.4-1 for QPSK/16QAM/64QAM. bpd = bits per dimension
 * (1/2/3). ib[]/qb[] are the I/Q bit groups in the same order
 * modulation.c's qam_modulate() extracts them (bits[2k], bits[2k+1]). */
static double closed_form_level(const int *b, int bpd) {
    if (bpd == 1) return (double)(1 - 2 * b[0]);
    if (bpd == 2) return (double)(1 - 2 * b[0]) * (2.0 - (double)(1 - 2 * b[1]));
    /* bpd == 3 */
    return (double)(1 - 2 * b[0]) * (4.0 - (double)(1 - 2 * b[1]) * (2.0 - (double)(1 - 2 * b[2])));
}

static void exhaustive_mapping_check(const char *mod, int bpd, double norm) {
    int bps = 2 * bpd;
    int nsym = 1 << bps;
    int mismatch = 0;
    double sum_pow = 0.0;
    for (int s = 0; s < nsym; s++) {
        int bits[8];
        for (int k = 0; k < bps; k++) bits[k] = (s >> (bps - 1 - k)) & 1;
        cx_t sym;
        qam_modulate(bits, bps, mod, &sym);

        int ib[3], qb[3];
        for (int k = 0; k < bpd; k++) { ib[k] = bits[2 * k]; qb[k] = bits[2 * k + 1]; }
        double expect_I = closed_form_level(ib, bpd) * norm;
        double expect_Q = closed_form_level(qb, bpd) * norm;
        if (fabs(creal(sym) - expect_I) > 1e-12 || fabs(cimag(sym) - expect_Q) > 1e-12)
            mismatch++;
        sum_pow += CX_NORM(sym);
    }
    char m[160];
    snprintf(m, sizeof(m), "%s: qam_modulate matches independent closed-form for all %d symbols", mod, nsym);
    CHECK(mismatch == 0, m);
    double avg_pow = sum_pow / nsym;
    snprintf(m, sizeof(m), "%s: average symbol power over all equiprobable bit patterns == 1.0 (got %.6f)", mod, avg_pow);
    CHECK(fabs(avg_pow - 1.0) < 1e-9, m);
}

static void demod_roundtrip_noise_free(const char *mod, int bps) {
    int nsym = 1 << bps;
    int all_ok = 1;
    for (int s = 0; s < nsym; s++) {
        int bits[8], out[8];
        for (int k = 0; k < bps; k++) bits[k] = (s >> (bps - 1 - k)) & 1;
        cx_t sym;
        qam_modulate(bits, bps, mod, &sym);
        qam_demodulate(&sym, 1, mod, out);
        if (memcmp(bits, out, bps * sizeof(int)) != 0) all_ok = 0;
    }
    char m[128];
    snprintf(m, sizeof(m), "%s: noise-free modulate->demodulate recovers every bit pattern exactly", mod);
    CHECK(all_ok, m);
}

static void llr_sign_and_scaling_check(void) {
    /* QPSK: bit=0 maps to the +1/sqrt(2) level (closed_form_level with
     * b[0]=0 gives +1). A received symbol far on the positive side
     * should give a strongly positive LLR (this codebase's convention,
     * matching polar.c: LLR>0 favors bit=0), and LLR magnitude must
     * scale as 2/noise_var for a fixed received point (exact from the
     * qam_demap_llr() formula, itself a direct algebraic consequence of
     * squared-distance differences scaled by 2/noise_var). */
    cx_t far_positive = CX_MAKE(10.0, 10.0);
    double llr_a[2], llr_b[2];
    qam_demap_llr(&far_positive, 1, "QPSK", 1.0, llr_a);
    qam_demap_llr(&far_positive, 1, "QPSK", 2.0, llr_b);

    CHECK(llr_a[0] > 0.0 && llr_a[1] > 0.0,
          "QPSK LLR: received symbol far on the +I/+Q side gives positive LLR for both bits (favors bit=0)");

    cx_t far_negative = CX_MAKE(-10.0, -10.0);
    double llr_c[2];
    qam_demap_llr(&far_negative, 1, "QPSK", 1.0, llr_c);
    CHECK(llr_c[0] < 0.0 && llr_c[1] < 0.0,
          "QPSK LLR: received symbol far on the -I/-Q side gives negative LLR for both bits (favors bit=1)");

    CHECK(fabs(llr_a[0] - 2.0 * llr_b[0]) < 1e-9 && fabs(llr_a[1] - 2.0 * llr_b[1]) < 1e-9,
          "QPSK LLR: doubling noise_var exactly halves LLR magnitude at a fixed received point (2/noise_var scaling)");
}

/* qam_demap_llr_re() (tasks/todo.md "RE별 effective noise variance 기반
 * LLR", P1-1): a per-RE noise_var[] that happens to be constant across
 * all n symbols must reproduce qam_demap_llr()'s scalar-noise_var output
 * bit-for-bit (both formulas reduce to the identical sc=2/noise_var per
 * symbol in that case) -- this is the exact property TDL call sites rely
 * on when they stop averaging nv_re[] into one scalar and start passing
 * the array directly. Also checks the actually-varying case against a
 * hand-derived per-RE scaling. */
static void llr_re_matches_scalar_when_constant(void) {
    cx_t syms[5] = {
        CX_MAKE(10.0, 10.0), CX_MAKE(-3.0, 4.0), CX_MAKE(0.5, -0.5),
        CX_MAKE(-7.0, -2.0), CX_MAKE(2.0, 6.0)
    };
    double nv_const[5] = {0.7, 0.7, 0.7, 0.7, 0.7};
    double llr_scalar[5*4], llr_re[5*4];
    qam_demap_llr(syms, 5, "16QAM", 0.7, llr_scalar);
    qam_demap_llr_re(syms, 5, "16QAM", nv_const, llr_re);
    int all_eq = 1;
    for (int i = 0; i < 5*4; i++) if (fabs(llr_scalar[i] - llr_re[i]) > 1e-12) all_eq = 0;
    CHECK(all_eq, "qam_demap_llr_re() with a constant noise_var[] array matches qam_demap_llr()'s scalar path exactly, for every RE/bit");

    /* Per-RE case: each RE's own noise_var scales only that RE's own bits
     * by 2/noise_var[i], independent of every other RE's value -- checked
     * by comparing RE i's LLR under a per-RE array against RE i's LLR
     * from a separate all-constant call using RE i's own noise_var (the
     * two calls must agree exactly on that one RE since qam_demap_llr_re()
     * processes each RE independently with no cross-RE state). */
    double nv_varying[5] = {0.1, 5.0, 0.7, 50.0, 0.02};
    double llr_varying[5*4];
    qam_demap_llr_re(syms, 5, "16QAM", nv_varying, llr_varying);
    int per_re_ok = 1;
    for (int i = 0; i < 5; i++) {
        double nv_i[5] = {nv_varying[i], nv_varying[i], nv_varying[i], nv_varying[i], nv_varying[i]};
        double llr_i[5*4];
        qam_demap_llr_re(syms, 5, "16QAM", nv_i, llr_i);
        for (int k = 0; k < 4; k++)
            if (fabs(llr_varying[i*4+k] - llr_i[i*4+k]) > 1e-12) per_re_ok = 0;
    }
    CHECK(per_re_ok, "qam_demap_llr_re(): each RE's LLR depends only on that RE's own noise_var[i], not on neighboring REs' values");
}

int main(void) {
    exhaustive_mapping_check("QPSK", 1, 1.0 / sqrt(2.0));
    exhaustive_mapping_check("16QAM", 2, 1.0 / sqrt(10.0));
    exhaustive_mapping_check("64QAM", 3, 1.0 / sqrt(42.0));

    demod_roundtrip_noise_free("QPSK", 2);
    demod_roundtrip_noise_free("16QAM", 4);
    demod_roundtrip_noise_free("64QAM", 6);

    llr_sign_and_scaling_check();
    llr_re_matches_scalar_when_constant();

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
