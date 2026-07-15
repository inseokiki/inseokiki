/* ================================================================
 *  srs.c
 *  SRS channel sounding simulation
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "srs.h"
#include "channel_estimation.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int is_prime(int n) {
    if (n < 2) return 0;
    if (n == 2) return 1;
    if (n % 2 == 0) return 0;
    for (int i = 3; i*i <= n; i += 2) if (n%i==0) return 0;
    return 1;
}
static int largest_prime_below(int n) {
    while (n >= 2 && !is_prime(n)) n--;
    return (n >= 2) ? n : 2;
}

void srs_sequence(const SRSParams *p, cx_t *out) {
    int M    = (p->mSRS_b * 12) / p->combSize;
    int N_ZC = largest_prime_below(M);
    double q_bar = (double)N_ZC * (p->seqGroupU + 1) / 31.0;
    int sign = ((int)(floor(2.0 * q_bar)) % 2 == 0) ? 1 : -1;
    int q    = (int)(floor(q_bar + 0.5)) + p->seqNumV * sign;
    for (int n = 0; n < M; n++) {
        int m = n % N_ZC;
        double ph = -PHY_PI * q * (double)m * (m + 1) / N_ZC;
        out[n] = CX_MAKE(cos(ph), sin(ph));
    }
    int N_ap   = (p->combSize == 2) ? 8 : 12;
    double alpha = 2.0 * PHY_PI * p->cyclicShift / N_ap;
    for (int n = 0; n < M; n++)
        out[n] *= CX_MAKE(cos(alpha*n), sin(alpha*n));
}

int srs_subcarrier_indices(int num_active, int mSRS_b, int comb, int comb_off,
                            int *out) {
    int bw = mSRS_b * 12;
    if (bw > num_active) bw = num_active;
    int M = bw / comb, cnt = 0;
    for (int n = 0; n < M; n++) {
        int k = comb_off + comb * n;
        if (k < num_active) out[cnt++] = k;
    }
    return cnt;
}

void run_srs_simulation(const L1Config *cfg) {
    int  active   = cfg->numRB * 12;
    int  use_ray  = (strcmp(cfg->channelModel, "AWGN") != 0);
    double inv_s2 = 1.0 / sqrt(2.0);

    SRSParams p;
    p.mSRS_b      = cfg->srsBandwidthRB;
    p.combSize    = cfg->srsCombSize;
    p.combOffset  = cfg->srsCombOffset % cfg->srsCombSize;
    p.cyclicShift = cfg->srsCyclicShift;
    p.seqGroupU   = cfg->srsSeqGroupU;
    p.seqNumV     = cfg->srsSeqNumV;
    if (p.mSRS_b * 12 > active) p.mSRS_b = active / 12;

    int bw_sc  = p.mSRS_b * 12;
    int seq_len = bw_sc / p.combSize;
    int N_ZC   = largest_prime_below(seq_len);

    int *pilot_pos = (int *)malloc((bw_sc / p.combSize + 1) * sizeof(int));
    int  num_pilots = srs_subcarrier_indices(active, p.mSRS_b, p.combSize,
                                              p.combOffset, pilot_pos);
    cx_t *tx_pilots = (cx_t *)malloc(seq_len * sizeof(cx_t));
    srs_sequence(&p, tx_pilots);
    if (seq_len > num_pilots) seq_len = num_pilots;  /* align */

    double oh = (double)num_pilots / active;
    printf("=== SRS Channel Sounding Simulation ===\n");
    printf("SRS BW (RB)  : %d  (%d subcarriers)\n", p.mSRS_b, bw_sc);
    printf("Comb size    : %d  (K_TC=%d, offset=%d)\n",
           p.combSize, p.combSize, p.combOffset);
    printf("Seq length M : %d  (ZC root N_ZC=%d)\n", seq_len, N_ZC);
    printf("Cyclic shift : %d\n", p.cyclicShift);
    printf("Seq group u  : %d  v=%d\n", p.seqGroupU, p.seqNumV);
    printf("Pilot REs    : %d  (overhead %.1f%%)\n", num_pilots, oh*100.0);
    printf("Channel      : %s\n", use_ray ? "Flat Rayleigh" : "AWGN");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%18s%18s%18s\n",
           "SNR (dB)","MSE@Pilots (dB)","MSE@All SC (dB)","Theory (dB)");
    for (int i=0;i<66;i++) printf("-");
    printf("\n");

    cx_t *rx_p = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_p  = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_f  = (cx_t *)malloc(active     * sizeof(cx_t));

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd+1e-6; snr += cfg->snrStep) {
        double snrlin = pow(10.0, snr/10.0);
        double N0     = 1.0 / snrlin;
        double sigma  = sqrt(N0 / 2.0);
        double sum_p=0, sum_a=0;
        for (int trial=0; trial<cfg->numTrials; trial++) {
            cx_t h_true = use_ray
                ? CX_MAKE(randn()*inv_s2, randn()*inv_s2) : CX_ONE;
            for (int pp=0; pp<num_pilots; pp++)
                rx_p[pp] = h_true*tx_pilots[pp]
                           + CX_MAKE(randn()*sigma, randn()*sigma);
            ls_estimate(rx_p, tx_pilots, num_pilots, h_p);
            double mp=0;
            for (int pp=0; pp<num_pilots; pp++) {
                cx_t e = h_p[pp] - h_true; mp += CX_NORM(e);
            }
            sum_p += mp / num_pilots;
            interpolate_channel(h_p, num_pilots, pilot_pos, active, h_f);
            double ma=0;
            for (int k=0; k<active; k++) {
                cx_t e = h_f[k] - h_true; ma += CX_NORM(e);
            }
            sum_a += ma / active;
        }
        double mp = sum_p/cfg->numTrials, ma = sum_a/cfg->numTrials;
        if (mp<1e-15) mp=1e-15;
        if (ma<1e-15) ma=1e-15;
        printf("%12.1f%18.2f%18.2f%18.2f\n",
               snr, 10*log10(mp), 10*log10(ma), -snr);
    }
    printf("\nZC sequence: constant-envelope (|x(n)|=1), PAPR=0dB, flat PSD.\n");
    printf("Theory: MSE@pilots ~= -SNR_dB  (LS estimator, unit-power pilots)\n");
    printf("SRS simulation complete.\n");

    free(pilot_pos); free(tx_pilots); free(rx_p); free(h_p); free(h_f);
}
