/* ================================================================
 *  modulation.c
 *  QAM modulation/demodulation, LLR demapping, soft symbols for turbo eq.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "modulation.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdio.h>

static double norm_factor(int bps) {
    switch (bps) {
        case 2: return 1.0 / sqrt(2.0);
        case 4: return 1.0 / sqrt(10.0);
        case 6: return 1.0 / sqrt(42.0);
        case 8: return 1.0 / sqrt(170.0);
        default: return 1.0;
    }
}

static double compute_pam(const int *bits, int n) {
    if (n == 1) return (double)(1 - 2 * bits[0]);
    double inner = 2.0 - (1 - 2 * bits[n - 1]);
    for (int k = n - 2; k >= 1; k--) {
        double pw = (double)(1 << (n - k));
        inner = pw - (1 - 2 * bits[k]) * inner;
    }
    return (1 - 2 * bits[0]) * inner;
}

typedef struct { double level; int bits; } PamEntry;

static void build_pam_table(PamEntry *t, int bpd) {
    int nl = 1 << bpd;
    for (int idx = 0; idx < nl; idx++) {
        int ba[4];
        for (int k = 0; k < bpd; k++)
            ba[k] = (idx >> (bpd - 1 - k)) & 1;
        t[idx].level = compute_pam(ba, bpd);
        t[idx].bits  = idx;
    }
}

int get_bits_per_symbol(const char *mod) {
    if (strcmp(mod, "QPSK")   == 0) return 2;
    if (strcmp(mod, "16QAM")  == 0) return 4;
    if (strcmp(mod, "64QAM")  == 0) return 6;
    if (strcmp(mod, "256QAM") == 0) return 8;
    return 2;
}

void qam_modulate(const int *bits, int num_bits, const char *mod, cx_t *syms) {
    int bps  = get_bits_per_symbol(mod);
    int bpd  = bps / 2;
    double n = norm_factor(bps);
    int ns   = num_bits / bps;
    for (int i = 0; i < ns; i++) {
        int off = i * bps;
        int ib[4], qb[4];
        for (int k = 0; k < bpd; k++) {
            ib[k] = bits[off + 2*k];
            qb[k] = bits[off + 2*k + 1];
        }
        double Iv = compute_pam(ib, bpd) * n;
        double Qv = compute_pam(qb, bpd) * n;
        syms[i] = CX_MAKE(Iv, Qv);
    }
}

void qam_demodulate(const cx_t *syms, int n, const char *mod, int *bits) {
    int bps = get_bits_per_symbol(mod);
    int bpd = bps / 2;
    double nm = norm_factor(bps);
    PamEntry t[16];
    build_pam_table(t, bpd);
    int nl = 1 << bpd;
    for (int i = 0; i < n; i++) {
        double rI = creal(syms[i]), rQ = cimag(syms[i]);
        double mdI = DBL_MAX; int biI = 0;
        double mdQ = DBL_MAX; int biQ = 0;
        for (int j = 0; j < nl; j++) {
            double dI = rI - t[j].level * nm, dQ = rQ - t[j].level * nm;
            if (dI*dI < mdI) { mdI = dI*dI; biI = j; }
            if (dQ*dQ < mdQ) { mdQ = dQ*dQ; biQ = j; }
        }
        int off = i * bps;
        for (int k = 0; k < bpd; k++) {
            bits[off + 2*k]     = (biI >> (bpd - 1 - k)) & 1;
            bits[off + 2*k + 1] = (biQ >> (bpd - 1 - k)) & 1;
        }
    }
}

void qam_demap_llr(const cx_t *syms, int n, const char *mod,
                   double noise_var, double *llr) {
    int bps  = get_bits_per_symbol(mod);
    int bpd  = bps / 2;
    double nm = norm_factor(bps);
    PamEntry t[16];
    build_pam_table(t, bpd);
    int nl    = 1 << bpd;
    double lv[16];  /* max 16 PAM levels (256QAM: 4bpd -> 16) */
    for (int j = 0; j < nl; j++) lv[j] = t[j].level * nm;
    double sc = 2.0 / noise_var;
    for (int i = 0; i < n; i++) {
        double rI = creal(syms[i]), rQ = cimag(syms[i]);
        for (int k = 0; k < bpd; k++) {
            double m0I=DBL_MAX, m1I=DBL_MAX, m0Q=DBL_MAX, m1Q=DBL_MAX;
            for (int j = 0; j < nl; j++) {
                int bv = (t[j].bits >> (bpd - 1 - k)) & 1;
                double dI = rI - lv[j]; double dQ = rQ - lv[j];
                double dI2 = dI*dI,     dQ2 = dQ*dQ;
                if (bv == 0) { if (dI2 < m0I) m0I=dI2; if (dQ2 < m0Q) m0Q=dQ2; }
                else         { if (dI2 < m1I) m1I=dI2; if (dQ2 < m1Q) m1Q=dQ2; }
            }
            llr[i*bps + 2*k]     = sc * (m1I - m0I);
            llr[i*bps + 2*k + 1] = sc * (m1Q - m0Q);
        }
    }
}

void qam_demap_llr_mmse(const cx_t *rx, int n, const char *mod,
                         const cx_t *h_data, double N0, double *llr) {
    int bps  = get_bits_per_symbol(mod);
    int bpd  = bps / 2;
    double nm = norm_factor(bps);
    PamEntry t[16];
    build_pam_table(t, bpd);
    int nl = 1 << bpd;
    double lv[16];
    for (int j = 0; j < nl; j++) lv[j] = t[j].level * nm;
    for (int i = 0; i < n; i++) {
        double hp = CX_NORM(h_data[i]);
        if (hp < 1e-10) {
            for (int k = 0; k < bps; k++) llr[i*bps + k] = 0.0;
            continue;
        }
        double denom  = hp + N0;
        double alpha  = hp / denom;
        double sigma2 = N0 * hp / (denom * denom);
        double sc     = 2.0 / sigma2;
        double rI = creal(rx[i]), rQ = cimag(rx[i]);
        for (int k = 0; k < bpd; k++) {
            double m0I=DBL_MAX, m1I=DBL_MAX, m0Q=DBL_MAX, m1Q=DBL_MAX;
            for (int j = 0; j < nl; j++) {
                int bv = (t[j].bits >> (bpd - 1 - k)) & 1;
                double sl = alpha * lv[j];
                double dI = rI - sl; double dQ = rQ - sl;
                double d2I = dI*dI,  d2Q = dQ*dQ;
                if (bv==0){if(d2I<m0I)m0I=d2I;if(d2Q<m0Q)m0Q=d2Q;}
                else      {if(d2I<m1I)m1I=d2I;if(d2Q<m1Q)m1Q=d2Q;}
            }
            llr[i*bps + 2*k]     = sc * (m1I - m0I);
            llr[i*bps + 2*k + 1] = sc * (m1Q - m0Q);
        }
    }
}

static double stable_sigmoid(double x) {
    if (x >= 0) { double e = exp(-x); return 1.0 / (1.0 + e); }
    else        { double e = exp(x);  return e / (1.0 + e); }
}

/* Soft-symbol statistics from a priori bit LLRs (turbo equalization).
 * P(bit=0) = sigmoid(LLR) (standard LLR=log(P0/P1) convention, matching
 * qam_demap_llr's sign). Per I/Q rail, enumerate the same PAM table
 * qam_demap_llr() uses and weight each level by the product of its bits'
 * probabilities -- exact for this codebase's separable I/Q PAM
 * construction, independent of whether the mapping is Gray or not. */
void qam_soft_symbol(const double *llr_apriori, int n, const char *mod,
                     cx_t *mean_out, double *var_out) {
    int bps = get_bits_per_symbol(mod);
    int bpd = bps / 2;
    double nm = norm_factor(bps);
    PamEntry t[16];
    build_pam_table(t, bpd);
    int nl = 1 << bpd;
    double lv[16];
    for (int j = 0; j < nl; j++) lv[j] = t[j].level * nm;

    for (int i = 0; i < n; i++) {
        int off = i * bps;
        double p0I[4], p0Q[4];
        for (int k = 0; k < bpd; k++) {
            p0I[k] = stable_sigmoid(llr_apriori[off + 2*k]);
            p0Q[k] = stable_sigmoid(llr_apriori[off + 2*k + 1]);
        }
        double meanI=0.0, meanQ=0.0, msqI=0.0, msqQ=0.0, sumI=0.0, sumQ=0.0;
        for (int j = 0; j < nl; j++) {
            double probI = 1.0, probQ = 1.0;
            for (int k = 0; k < bpd; k++) {
                int bv = (t[j].bits >> (bpd - 1 - k)) & 1;
                probI *= bv ? (1.0 - p0I[k]) : p0I[k];
                probQ *= bv ? (1.0 - p0Q[k]) : p0Q[k];
            }
            meanI += probI * lv[j]; sumI += probI; msqI += probI * lv[j]*lv[j];
            meanQ += probQ * lv[j]; sumQ += probQ; msqQ += probQ * lv[j]*lv[j];
        }
        if (sumI < 1e-300) sumI = 1e-300;
        if (sumQ < 1e-300) sumQ = 1e-300;
        meanI /= sumI; msqI /= sumI;
        meanQ /= sumQ; msqQ /= sumQ;
        double varI = msqI - meanI*meanI; if (varI < 0.0) varI = 0.0;
        double varQ = msqQ - meanQ*meanQ; if (varQ < 0.0) varQ = 0.0;
        mean_out[i] = CX_MAKE(meanI, meanQ);
        var_out[i]  = varI + varQ;
    }
}

void qpsk_modulate(const int *bits, int n, cx_t *syms) {
    double sc = 1.0 / sqrt(2.0);
    int ns = n / 2;
    for (int i = 0; i < ns; i++)
        syms[i] = CX_MAKE((1 - 2*bits[2*i])*sc, (1 - 2*bits[2*i+1])*sc);
}

void qpsk_demodulate(const cx_t *syms, int n, int *bits) {
    for (int i = 0; i < n; i++) {
        bits[2*i]   = (creal(syms[i]) < 0) ? 1 : 0;
        bits[2*i+1] = (cimag(syms[i]) < 0) ? 1 : 0;
    }
}
