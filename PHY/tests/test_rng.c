/* test_rng.c -- permanent regression test for utils.c's RNG reproducibility
 * (lab/PHY_REVIEW_2026-09-10.md PHY-05).
 *
 * Checks: same seed -> identical sequence; different seed -> different
 * sequence; and the randn() cached-spare bug (a mid-process reseed must
 * not leak the previous stream's cached Box-Muller spare into the new
 * stream's first draw).
 */
#include "utils.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

static void draw_bits_and_gaussians(int *bits, int nbits, double *gauss, int ngauss) {
    gen_random_bits(bits, nbits);
    for (int i = 0; i < ngauss; i++) gauss[i] = randn();
}

int main(void) {
    int bits_a[50], bits_b[50], bits_c[50];
    double g_a[50], g_b[50], g_c[50];

    rng_seed(111);
    draw_bits_and_gaussians(bits_a, 50, g_a, 50);

    rng_seed(111);
    draw_bits_and_gaussians(bits_b, 50, g_b, 50);

    rng_seed(222);
    draw_bits_and_gaussians(bits_c, 50, g_c, 50);

    CHECK(memcmp(bits_a, bits_b, sizeof(bits_a)) == 0, "same seed -> identical gen_random_bits() sequence");
    CHECK(memcmp(g_a, g_b, sizeof(g_a)) == 0, "same seed -> identical randn() sequence");
    CHECK(memcmp(bits_a, bits_c, sizeof(bits_a)) != 0, "different seed -> different gen_random_bits() sequence");
    CHECK(memcmp(g_a, g_c, sizeof(g_a)) != 0, "different seed -> different randn() sequence");

    /* Reseed-mid-process: draw an ODD number of randn() calls first (so a
     * Box-Muller spare is guaranteed to be cached), reseed, then draw a
     * fresh sequence -- it must match a process that started fresh with
     * that seed from the very first call, proving the stale spare from
     * the pre-reseed stream did not leak into the post-reseed stream. */
    rng_seed(333);
    double throwaway[3];
    for (int i = 0; i < 3; i++) throwaway[i] = randn();  /* odd count -> spare now cached */
    (void)throwaway;
    rng_seed(444);
    double after_reseed[20];
    for (int i = 0; i < 20; i++) after_reseed[i] = randn();

    rng_seed(444);
    double fresh[20];
    for (int i = 0; i < 20; i++) fresh[i] = randn();

    CHECK(memcmp(after_reseed, fresh, sizeof(fresh)) == 0,
          "mid-process reseed matches a fresh start with the same seed (no stale Box-Muller spare leak)");

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
