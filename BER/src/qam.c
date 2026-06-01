#include "qam.h"
#include <math.h>
#include <string.h>

int qam_order(const char *mod) {
    if (!mod)                       return 2;
    if (strcmp(mod, "QPSK") == 0)   return 2;
    if (strcmp(mod, "16QAM") == 0)  return 4;
    if (strcmp(mod, "64QAM") == 0)  return 6;
    if (strcmp(mod, "256QAM") == 0) return 8;
    return 2;
}

/* Per-component normalization factor for unit average symbol power.
 * k = Qm/2 bits per PAM component, levels = {±1,±3,...,±(2^k-1)}.
 * E[I^2] = (4^k - 1)/3  =>  E[|s|^2] = 2*(4^k-1)/3  =>  norm = sqrt(2*(4^k-1)/3).
 *
 * k=1: sqrt(2)   (QPSK, ±1/sqrt(2) per component)
 * k=2: sqrt(10)  (16QAM)
 * k=3: sqrt(42)  (64QAM)
 * k=4: sqrt(170) (256QAM) */
static double pam_norm(int k) {
    return sqrt(2.0 * (double)((1 << (2*k)) - 1) / 3.0);
}

/* Encode k bits (MSB first, Gray-coded) to a normalized PAM amplitude. */
static double pam_mod(const int *bits, int k) {
    /* Bits represent the Gray code word — convert to natural binary index */
    int gray = 0;
    for (int i = 0; i < k; i++)
        gray = (gray << 1) | (bits[i] & 1);

    /* Gray → natural binary (Proakis App. B) */
    int nat = gray;
    for (int mask = nat >> 1; mask; mask >>= 1)
        nat ^= mask;

    /* Natural index → odd-integer amplitude: 0 → -(2^k-1), ..., 2^k-1 → +(2^k-1) */
    int levels = 1 << k;
    double amp = (double)(2 * nat - levels + 1);

    return amp / pam_norm(k);
}

/* Decode a normalized received PAM sample to k bits (MSB first, Gray-coded). */
static void pam_demod(double r, int k, int *bits) {
    int levels = 1 << k;
    /* De-normalize to odd-integer amplitude space */
    double amp = r * pam_norm(k);
    /* Nearest-neighbor decision: nat = round((amp + levels - 1) / 2) */
    int nat = (int)((amp + (double)(levels - 1)) / 2.0 + 0.5);
    if (nat < 0)       nat = 0;
    if (nat >= levels) nat = levels - 1;
    /* Natural → Gray */
    int gray = nat ^ (nat >> 1);
    /* Extract k bits MSB first */
    for (int i = k - 1; i >= 0; i--) {
        bits[i] = gray & 1;
        gray >>= 1;
    }
}

void qam_mod(const int *bits, int nbits, int qm, cx_t *syms) {
    int k    = qm / 2;
    int nsym = nbits / qm;
    for (int s = 0; s < nsym; s++) {
        syms[s].re = pam_mod(&bits[s * qm],     k);
        syms[s].im = pam_mod(&bits[s * qm + k], k);
    }
}

void qam_demod(const cx_t *syms, int nsym, int qm, int *bits) {
    int k = qm / 2;
    for (int s = 0; s < nsym; s++) {
        pam_demod(syms[s].re, k, &bits[s * qm]);
        pam_demod(syms[s].im, k, &bits[s * qm + k]);
    }
}

double qam_ber_theory(int qm, double eb_n0_lin) {
    /* Approximate BER for Gray-coded square M-QAM over AWGN:
     *   BER ≈ (4/Qm) * (1 - 1/sqrt(M)) * Q(sqrt(3*Qm/(M-1) * Eb/N0))
     * where Q(x) = 0.5 * erfc(x / sqrt(2)).
     * This is exact for QPSK (M=4) and tight for M>=16. */
    int M        = 1 << qm;
    double coeff = (4.0 / qm) * (1.0 - 1.0 / sqrt((double)M));
    double arg   = sqrt(3.0 * qm / (double)(M - 1) * eb_n0_lin);
    /* Q(x) = 0.5 * erfc(x / sqrt(2)) */
    return coeff * 0.5 * erfc(arg / sqrt(2.0));
}
