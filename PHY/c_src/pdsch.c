#include "pdsch.h"
#include "crc.h"
#include "mcs_table.h"
#include "ldpc.h"
#include "modulation.h"
#include "channel.h"
#include "dmrs.h"
#include "channel_estimation.h"
#include "utils.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void run_pdsch_simulation(const L1Config *cfg) {
    MCSTableType tbl = (strcmp(cfg->mcsTableType,"TABLE2")==0) ? MCS_TABLE2 : MCS_TABLE1;
    MCSEntry mcs  = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr     = get_code_rate(&mcs);
    int    bps    = mcs.modulationOrder;
    int    crc_bits = 24;

    int A = cfg->tbSize > 0 ? cfg->tbSize : 1000;
    if (A > 8424) A = 8424;
    int K = A + crc_bits;

    LDPCCodec ldpc;
    ldpc_init(&ldpc, K, cr);
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);

    printf("=== PDSCH Simulation ===\n");
    printf("MCS Index: %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation: %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate: %.4f (R x 1024 = %.0f)\n", cr, mcs.targetCodeRate);
    printf("TB Size: %d bits\n", A);
    printf("CRC: CRC-24A (%d bits)\n", crc_bits);
    printf("LDPC Coded Size: %d bits\n", acsz);
    printf("Spectral Efficiency: %.3f bits/RE\n", mcs.spectralEff);
    printf("Trials per SNR: %d\n\n", cfg->numTrials);

    int *tb      = (int *)malloc(A     * sizeof(int));
    int *tb_crc  = (int *)malloc(K     * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int *txbits  = (int *)malloc(padsz * sizeof(int));
    cx_t *syms   = (cx_t *)malloc((padsz/bps) * sizeof(cx_t));
    cx_t *rxsyms = (cx_t *)malloc((padsz/bps) * sizeof(cx_t));
    double *allllr = (double *)malloc(padsz   * sizeof(double));
    double *llr    = (double *)malloc(acsz    * sizeof(double));
    int    *decoded = (int *)malloc(K          * sizeof(int));

    int   dump_en   = cfg->iqDumpEnable;
    FILE *iq_file   = NULL;
    int   dump_left = cfg->iqDumpTrials > 0 ? cfg->iqDumpTrials : cfg->numTrials;

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<42;i++) printf("-"); printf("\n");

    AWGNChannel ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+0.001; snr+=cfg->snrStep) {
        awgn_init(&ch, snr);
        int total_err=0, total_bits=0, blk_err=0;
        int dump_this = dump_en && fabs(snr - cfg->iqDumpSnr) < 0.001;
        if (dump_this && !iq_file) {
            iq_file = fopen(cfg->iqDumpFile, "w");
            if (iq_file) {
                fprintf(iq_file, "# MOD=%s SNR=%.1f\n", mcs.modulation, snr);
                fprintf(iq_file, "# tx_real  tx_imag  rx_real  rx_imag\n");
            }
            dump_left = cfg->iqDumpTrials > 0 ? cfg->iqDumpTrials : cfg->numTrials;
        }

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(tb, A);
            attach_crc(tb, A, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded);
            memcpy(txbits, coded, acsz * sizeof(int));
            for (int i=acsz; i<padsz; i++) txbits[i] = 0;
            int nsym = padsz / bps;
            qam_modulate(txbits, padsz, mcs.modulation, syms);

            if (strcmp(cfg->channelModel,"NONE")==0) {
                memcpy(rxsyms, syms, nsym * sizeof(cx_t));
            } else {
                awgn_add_noise(&ch, syms, nsym, 0, rxsyms);
            }

            if (dump_this && iq_file && dump_left > 0) {
                for (int s=0; s<nsym; s++)
                    fprintf(iq_file, "%.6f %.6f %.6f %.6f\n",
                            creal(syms[s]), cimag(syms[s]),
                            creal(rxsyms[s]), cimag(rxsyms[s]));
                dump_left--;
            }

            double nv = (strcmp(cfg->channelModel,"NONE")==0)
                        ? 1e-10 : 1.0 / pow(10.0, snr/10.0);
            qam_demap_llr(rxsyms, nsym, mcs.modulation, nv, allllr);
            memcpy(llr, allllr, acsz * sizeof(double));
            ldpc_decode(&ldpc, llr, 25, decoded);

            int crc_ok = check_crc(decoded, K, CRC24A);
            int be = 0;
            int ml = (A < ldpc.info_size - crc_bits) ? A : ldpc.info_size - crc_bits;
            if (ml < 0) ml = 0;
            for (int i=0; i<ml; i++) if (tb[i]!=decoded[i]) be++;
            total_err  += be;
            total_bits += A;
            if (!crc_ok || be > 0) blk_err++;
        }

        if (dump_this && iq_file) {
            fclose(iq_file); iq_file = NULL;
            printf("IQ dump written to: %s\n", cfg->iqDumpFile);
        }
        double ber  = total_bits > 0 ? (double)total_err  / total_bits : 0.0;
        double bler = (double)blk_err / cfg->numTrials;
        printf("%12.1f%15.4e%15.4f\n", snr, ber, bler);
    }
    printf("\nPDSCH simulation complete.\n");

    ldpc_free(&ldpc);
    free(tb); free(tb_crc); free(coded); free(txbits);
    free(syms); free(rxsyms); free(allllr); free(llr); free(decoded);
}

void run_pdsch_dmrs_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = (strcmp(cfg->mcsTableType,"TABLE2")==0) ? MCS_TABLE2 : MCS_TABLE1;
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr    = get_code_rate(&mcs);
    int    bps   = mcs.modulationOrder;
    int    crc_bits = 24;

    const uint32_t c_init = 0x12345678u;
    int num_pilots = 6 * num_rb;
    int num_data   = 6 * num_rb;
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);

    cx_t *dmrs_sym = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    dmrs_sequence(c_init, num_pilots, dmrs_sym);

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);

    printf("=== PDSCH + DMRS Channel Estimation ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Active SC    : %d\n", active);
    printf("Pilot REs    : %d (DMRS Type 1, 50%% overhead)\n", num_pilots);
    printf("Data REs     : %d\n", num_data);
    printf("TB Size      : %d bits\n", tbsz);
    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    int use_ray  = (strcmp(cfg->channelModel,"AWGN")!=0);
    printf("Channel      : %s\n", use_ray ? "Flat Rayleigh fading" : "AWGN (H=1)");
    printf("Equalizer    : %s with LS channel estimation\n", use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb      = (int *)malloc(tbsz  * sizeof(int));
    int *tb_crc  = (int *)malloc(K     * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int *txbits  = (int *)malloc(padsz * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc((padsz/bps) * sizeof(cx_t));
    cx_t *tx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_pilots  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots   = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_full     = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *h_data     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *eq_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    double *allllr   = (double *)malloc(padsz        * sizeof(double));
    double *llr      = (double *)malloc(acsz         * sizeof(double));
    int    *decoded  = (int *)malloc(K               * sizeof(int));

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<42;i++) printf("-"); printf("\n");

    AWGNChannel awgn_ch;
    FlatFadingChannel ray_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        awgn_init(&awgn_ch, snr);
        flat_fading_init(&ray_ch, snr);
        double snrlin = pow(10.0, snr/10.0);
        double N0     = 1.0 / snrlin;
        int total_err=0, total_bits=0, blk_err=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            /* TX */
            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded);
            memcpy(txbits, coded, acsz * sizeof(int));
            for (int i=acsz; i<padsz; i++) txbits[i] = 0;
            int nsym = padsz / bps;
            qam_modulate(txbits, padsz, mcs.modulation, data_syms);

            /* build resource grid */
            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            int nd = nsym < num_data ? nsym : num_data;
            for (int d=0;d<nd;d++) tx_grid[data_pos[d]] = data_syms[d];

            /* channel */
            if (use_ray)
                flat_fading_apply(&ray_ch, tx_grid, active, rx_grid, NULL);
            else
                awgn_add_noise(&awgn_ch, tx_grid, active, 0, rx_grid);

            /* LS channel estimation */
            for (int p=0;p<num_pilots;p++) rx_pilots[p] = rx_grid[pilot_pos[p]];
            ls_estimate(rx_pilots, dmrs_sym, num_pilots, h_pilots);
            interpolate_channel(h_pilots, num_pilots, pilot_pos, active, h_full);

            /* extract data */
            for (int d=0;d<num_data;d++) {
                rx_data[d] = rx_grid[data_pos[d]];
                h_data[d]  = h_full[data_pos[d]];
            }

            /* equalize */
            if (use_mmse) {
                mmse_equalize(rx_data, h_data, num_data, N0, eq_data, NULL);
                qam_demap_llr_mmse(eq_data, nd, mcs.modulation, h_data, N0, allllr);
            } else {
                zf_equalize(rx_data, h_data, num_data, eq_data);
                double mhp = 0.0;
                for (int d=0;d<num_data;d++) mhp += CX_NORM(h_data[d]);
                mhp /= num_data;
                double env = (mhp > 1e-10) ? N0/mhp : N0;
                qam_demap_llr(eq_data, nd, mcs.modulation, env, allllr);
            }
            int llr_len = acsz < nd*bps ? acsz : nd*bps;
            memcpy(llr, allllr, llr_len * sizeof(double));
            for (int i=llr_len; i<acsz; i++) llr[i] = 0.0;

            ldpc_decode(&ldpc, llr, 25, decoded);
            int crc_ok = check_crc(decoded, K, CRC24A);
            int be=0, ml=tbsz < ldpc.info_size-crc_bits ? tbsz : ldpc.info_size-crc_bits;
            if (ml<0) ml=0;
            for (int i=0;i<ml;i++) if (tb[i]!=decoded[i]) be++;
            total_err  += be;
            total_bits += tbsz;
            if (!crc_ok || be > 0) blk_err++;
        }
        double ber  = total_bits > 0 ? (double)total_err/total_bits : 0.0;
        double bler = (double)blk_err / cfg->numTrials;
        printf("%12.1f%15.4e%15.4f\n", snr, ber, bler);
    }
    printf("\nPDSCH+DMRS simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(txbits);
    free(data_syms); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(allllr); free(llr); free(decoded);
}
