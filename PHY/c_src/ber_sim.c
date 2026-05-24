#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "config_parser.h"
#include "modulation.h"
#include "channel.h"
#include "utils.h"

int main(int argc, char *argv[]) {
    const char *cfg_file = "config/ber_config.txt";
    if (argc > 1) cfg_file = argv[1];

    ConfigParser parser;
    config_parser_load(&parser, cfg_file);
    L1Config cfg = config_parser_get(&parser);

    int bps = get_bits_per_symbol(cfg.modulation);
    long long num_bits    = (long long)cfg.numBits;
    long long num_symbols = num_bits / bps;
    num_bits = num_symbols * bps;  /* align to symbol boundary */

    double eb_to_es_db = 10.0 * log10((double)bps);

    printf("=== BER Simulation (uncoded, no OFDM) ===\n");
    printf("Modulation   : %s\n", cfg.modulation);
    printf("Channel      : %s\n", cfg.channelModel);
    printf("SNR axis     : Eb/N0\n");
    printf("Total bits   : %lld /SNR\n\n", num_bits);
    printf("%12s%15s\n", "Eb/N0 (dB)", "BER");
    for (int i=0;i<27;i++) printf("-"); printf("\n");

    const int chunk = 100000;
    int    *tx  = (int  *)malloc(chunk * bps * sizeof(int));
    int    *rx  = (int  *)malloc(chunk * bps * sizeof(int));
    cx_t   *sym = (cx_t *)malloc(chunk        * sizeof(cx_t));
    cx_t   *rsm = (cx_t *)malloc(chunk        * sizeof(cx_t));

    AWGNChannel ch;
    for (double ebN0 = cfg.snrStart; ebN0 <= cfg.snrEnd+0.001; ebN0 += cfg.snrStep) {
        double esN0 = ebN0 + eb_to_es_db;
        awgn_init(&ch, esN0);
        long long tot_err = 0, rem = num_symbols;
        while (rem > 0) {
            int n = (int)(rem < chunk ? rem : chunk);
            int nb = n * bps;
            gen_random_bits(tx, nb);
            qam_modulate(tx, nb, cfg.modulation, sym);
            if (strcmp(cfg.channelModel,"NONE")==0)
                memcpy(rsm, sym, n * sizeof(cx_t));
            else
                awgn_add_noise(&ch, sym, n, 0, rsm);
            qam_demodulate(rsm, n, cfg.modulation, rx);
            for (int i=0;i<nb;i++) if (tx[i]!=rx[i]) tot_err++;
            rem -= n;
        }
        double ber = (double)tot_err / num_bits;
        printf("%12.4f%15.4e\n", ebN0, ber);
    }
    printf("\nSimulation complete.\n");

    free(tx); free(rx); free(sym); free(rsm);
    return 0;
}
