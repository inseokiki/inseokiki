/* test_polar.c -- permanent numeric regression test for polar.c
 * (TS 38.212 5.3.1 reliability sequence/input interleaving/PC bits, and
 * the CA-SCL decoder) (lab/PHY_REVIEW_2026-09-10.md PHY-03).
 *
 * Migrated from ad-hoc scratch harnesses used during the Polar Phase
 * 1-4 standardization session (2026-09-04).
 *
 * UT-06 (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10): the round-trip
 * checks here are self-consistent (this project's own polar_encode()
 * validated by its own polar_decode()/polar_decode_scl()) -- the
 * reliability sequence and interleaver TABLES themselves (polar_tables.c)
 * were separately cross-checked against known reference values and an
 * independent extraction method when first written (see polar_tables.h's
 * provenance comment), but the encode/decode round-trip logic in this
 * file has no independently-sourced test vector to compare against.
 *
 * Build/run: see PHY/tests/README.md (run_numeric_tests.sh drives this).
 */
#include "polar.h"
#include "polar_rate_match.h"
#include "crc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

static void roundtrip(const char *label, int N, int K, int E, int I_IL, int n_PC, int n_PC_wm) {
    PolarCodec pc;
    polar_init(&pc, N, K, E, I_IL, n_PC, n_PC_wm);

    int *info = malloc(K * sizeof(int));
    int *coded = malloc(N * sizeof(int));
    int *rm = malloc(E * sizeof(int));
    double *llr_e = malloc(E * sizeof(double));
    double *llr_n = malloc(N * sizeof(double));
    int *decoded = malloc(K * sizeof(int));

    srand(12345);
    int all_ok = 1;
    for (int trial = 0; trial < 200; trial++) {
        for (int i = 0; i < K; i++) info[i] = rand() & 1;
        polar_encode(&pc, info, coded);
        polar_rate_match(coded, N, E, K, rm);
        for (int i = 0; i < E; i++) llr_e[i] = rm[i] ? -20.0 : 20.0;
        polar_rate_dematch(llr_e, E, N, K, llr_n);
        polar_decode(&pc, llr_n, decoded);
        for (int i = 0; i < K; i++)
            if (decoded[i] != info[i]) { all_ok = 0; }
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "%s: 200-trial noise-free round-trip exact (N=%d,K=%d,E=%d,I_IL=%d,n_PC=%d,n_PC_wm=%d)",
             label, N, K, E, I_IL, n_PC, n_PC_wm);
    CHECK(all_ok, msg);

    free(info); free(coded); free(rm); free(llr_e); free(llr_n); free(decoded);
    polar_free(&pc);
}

static double randn(void) {
    double u1 = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
    double u2 = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

/* SCL(L=1, no CRC) must match plain SC bit-for-bit: at L=1 the split-then-
 * prune-to-1 decision at each info-bit leaf is mathematically the same
 * comparison as plain SC's hard decision. */
static void scl_l1_matches_sc(const char *label, int N, int K, int E, int I_IL, int n_PC, int n_PC_wm) {
    PolarCodec pc;
    polar_init(&pc, N, K, E, I_IL, n_PC, n_PC_wm);

    int *info = malloc(K * sizeof(int));
    int *coded = malloc(N * sizeof(int));
    int *rm = malloc(E * sizeof(int));
    double *llr_e = malloc(E * sizeof(double));
    double *llr_n = malloc(N * sizeof(double));
    int *dec_sc = malloc(K * sizeof(int));
    int *dec_scl = malloc(K * sizeof(int));

    srand(777);
    double sigma = 0.9;
    int all_match = 1;
    for (int trial = 0; trial < 300; trial++) {
        for (int i = 0; i < K; i++) info[i] = rand() & 1;
        polar_encode(&pc, info, coded);
        polar_rate_match(coded, N, E, K, rm);
        for (int i = 0; i < E; i++) {
            double s = 1.0 - 2.0 * rm[i];
            double r = s + sigma * randn();
            llr_e[i] = 2.0 * r / (sigma * sigma);
        }
        polar_rate_dematch(llr_e, E, N, K, llr_n);
        polar_decode(&pc, llr_n, dec_sc);
        polar_decode_scl(&pc, llr_n, 1, 0, CRC24C, 0, 0, dec_scl);
        for (int i = 0; i < K; i++)
            if (dec_sc[i] != dec_scl[i]) { all_match = 0; }
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "%s: SCL(L=1,no CRC) == plain SC bit-for-bit over 300 noisy trials", label);
    CHECK(all_match, msg);

    free(info); free(coded); free(rm); free(llr_e); free(llr_n); free(dec_sc); free(dec_scl);
    polar_free(&pc);
}

/* CA-SCL(L=8) BLER must be <= plain SC's BLER at the same SNR -- the point
 * of list decoding. Payload = K-24 bits + CRC24C -> K.
 *
 * UT-05 (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10): this is a
 * statistical regression check for this file's fixed seed/sigma/trial
 * count, not a proof that hold for every noise realization -- a
 * different seed or a much smaller trial count could in principle
 * violate err_scl<=err_sc by chance (list decoding is a strict
 * improvement in expectation, not per-realization). Keep this separate
 * from the deterministic correctness checks elsewhere in this file
 * (round-trip exactness, SCL(L=1)==SC bit-for-bit), which hold for
 * every trial by construction, not just in expectation. */
static void scl_beats_sc_bler(void) {
    int N = 512, Kfull = 56, E = 864, I_IL = 1, crc_len = 24;
    int payload_len = Kfull - crc_len;
    PolarCodec pc;
    polar_init(&pc, N, Kfull, E, I_IL, 0, 0);

    int *payload = malloc(payload_len * sizeof(int));
    int *info = malloc(Kfull * sizeof(int));
    int *coded = malloc(N * sizeof(int));
    int *rm = malloc(E * sizeof(int));
    double *llr_e = malloc(E * sizeof(double));
    double *llr_n = malloc(N * sizeof(double));
    int *dec_sc = malloc(Kfull * sizeof(int));
    int *dec_scl = malloc(Kfull * sizeof(int));

    srand(2026);
    double sigma = 2.4;
    int trials = 2000;
    int err_sc = 0, err_scl = 0;
    for (int trial = 0; trial < trials; trial++) {
        for (int i = 0; i < payload_len; i++) payload[i] = rand() & 1;
        attach_crc(payload, payload_len, CRC24C, info);
        polar_encode(&pc, info, coded);
        polar_rate_match(coded, N, E, Kfull, rm);
        for (int i = 0; i < E; i++) {
            double s = 1.0 - 2.0 * rm[i];
            double r = s + sigma * randn();
            llr_e[i] = 2.0 * r / (sigma * sigma);
        }
        polar_rate_dematch(llr_e, E, N, Kfull, llr_n);

        polar_decode(&pc, llr_n, dec_sc);
        if (memcmp(dec_sc, info, Kfull * sizeof(int)) != 0) err_sc++;

        polar_decode_scl(&pc, llr_n, 8, 1, CRC24C, 0, 0, dec_scl);
        if (memcmp(dec_scl, info, Kfull * sizeof(int)) != 0) err_scl++;
    }
    printf("BLER: plain SC = %d/%d (%.4f), CA-SCL(L=8) = %d/%d (%.4f)\n",
           err_sc, trials, (double)err_sc/trials, err_scl, trials, (double)err_scl/trials);
    char msg[256];
    snprintf(msg, sizeof(msg), "CA-SCL(L=8) BLER (%d/%d) <= plain SC BLER (%d/%d)", err_scl, trials, err_sc, trials);
    CHECK(err_scl <= err_sc, msg);
    CHECK(err_sc > 0, "sanity: sigma chosen high enough that plain SC has nonzero errors");

    free(payload); free(info); free(coded); free(rm); free(llr_e); free(llr_n); free(dec_sc); free(dec_scl);
    polar_free(&pc);
}

/* RNTI-masked CRC path selection (PDCCH-specific wiring): correct RNTI must
 * always recover the true candidate; wrong RNTI must never falsely pass.
 *
 * UT-05 (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10): also a statistical
 * check, not a hard guarantee -- "right_ok==trials" depends on sigma_lo
 * being low enough that decoding essentially never fails (not zero
 * probability), and "wrong_false_pass==0" relies on CRC24C's ~2^-24
 * per-candidate collision probability being small enough that 500
 * trials * 8 candidates essentially never hits one (expected ~0.0002
 * false positives, not exactly zero). Masking-live/masking-broken is
 * the property under test; these exact counts are this seed's result,
 * not a universal bound. */
static void scl_rnti_masking(void) {
    int N = 128, Kfull = 51, E = 216, I_IL = 1, crc_len = 24;
    int dci_size = Kfull - crc_len;
    PolarCodec pc;
    polar_init(&pc, N, Kfull, E, I_IL, 0, 0);

    int *dci = malloc(dci_size * sizeof(int));
    int *info = malloc(Kfull * sizeof(int));
    int *coded = malloc(N * sizeof(int));
    int *rm = malloc(E * sizeof(int));
    double *llr_e = malloc(E * sizeof(double));
    double *llr_n = malloc(N * sizeof(double));
    int *dec_right = malloc(Kfull * sizeof(int));
    int *dec_wrong = malloc(Kfull * sizeof(int));

    srand(555);
    uint16_t rnti_right = 0x1234, rnti_wrong = 0x5678;
    double sigma_lo = 0.5, sigma_hi = 1.6;
    int trials = 500;
    int right_ok = 0, wrong_false_pass = 0;
    for (int trial = 0; trial < trials; trial++) {
        for (int i = 0; i < dci_size; i++) dci[i] = rand() & 1;
        attach_crc_rnti(dci, dci_size, CRC24C, rnti_right, info);
        polar_encode(&pc, info, coded);
        polar_rate_match(coded, N, E, Kfull, rm);
        for (int i = 0; i < E; i++) {
            double s = 1.0 - 2.0 * rm[i];
            double r = s + sigma_lo * randn();
            llr_e[i] = 2.0 * r / (sigma_lo * sigma_lo);
        }
        polar_rate_dematch(llr_e, E, N, Kfull, llr_n);
        int f1 = polar_decode_scl(&pc, llr_n, 8, 1, CRC24C, 1, rnti_right, dec_right);
        if (f1 && memcmp(dec_right, info, Kfull * sizeof(int)) == 0) right_ok++;
    }
    for (int trial = 0; trial < trials; trial++) {
        for (int i = 0; i < dci_size; i++) dci[i] = rand() & 1;
        attach_crc_rnti(dci, dci_size, CRC24C, rnti_right, info);
        polar_encode(&pc, info, coded);
        polar_rate_match(coded, N, E, Kfull, rm);
        for (int i = 0; i < E; i++) {
            double s = 1.0 - 2.0 * rm[i];
            double r = s + sigma_hi * randn();
            llr_e[i] = 2.0 * r / (sigma_hi * sigma_hi);
        }
        polar_rate_dematch(llr_e, E, N, Kfull, llr_n);
        int f2 = polar_decode_scl(&pc, llr_n, 8, 1, CRC24C, 1, rnti_wrong, dec_wrong);
        if (f2) wrong_false_pass++;
    }
    printf("RNTI masking: correct-RNTI exact-decode %d/%d, wrong-RNTI false-CRC-pass %d/%d\n",
           right_ok, trials, wrong_false_pass, trials);
    CHECK(right_ok == trials, "polar_decode_scl(use_rnti=1, correct RNTI): always finds the true candidate");
    CHECK(wrong_false_pass == 0, "polar_decode_scl(use_rnti=1, wrong RNTI): never falsely passes (masking is live)");

    free(dci); free(info); free(coded); free(rm); free(llr_e); free(llr_n); free(dec_right); free(dec_wrong);
    polar_free(&pc);
}

/* Independent Arikan kernel check: x = u*G_N with G_N = F^{(x)n} (F=[[1,0],
 * [1,1]]), computed here via the standard textbook RECURSIVE split-combine
 * definition (v = u_left XOR u_right, w = u_right, x = [transform(v),
 * transform(w)]) -- structurally different from polar.c's iterative
 * doubling-stride butterfly (polar_transform(), not exported, so this
 * couldn't call it even if desired). Verified by hand for N=4 (see file-
 * header-adjacent derivation notes) that both formulations agree; this
 * test checks that agreement holds for many random inputs at several N.
 * Exercised through polar_encode() with K=N (rate-1: I_IL=0/n_PC=0 makes
 * generate_frozen_bits() produce an all-zero frozen_mask/pc_mask, and
 * E=N disables shortening/puncturing's forced-zero side effect), so
 * polar_encode(u) == polar_transform(u) == arikan_transform_independent(u)
 * exactly for this configuration. */
static void arikan_transform_independent(const int *u, int N, int *x) {
    if (N == 1) { x[0] = u[0]; return; }
    int half = N / 2;
    int *v = malloc(half * sizeof(int));
    for (int i = 0; i < half; i++) v[i] = u[i] ^ u[half + i];
    arikan_transform_independent(v, half, x);
    arikan_transform_independent(u + half, half, x + half);
    free(v);
}

static void test_arikan_kernel_independent(void) {
    int Ns[] = { 8, 16, 32, 64 };
    srand(2468);
    for (size_t ni = 0; ni < sizeof(Ns) / sizeof(Ns[0]); ni++) {
        int N = Ns[ni];
        PolarCodec pc;
        polar_init(&pc, N, N, N, 0, 0, 0);   /* rate-1: all N positions are info bits */

        int *u = malloc(N * sizeof(int));
        int *coded = malloc(N * sizeof(int));
        int *expect = malloc(N * sizeof(int));
        int all_ok = 1;
        for (int trial = 0; trial < 50; trial++) {
            for (int i = 0; i < N; i++) u[i] = rand() & 1;
            polar_encode(&pc, u, coded);
            arikan_transform_independent(u, N, expect);
            if (memcmp(coded, expect, N * sizeof(int)) != 0) all_ok = 0;
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "N=%d, rate-1: polar_encode matches an independently-derived recursive Arikan transform, 50 random trials", N);
        CHECK(all_ok, msg);

        free(u); free(coded); free(expect);
        polar_free(&pc);
    }
}

/* TS 38.212 Table 5.4.1.1-1 sub-block interleaver pattern, independently
 * transcribed from the local primary source (3gpp/38212-hc0/38212-hc0.docx
 * word/document.xml table cells, extracted via a fresh XML table-cell
 * parse on 2026-09-11 -- not copied from polar_rate_match.c's own
 * sub_block_pattern[] table, which was extracted separately when that
 * file was first written). Matches polar_rate_match.c's table exactly
 * (confirmed by this test), giving an independent cross-check of the
 * transcription against the primary source. */
static const int SPEC_TABLE_5_4_1_1_1[32] = {
     0,  1,  2,  4,  3,  5,  6,  7,
     8, 16,  9, 17, 10, 18, 11, 19,
    12, 20, 13, 21, 14, 22, 15, 23,
    24, 25, 26, 28, 27, 29, 30, 31
};

static void test_polar_interleaver_matches_spec(void) {
    int pi32[32];
    polar_interleaver(32, pi32);
    int ok32 = memcmp(pi32, SPEC_TABLE_5_4_1_1_1, sizeof(pi32)) == 0;
    CHECK(ok32, "polar_interleaver(N=32) matches TS 38.212 Table 5.4.1.1-1, independently re-transcribed from the local primary source docx");

    /* N=64/128: polar_interleaver()'s own doc comment says larger N scales
     * the same 32-entry pattern by ratio=N/32 (pi[32*b+j] = pattern[b]*
     * ratio+j) -- an independent recomputation of that scaling rule from
     * the already-confirmed 32-entry base table, not a copy of
     * polar_interleaver()'s internal loop. */
    int Ns[] = { 64, 128 };
    for (size_t ni = 0; ni < sizeof(Ns) / sizeof(Ns[0]); ni++) {
        int N = Ns[ni], ratio = N / 32;
        int *pi = malloc(N * sizeof(int));
        polar_interleaver(N, pi);
        int ok = 1, idx = 0;
        for (int b = 0; b < 32; b++)
            for (int j = 0; j < ratio; j++) {
                int expect = SPEC_TABLE_5_4_1_1_1[b] * ratio + j;
                if (pi[idx++] != expect) ok = 0;
            }
        char msg[128];
        snprintf(msg, sizeof(msg), "polar_interleaver(N=%d) matches the independently-recomputed ratio=%d scaling of Table 5.4.1.1-1", N, ratio);
        CHECK(ok, msg);
        free(pi);
    }
}

/* TS 38.212 6.3.1.3.1 UCI n_PC boundary sweep: n_PC=3 for 18<=K<=25, else
 * 0 (see polar.h's doc comment on the 26<=K<=30 open question -- this
 * test checks the DOCUMENTED/implemented boundary, not a claim about
 * what TS 38.212 mandates for that gap). */
static void test_polar_uci_npc_boundaries(void) {
    struct { int K; int expect_nPC; } cases[] = {
        { 17, 0 }, { 18, 3 }, { 25, 3 }, { 26, 0 }, { 30, 0 }, { 31, 0 },
    };
    int ok = 1;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        int n_PC, n_PC_wm;
        polar_uci_npc(cases[c].K, 300, &n_PC, &n_PC_wm);
        if (n_PC != cases[c].expect_nPC) ok = 0;
    }
    CHECK(ok, "polar_uci_npc: n_PC boundary matches 18<=K<=25 -> 3, else 0, for K=17/18/25/26/30/31");
}

/* polar_decode_scl()'s documented return-value contract: 1 iff a
 * CRC-passing candidate was selected (and decoded[] is that candidate);
 * 0 otherwise, with decoded[] still holding a valid best-effort result
 * (the exact p==0-fallback path fixed this session, 2026-09-10 -- see
 * polar.c's SCL decode loop: previously the CRC-fail fallback returned
 * the WORST checked candidate instead of the lowest-path-metric one).
 * At noise-free/low-noise, return must be 1 with decoded==info. At heavy
 * noise, return 0 must still occur across enough trials (otherwise this
 * test isn't exercising the fallback path at all) and decoded[] must
 * always be a valid 0/1 array (never garbage/uninitialized) whenever
 * return is 0. */
static void test_scl_return_value_contract(void) {
    int N = 128, Kfull = 51, E = 216, I_IL = 1, crc_len = 24;
    int dci_size = Kfull - crc_len;
    PolarCodec pc;
    polar_init(&pc, N, Kfull, E, I_IL, 0, 0);

    int *dci = malloc(dci_size * sizeof(int));
    int *info = malloc(Kfull * sizeof(int));
    int *coded = malloc(N * sizeof(int));
    int *rm = malloc(E * sizeof(int));
    double *llr_e = malloc(E * sizeof(double));
    double *llr_n = malloc(N * sizeof(double));
    int *decoded = malloc(Kfull * sizeof(int));

    /* Noise-free: return must always be 1, decoded must always equal info. */
    srand(864213);
    int nf_ret_ok = 1, nf_match_ok = 1;
    for (int trial = 0; trial < 50; trial++) {
        for (int i = 0; i < dci_size; i++) dci[i] = rand() & 1;
        attach_crc(dci, dci_size, CRC24C, info);
        polar_encode(&pc, info, coded);
        polar_rate_match(coded, N, E, Kfull, rm);
        for (int i = 0; i < E; i++) llr_e[i] = rm[i] ? -20.0 : 20.0;
        polar_rate_dematch(llr_e, E, N, Kfull, llr_n);
        int ret = polar_decode_scl(&pc, llr_n, 8, 1, CRC24C, 0, 0, decoded);
        if (ret != 1) nf_ret_ok = 0;
        if (memcmp(decoded, info, Kfull * sizeof(int)) != 0) nf_match_ok = 0;
    }
    CHECK(nf_ret_ok, "polar_decode_scl noise-free: always returns 1 (a CRC-passing candidate always exists)");
    CHECK(nf_match_ok, "polar_decode_scl noise-free: decoded[] always equals the true info bits when return==1");

    /* Heavy noise: exercise the CRC-fail fallback path, and check
     * decoded[] is always a valid 0/1 array (never uninitialized/garbage)
     * regardless of the return value -- direct regression coverage for
     * the exact bug fixed this session. */
    double sigma_heavy = 3.0;
    int saw_fail = 0, valid_ok = 1, when1_matches_ok = 1;
    for (int trial = 0; trial < 400; trial++) {
        for (int i = 0; i < dci_size; i++) dci[i] = rand() & 1;
        attach_crc(dci, dci_size, CRC24C, info);
        polar_encode(&pc, info, coded);
        polar_rate_match(coded, N, E, Kfull, rm);
        for (int i = 0; i < E; i++) {
            double s = 1.0 - 2.0 * rm[i];
            double r = s + sigma_heavy * randn();
            llr_e[i] = 2.0 * r / (sigma_heavy * sigma_heavy);
        }
        polar_rate_dematch(llr_e, E, N, Kfull, llr_n);
        int ret = polar_decode_scl(&pc, llr_n, 8, 1, CRC24C, 0, 0, decoded);
        if (ret == 0) saw_fail = 1;
        if (ret == 1 && memcmp(decoded, info, Kfull * sizeof(int)) != 0) when1_matches_ok = 0;
        for (int i = 0; i < Kfull; i++) if (decoded[i] != 0 && decoded[i] != 1) valid_ok = 0;
    }
    CHECK(saw_fail, "polar_decode_scl heavy-noise (sigma=3.0, 400 trials): CRC-fail fallback path (return==0) is actually exercised");
    CHECK(valid_ok, "polar_decode_scl heavy-noise: decoded[] is always a valid 0/1 array, whether return is 0 or 1 (never garbage)");
    CHECK(when1_matches_ok, "polar_decode_scl heavy-noise: whenever return==1, decoded[] still equals the true info bits (CRC-pass implies correct, modulo the ~2^-24 collision chance)");

    free(dci); free(info); free(coded); free(rm); free(llr_e); free(llr_n); free(decoded);
    polar_free(&pc);
}

int main(void) {
    roundtrip("PBCH-like", 512, 56, 864, 1, 0, 0);
    roundtrip("PDCCH-like", 128, 51, 216, 1, 0, 0);

    int n_PC, n_PC_wm;
    polar_uci_npc(20, 300, &n_PC, &n_PC_wm);
    CHECK(n_PC == 3 && n_PC_wm == 1, "polar_uci_npc K=20 E=300 -> n_PC=3, n_PC_wm=1");
    roundtrip("UCI-PC(wm=1)", 64, 20, 64, 0, n_PC, n_PC_wm);

    polar_uci_npc(20, 100, &n_PC, &n_PC_wm);
    CHECK(n_PC == 3 && n_PC_wm == 0, "polar_uci_npc K=20 E=100 -> n_PC=3, n_PC_wm=0");
    roundtrip("UCI-PC(wm=0)", 64, 20, 100, 0, n_PC, n_PC_wm);

    polar_uci_npc(30, 300, &n_PC, &n_PC_wm);
    CHECK(n_PC == 0 && n_PC_wm == 0, "polar_uci_npc K=30 -> n_PC=0 (below the K>30 branch)");
    roundtrip("UCI-noPC(K=30)", 64, 30, 64, 0, n_PC, n_PC_wm);

    scl_l1_matches_sc("PBCH-like", 512, 56, 864, 1, 0, 0);
    scl_l1_matches_sc("UCI-PC(wm=1)", 64, 20, 64, 0, 3, 1);
    scl_beats_sc_bler();
    scl_rnti_masking();

    test_arikan_kernel_independent();
    test_polar_interleaver_matches_spec();
    test_polar_uci_npc_boundaries();
    test_scl_return_value_contract();

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
