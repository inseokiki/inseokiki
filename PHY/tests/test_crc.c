/* test_crc.c -- permanent numeric regression test for crc.c
 * (lab/PHY_UNIT_VALIDATION_PLAN.md, 2026-09-10 "CRC" row: "종류별 독립
 * 벡터, bit order, 오류 검출, RNTI masking").
 *
 * Independence notes (UT-06 spirit -- be honest about what's actually
 * independently verified vs self-consistent):
 *   - CRC16 is cross-checked against the well-known public CRC-16/XMODOM
 *     (aka CRC-CCITT, poly 0x1021, init 0x0000, no reflection, no final
 *     XOR) check value for the industry-standard ASCII test string
 *     "123456789" -> 0x31C3. This value is widely published/memorized in
 *     CRC literature independent of this codebase.
 *   - CRC24A/24B/24C (LTE/NR-specific 24-bit polynomials) have no such
 *     widely-memorized short check value available here, and this file
 *     does NOT fabricate one -- per this project's "확인 안 됨은 명시"
 *     principle, only structural properties that are mathematically
 *     guaranteed for ANY correct CRC of a nontrivial generator polynomial
 *     are checked for them: (a) the full codeword (data+crc) is exactly
 *     divisible by the generator (remainder-zero property, computed via
 *     compute_crc() applied to the CONCATENATED codeword -- a strictly
 *     stronger self-test than merely calling check_crc()), (b) every
 *     single-bit-flip position is detected (guaranteed for any
 *     multi-term generator polynomial, a standard linear-block-code
 *     property, not something that needs per-polynomial hand-verification).
 */
#include "crc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

/* MSB-first byte array -> bit array (bit[0] = MSB of byte[0]). */
static void bytes_to_bits(const unsigned char *bytes, int nbytes, int *bits) {
    for (int i = 0; i < nbytes; i++)
        for (int b = 0; b < 8; b++)
            bits[i * 8 + b] = (bytes[i] >> (7 - b)) & 1;
}

static unsigned int bits_to_uint(const int *bits, int n) {
    unsigned int v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | (unsigned int)bits[i];
    return v;
}

static void test_crc16_known_check_value(void) {
    const unsigned char msg[] = "123456789";
    int nbytes = 9;
    int bits[9 * 8];
    bytes_to_bits(msg, nbytes, bits);

    int crc[16];
    compute_crc(bits, nbytes * 8, CRC16, crc);
    unsigned int got = bits_to_uint(crc, 16);
    char m[128];
    snprintf(m, sizeof(m), "CRC16(\"123456789\") == well-known CRC-16/XMODEM check value 0x31C3 (got 0x%04X)", got);
    CHECK(got == 0x31C3u, m);
}

/* Property: for a correct CRC, the full codeword (data bits followed by
 * its own CRC bits) is exactly divisible by the generator polynomial --
 * i.e. compute_crc() of the FULL codeword (not just the data) must be
 * all-zero. This holds for every CRCType by construction of the
 * algorithm (not something that needs a per-polynomial known vector),
 * and is a strictly independent check from check_crc() itself (does not
 * call check_crc() at all -- recomputes from first principles). */
static void test_full_codeword_divisible(CRCType type, const char *label) {
    int crc_len = get_crc_length(type);
    int data_len = 37;   /* arbitrary, not a multiple of any CRC length */
    int data[37];
    for (int i = 0; i < data_len; i++) data[i] = (i * 13 + 5) % 2;

    int codeword[37 + 24];
    attach_crc(data, data_len, type, codeword);

    int remainder[24];
    compute_crc(codeword, data_len + crc_len, type, remainder);
    int all_zero = 1;
    for (int i = 0; i < crc_len; i++) if (remainder[i] != 0) all_zero = 0;

    char m[160];
    snprintf(m, sizeof(m), "%s: full codeword (data+crc) is exactly divisible by the generator (remainder-zero)", label);
    CHECK(all_zero, m);
}

static void test_single_bit_flip_always_detected(CRCType type, const char *label) {
    int crc_len = get_crc_length(type);
    int data_len = 50;
    int data[50];
    for (int i = 0; i < data_len; i++) data[i] = (i * 7 + 3) % 2;

    int codeword[50 + 24];
    attach_crc(data, data_len, type, codeword);
    CHECK(check_crc(codeword, data_len + crc_len, type), "sanity: unmodified codeword passes check_crc");

    int total = data_len + crc_len;
    int missed = 0;
    for (int flip = 0; flip < total; flip++) {
        int tmp[50 + 24];
        memcpy(tmp, codeword, total * sizeof(int));
        tmp[flip] ^= 1;
        if (check_crc(tmp, total, type)) missed++;
    }
    char m[160];
    snprintf(m, sizeof(m), "%s: every single-bit flip (%d positions) is detected", label, total);
    CHECK(missed == 0, m);
}

static void test_bit_order_matches_compute_crc_prefix(void) {
    /* bit order sanity: attach_crc's first data_len output bits must be
     * an exact copy of the input (not reversed/permuted), and the CRC
     * bits attach_crc appends must equal compute_crc()'s direct output
     * for the same input -- i.e. attach_crc is exactly "copy then
     * append compute_crc()", nothing more. */
    int data[20] = {1,0,1,1,0,0,1,0,1,1,0,1,0,0,0,1,1,0,1,0};
    int out[20 + 24];
    attach_crc(data, 20, CRC24C, out);
    int prefix_ok = memcmp(out, data, 20 * sizeof(int)) == 0;
    CHECK(prefix_ok, "attach_crc: first data_len output bits are an exact, unpermuted copy of the input");

    int expect_crc[24];
    compute_crc(data, 20, CRC24C, expect_crc);
    int suffix_ok = memcmp(out + 20, expect_crc, 24 * sizeof(int)) == 0;
    CHECK(suffix_ok, "attach_crc: appended CRC bits exactly match compute_crc()'s direct output");
}

static void test_rnti_masking(void) {
    int data[30];
    for (int i = 0; i < 30; i++) data[i] = (i * 3 + 1) % 2;

    /* RNTI=0 must be a no-op (XOR with 16 zero bits) -- attach_crc_rnti
     * with rnti=0 should produce byte-identical output to plain
     * attach_crc. */
    int out_plain[30 + 24], out_rnti0[30 + 24];
    attach_crc(data, 30, CRC24C, out_plain);
    attach_crc_rnti(data, 30, CRC24C, 0, out_rnti0);
    CHECK(memcmp(out_plain, out_rnti0, sizeof(out_plain)) == 0,
          "attach_crc_rnti(rnti=0) is byte-identical to plain attach_crc (all-zero mask is a no-op)");

    uint16_t rnti = 0xBEEF;
    int out_rnti[30 + 24];
    attach_crc_rnti(data, 30, CRC24C, rnti, out_rnti);
    CHECK(check_crc_rnti(out_rnti, 30 + 24, CRC24C, rnti),
          "correct RNTI: check_crc_rnti passes");
    CHECK(!check_crc_rnti(out_rnti, 30 + 24, CRC24C, (uint16_t)(rnti ^ 1)),
          "wrong RNTI (single bit different): check_crc_rnti fails");
    CHECK(!check_crc(out_rnti, 30 + 24, CRC24C),
          "RNTI-masked codeword does NOT pass plain (unmasked) check_crc (masking actually changes the bits)");
}

int main(void) {
    test_crc16_known_check_value();
    test_bit_order_matches_compute_crc_prefix();
    test_rnti_masking();

    test_full_codeword_divisible(CRC24A, "CRC24A");
    test_full_codeword_divisible(CRC24B, "CRC24B");
    test_full_codeword_divisible(CRC24C, "CRC24C");
    test_full_codeword_divisible(CRC16,  "CRC16");

    test_single_bit_flip_always_detected(CRC24A, "CRC24A");
    test_single_bit_flip_always_detected(CRC24B, "CRC24B");
    test_single_bit_flip_always_detected(CRC24C, "CRC24C");
    test_single_bit_flip_always_detected(CRC16,  "CRC16");

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
