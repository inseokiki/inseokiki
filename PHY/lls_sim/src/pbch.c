#include "pbch.h"
#include "crc.h"
#include "polar.h"
#include "polar_rate_match.h"
#include "modulation.h"
#include "channel.h"
#include "utils.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

void run_pbch_simulation(const L1Config *cfg) {
    const int payload  = 32;
    const int crc_bits = 24;
    const int K        = payload + crc_bits;  /* 56 */
    const int N        = 512;
    const int E        = 864;
    const int bps      = 2;  /* QPSK */

    printf("=== PBCH Simulation ===\n");
    printf("Payload: %d bits\n", payload);
    printf("CRC: CRC-24C (%d bits)\n", crc_bits);
    printf("Polar code: (%d, %d)\n", N, K);
    printf("Rate matching: %d -> %d bits\n", N, E);
    printf("Modulation: QPSK (%d symbols)\n", E/bps);
    printf("Trials per SNR: %d\n\n", cfg->numTrials);

    PolarCodec polar;
    polar_init(&polar, N, K);

    int *payload_bits   = (int *)malloc(payload * sizeof(int));
    int *with_crc       = (int *)malloc((payload + crc_bits) * sizeof(int));
    int *coded          = (int *)malloc(N * sizeof(int));
    int *rm             = (int *)malloc(E * sizeof(int));
    cx_t *syms          = (cx_t *)malloc((E/bps) * sizeof(cx_t));
    cx_t *rx_syms       = (cx_t *)malloc((E/bps) * sizeof(cx_t));
    double *llr_qam     = (double *)malloc(E * sizeof(double));
    double *llr_dm      = (double *)malloc(N * sizeof(double));
    int *decoded        = (int *)malloc(K * sizeof(int));

    printf("%12s%15s\n", "SNR (dB)", "BLER");
    for (int i=0;i<27;i++) printf("-");
    printf("\n");

    AWGNChannel ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd+0.001; snr += cfg->snrStep) {
        awgn_init(&ch, snr);
        int blk_err = 0;
        for (int trial = 0; trial < cfg->numTrials; trial++) {
            gen_random_bits(payload_bits, payload);
            attach_crc(payload_bits, payload, CRC24C, with_crc);
            polar_encode(&polar, with_crc, coded);
            polar_rate_match(coded, N, E, K, rm);
            qam_modulate(rm, E, "QPSK", syms);
            awgn_add_noise(&ch, syms, E/bps, 0, rx_syms);
            double nv = 1.0 / pow(10.0, snr / 10.0);
            qam_demap_llr(rx_syms, E/bps, "QPSK", nv, llr_qam);
            polar_rate_dematch(llr_qam, E, N, K, llr_dm);
            polar_decode(&polar, llr_dm, decoded);
            if (!check_crc(decoded, K, CRC24C)) blk_err++;
        }
        double bler = (double)blk_err / cfg->numTrials;
        printf("%12.1f%15.4e\n", snr, bler);
    }
    printf("\nPBCH simulation complete.\n");

    polar_free(&polar);
    free(payload_bits); free(with_crc); free(coded); free(rm);
    free(syms); free(rx_syms); free(llr_qam); free(llr_dm); free(decoded);
}
