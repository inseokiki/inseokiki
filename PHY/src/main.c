/* ================================================================
 *  main.c
 *  Entry point -- dispatches to the per-channel simulation
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "config_parser.h"
#include "utils.h"
#include "modulation.h"
#include "ofdm.h"
#include "channel.h"
#include "ldpc.h"
#include "polar.h"
#include "pbch.h"
#include "pdcch.h"
#include "pdsch.h"
#include "csi_rs.h"
#include "srs.h"
#include "pusch.h"
#include "pucch.h"
#include "prach.h"

static void run_ber_sim(const L1Config *cfg) {
    int bps = get_bits_per_symbol(cfg->modulation);
    const int blk = 1000;
    int pad = ((blk + bps - 1) / bps) * bps;
    int nsym = pad / bps;

    int    *info   = (int *)malloc(blk  * sizeof(int));
    int    *tx     = (int *)malloc(pad  * sizeof(int));
    int    *rx     = (int *)malloc(pad  * sizeof(int));
    cx_t   *syms   = (cx_t *)malloc(nsym * sizeof(cx_t));
    cx_t   *rxsyms = (cx_t *)malloc(nsym * sizeof(cx_t));

    printf("=== BER Simulation (uncoded, no OFDM) ===\n");
    printf("Modulation   : %s\n", cfg->modulation);
    printf("Channel      : %s\n", cfg->channelModel);
    printf("Block Size   : %d bits\n", blk);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<42;i++) printf("-");
    printf("\n");

    AWGNChannel ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+0.001; snr+=cfg->snrStep) {
        awgn_init(&ch, snr);
        long long total_err=0; int blk_err=0;
        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(info, blk);
            memcpy(tx, info, blk * sizeof(int));
            for (int i=blk; i<pad; i++) tx[i] = 0;
            qam_modulate(tx, pad, cfg->modulation, syms);
            if (strcmp(cfg->channelModel,"NONE")==0)
                memcpy(rxsyms, syms, nsym * sizeof(cx_t));
            else
                awgn_add_noise(&ch, syms, nsym, 0, rxsyms);
            qam_demodulate(rxsyms, nsym, cfg->modulation, rx);
            int be=0;
            for (int i=0;i<blk;i++) if (info[i]!=rx[i]) be++;
            total_err += be;
            if (be>0) blk_err++;
        }
        double ber  = (double)total_err  / ((long long)cfg->numTrials * blk);
        double bler = (double)blk_err    / cfg->numTrials;
        printf("%12.4f%15.4e%15.4f\n", snr, ber, bler);
    }
    printf("\nSimulation complete.\n");
    free(info); free(tx); free(rx); free(syms); free(rxsyms);
}

static void run_legacy_sim(const L1Config *cfg) {
    int bps = get_bits_per_symbol(cfg->modulation);
    OFDMCtx ofdm; ofdm_init(&ofdm, cfg->nfft, cfg->cpLengthNormal);

    int info_blk = 128;
    LDPCCodec ldpc; ldpc_init(&ldpc, info_blk, cfg->codeRate);
    PolarCodec polar; polar_init(&polar, 256, info_blk, 256);

    int use_ldpc  = (strcmp(cfg->coding,"LDPC")==0);
    int use_polar = (strcmp(cfg->coding,"POLAR")==0);
    int use_code  = use_ldpc || use_polar;
    int coded_sz  = use_ldpc ? ldpc.coded_size :
                    use_polar ? polar.N : info_blk;
    int pad = coded_sz + ((coded_sz%bps) ? bps - coded_sz%bps : 0);

    printf("=== Legacy Simulation (OFDM-based) ===\n");
    printf("Modulation   : %s\n", cfg->modulation);
    printf("Channel      : %s\n", cfg->channelModel);
    printf("Block Size   : %d bits\n", info_blk);
    printf("\n");
    printf("%10s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<40;i++) printf("-");
    printf("\n");

    int   *info    = (int *)malloc(info_blk * sizeof(int));
    int   *coded   = (int *)malloc(coded_sz * sizeof(int));
    int   *txbits  = (int *)malloc(pad      * sizeof(int));
    int   *rxbits  = (int *)malloc(info_blk * sizeof(int));
    cx_t  *modsyms = (cx_t *)malloc((pad/bps)    * sizeof(cx_t));
    cx_t  *freq    = (cx_t *)malloc(cfg->nfft     * sizeof(cx_t));
    cx_t  *tx_sig  = (cx_t *)malloc((cfg->nfft+cfg->cpLengthNormal) * sizeof(cx_t));
    cx_t  *rx_sig  = (cx_t *)malloc((cfg->nfft+cfg->cpLengthNormal) * sizeof(cx_t));
    cx_t  *rx_freq = (cx_t *)malloc(cfg->nfft     * sizeof(cx_t));
    cx_t  *rx_mod  = (cx_t *)malloc((pad/bps)     * sizeof(cx_t));
    double *all_llr= (double *)malloc(pad          * sizeof(double));
    double *llr    = (double *)malloc(coded_sz     * sizeof(double));

    AWGNChannel ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd; snr+=cfg->snrStep) {
        awgn_init(&ch, snr);
        int tot_bits=0, tot_err=0, tot_blk=0, blk_err=0;
        for (int sym=0; sym<cfg->numOfdmSymbols; sym++) {
            gen_random_bits(info, info_blk);
            if (use_ldpc)       ldpc_encode(&ldpc, info, coded);
            else if (use_polar) polar_encode(&polar, info, coded);
            else                memcpy(coded, info, info_blk*sizeof(int));

            memcpy(txbits, coded, coded_sz*sizeof(int));
            for (int i=coded_sz;i<pad;i++) txbits[i]=0;
            int ns = pad/bps;
            qam_modulate(txbits, pad, cfg->modulation, modsyms);

            for (int k=0;k<cfg->nfft;k++) freq[k] = CX_ZERO;
            int cp = ns < cfg->nfft ? ns : cfg->nfft;
            for (int k=0;k<cp;k++) freq[k] = modsyms[k];
            ofdm_modulate(&ofdm, freq, tx_sig);
            awgn_add_noise(&ch, tx_sig, cfg->nfft+cfg->cpLengthNormal,
                           cfg->nfft, rx_sig);
            ofdm_demodulate(&ofdm, rx_sig, rx_freq);
            for (int k=0;k<ns;k++) rx_mod[k] = rx_freq[k];

            if (use_code) {
                double nv = 1.0/pow(10.0, snr/10.0);
                qam_demap_llr(rx_mod, ns, cfg->modulation, nv, all_llr);
                memcpy(llr, all_llr, coded_sz*sizeof(double));
                if (use_ldpc) ldpc_decode(&ldpc, llr, 20, rxbits);
                else          polar_decode(&polar, llr, rxbits);
            } else {
                int *tmp = (int *)malloc(ns*bps*sizeof(int));
                qam_demodulate(rx_mod, ns, cfg->modulation, tmp);
                memcpy(rxbits, tmp, info_blk*sizeof(int));
                free(tmp);
            }

            int be=0;
            for (int i=0;i<info_blk;i++) if (info[i]!=rxbits[i]) be++;
            tot_err  += be; tot_bits += info_blk; tot_blk++;
            if (be>0) blk_err++;
        }
        double ber  = (double)tot_err  / tot_bits;
        double bler = (double)blk_err  / tot_blk;
        printf("%10.4f%15.4e%15.4f\n", snr, ber, bler);
    }
    printf("\nSimulation complete.\n");

    ldpc_free(&ldpc); polar_free(&polar);
    free(info); free(coded); free(txbits); free(rxbits);
    free(modsyms); free(freq); free(tx_sig); free(rx_sig);
    free(rx_freq); free(rx_mod); free(all_llr); free(llr);
}

int main(int argc, char *argv[]) {
    const char *cfg_file = "config/sim_config.txt";
    if (argc > 1) cfg_file = argv[1];

    ConfigParser parser;
    config_parser_load(&parser, cfg_file);
    L1Config cfg = config_parser_get(&parser);

    printf("=== 5G PHY Link Level Simulator (C) ===\n");
    config_parser_print(&parser);
    printf("\n");

    if      (strcmp(cfg.physicalChannel,"PBCH"  )==0) run_pbch_simulation(&cfg);
    else if (strcmp(cfg.physicalChannel,"PDCCH" )==0) run_pdcch_simulation(&cfg);
    else if (strcmp(cfg.physicalChannel,"PDSCH" )==0) {
        if (!cfg.useDmrs) {
            run_pdsch_simulation(&cfg);
        } else if (cfg.harqEnable) {
            if (strcmp(cfg.mimoMode,"SM_2X2")==0 && strcmp(cfg.channelModel,"TDL")==0)
                run_pdsch_sm2x2_tdl_harq_simulation(&cfg);
            else if (strcmp(cfg.mimoMode,"SIMO_MRC")==0 && strcmp(cfg.channelModel,"TDL")==0)
                run_pdsch_simo_mrc_tdl_harq_simulation(&cfg);
            else
                run_pdsch_harq_simulation(&cfg);
        } else if (strcmp(cfg.mimoMode,"SIMO_MRC")==0) {
            if (strcmp(cfg.channelModel,"TDL")==0) run_pdsch_simo_mrc_tdl_simulation(&cfg);
            else                                    run_pdsch_simo_mrc_simulation(&cfg);
        } else if (strcmp(cfg.mimoMode,"SM_2X2")==0) {
            if (strcmp(cfg.channelModel,"TDL")==0) run_pdsch_sm2x2_tdl_simulation(&cfg);
            else                                    run_pdsch_sm2x2_simulation(&cfg);
        } else if (strcmp(cfg.mimoMode,"SM_4X4")==0) {
            run_pdsch_sm4x4_simulation(&cfg);
        } else if (strcmp(cfg.channelModel,"TDL")==0) {
            run_pdsch_tdl_simulation(&cfg);
        } else {
            run_pdsch_dmrs_simulation(&cfg);
        }
    }
    else if (strcmp(cfg.physicalChannel,"CSIRS" )==0) run_csirs_simulation(&cfg);
    else if (strcmp(cfg.physicalChannel,"SRS"   )==0) run_srs_simulation(&cfg);
    else if (strcmp(cfg.physicalChannel,"PUSCH" )==0) {
        if (strcmp(cfg.channelModel,"TDL")==0) {
            if      (cfg.puschTurboEnable) run_pusch_tdl_turbo_simulation(&cfg);
            else if (cfg.puschDfeEnable)   run_pusch_tdl_dfe_simulation(&cfg);
            else                           run_pusch_tdl_simulation(&cfg);
        } else {
            run_pusch_simulation(&cfg);
        }
    }
    else if (strcmp(cfg.physicalChannel,"PUCCH" )==0) {
        int tdl = (strcmp(cfg.channelModel,"TDL")==0);
        if      (cfg.pucchFormat==0) run_pucch_format0_simulation(&cfg);
        else if (cfg.pucchFormat==1) {
            if (tdl && cfg.harqEnable)  run_pucch_format1_tdl_harq_simulation(&cfg);
            else if (tdl)               run_pucch_format1_tdl_simulation(&cfg);
            else                        run_pucch_format1_simulation(&cfg);
        } else if (cfg.pucchFormat==2) run_pucch_format2_simulation(&cfg);
        else {
            if (tdl && cfg.harqEnable)  run_pucch_format3_tdl_harq_simulation(&cfg);
            else if (tdl)               run_pucch_format3_tdl_simulation(&cfg);
            else                        run_pucch_format3_simulation(&cfg);
        }
    }
    else if (strcmp(cfg.physicalChannel,"PRACH" )==0) {
        if (strcmp(cfg.channelModel,"TDL")==0) run_prach_tdl_simulation(&cfg);
        else                                    run_prach_simulation(&cfg);
    }
    else if (strcmp(cfg.physicalChannel,"BER"   )==0) run_ber_sim(&cfg);
    else                                               run_legacy_sim(&cfg);

    return 0;
}
