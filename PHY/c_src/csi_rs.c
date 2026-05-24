#include "csi_rs.h"
#include "dmrs.h"
#include "channel_estimation.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void csirs_sequence(uint32_t c_init, int length, cx_t *out) {
    int *c = (int *)malloc(2 * length * sizeof(int));
    gold_sequence(c_init, 2 * length, c);
    double inv_s2 = 1.0 / sqrt(2.0);
    for (int m = 0; m < length; m++)
        out[m] = CX_MAKE(inv_s2*(1-2*c[2*m]), inv_s2*(1-2*c[2*m+1]));
    free(c);
}

int csirs_pilots_per_rb(int row) {
    switch (row) {
        case 1: return 3; case 2: return 1;
        case 3: return 2; case 4: return 4;
        default: return 1;
    }
}

int csirs_subcarrier_indices(int num_rb, const CSIRSMapConfig *cfg, int *out) {
    int k0  = cfg->subcarrierOffset % 12;
    int cnt = 0;
    for (int rb = 0; rb < num_rb; rb++) {
        int base = rb * 12;
        switch (cfg->row) {
            case 1:
                out[cnt++] = base + 0;
                out[cnt++] = base + 4;
                out[cnt++] = base + 8; break;
            case 2: out[cnt++] = base + k0; break;
            case 3: out[cnt++] = base+k0; out[cnt++] = base+k0+2; break;
            case 4:
                out[cnt++]=base+k0; out[cnt++]=base+k0+2;
                out[cnt++]=base+k0+4; out[cnt++]=base+k0+6; break;
            default: out[cnt++] = base + k0; break;
        }
    }
    return cnt;
}

void run_csirs_simulation(const L1Config *cfg) {
    int  num_rb   = cfg->numRB;
    int  active   = num_rb * 12;
    int  use_ray  = (strcmp(cfg->channelModel, "AWGN") != 0);
    double inv_s2 = 1.0 / sqrt(2.0);

    CSIRSMapConfig map;
    map.row             = cfg->csirsRow;
    map.scramblingID    = (uint32_t)cfg->csirsScramID;
    map.slotIdx         = 0;
    map.symbolIdx       = cfg->csirsSymbol;
    map.subcarrierOffset= cfg->csirsK0;

    uint32_t c_init = (uint32_t)(
        ((uint64_t)(14u*map.slotIdx + map.symbolIdx + 1u)
         * (2u*map.scramblingID + 1u) << 10u)
        + 2u*map.scramblingID) & 0x7FFFFFFFu;

    int *pilot_pos = (int *)malloc(csirs_pilots_per_rb(map.row) * num_rb * sizeof(int));
    int  num_pilots = csirs_subcarrier_indices(num_rb, &map, pilot_pos);
    cx_t *tx_pilots = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    csirs_sequence(c_init, num_pilots, tx_pilots);

    double pilot_oh = (double)num_pilots / active;

    printf("=== CSI-RS Channel Estimation Simulation ===\n");
    printf("Row          : %d  (density=%d RE/RB, %d %s)\n",
           cfg->csirsRow, csirs_pilots_per_rb(cfg->csirsRow),
           csirs_pilots_per_rb(cfg->csirsRow),
           cfg->csirsRow == 1 ? "port" : "ports");
    printf("Num RB       : %d\n", num_rb);
    printf("Active SC    : %d\n", active);
    printf("Pilot REs    : %d  (overhead %.1f%%)\n",
           num_pilots, pilot_oh * 100.0);
    printf("Scrambling ID: %d\n", cfg->csirsScramID);
    printf("Symbol l0    : %d\n", cfg->csirsSymbol);
    printf("Channel      : %s\n", use_ray ? "Flat Rayleigh" : "AWGN");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%18s%18s\n", "SNR (dB)", "MSE@Pilots (dB)", "MSE@All SC (dB)");
    for (int i=0;i<48;i++) printf("-"); printf("\n");

    cx_t *rx_pilots = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_pilots  = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_full    = (cx_t *)malloc(active     * sizeof(cx_t));

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double snrlin = pow(10.0, snr / 10.0);
        double N0     = 1.0 / snrlin;
        double sigma  = sqrt(N0 / 2.0);

        double sum_mse_p = 0.0, sum_mse_a = 0.0;
        for (int trial = 0; trial < cfg->numTrials; trial++) {
            cx_t h_true = use_ray
                ? CX_MAKE(randn()*inv_s2, randn()*inv_s2)
                : CX_ONE;
            for (int p = 0; p < num_pilots; p++)
                rx_pilots[p] = h_true*tx_pilots[p]
                               + CX_MAKE(randn()*sigma, randn()*sigma);
            ls_estimate(rx_pilots, tx_pilots, num_pilots, h_pilots);

            double mse_p = 0.0;
            for (int p = 0; p < num_pilots; p++) {
                cx_t e = h_pilots[p] - h_true;
                mse_p += CX_NORM(e);
            }
            sum_mse_p += mse_p / num_pilots;

            interpolate_channel(h_pilots, num_pilots, pilot_pos, active, h_full);
            double mse_a = 0.0;
            for (int k = 0; k < active; k++) {
                cx_t e = h_full[k] - h_true;
                mse_a += CX_NORM(e);
            }
            sum_mse_a += mse_a / active;
        }
        double mp = sum_mse_p / cfg->numTrials;
        double ma = sum_mse_a / cfg->numTrials;
        if (mp < 1e-15) mp = 1e-15;
        if (ma < 1e-15) ma = 1e-15;
        printf("%12.1f%18.2f%18.2f\n", snr,
               10.0*log10(mp), 10.0*log10(ma));
    }
    printf("\nTheory (LS): MSE@Pilots ~= -SNR_dB  (flat channel, unit-power pilots)\n");
    printf("CSI-RS simulation complete.\n");

    free(pilot_pos); free(tx_pilots);
    free(rx_pilots); free(h_pilots); free(h_full);
}
