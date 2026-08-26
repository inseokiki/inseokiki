/* ================================================================
 *  pdsch.c
 *  PDSCH simulation loop (DMRS, MIMO, HARQ, TDL combinations)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "pdsch.h"
#include "crc.h"
#include "mcs_table.h"
#include "ldpc.h"
#include "modulation.h"
#include "channel.h"
#include "dmrs.h"
#include "channel_estimation.h"
#include "mimo.h"
#include "rate_matching.h"
#include "tdl.h"
#include "codebook.h"
#include "utils.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void run_pdsch_simulation(const L1Config *cfg) {
    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
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
    for (int i=0;i<42;i++) printf("-");
    printf("\n");

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

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
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
    for (int i=0;i<42;i++) printf("-");
    printf("\n");

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

/* 1x2 SIMO receive diversity: 1 Tx layer, 2 Rx antennas, MRC combining.
   Same DMRS/LDPC/CRC pipeline as run_pdsch_dmrs_simulation(); only the
   channel + equalizer stage is doubled across two independent Rx chains. */
void run_pdsch_simo_mrc_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
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

    printf("=== PDSCH 1x2 SIMO (MRC receive diversity) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Rx Antennas  : 2\n");
    printf("TB Size      : %d bits\n", tbsz);
    printf("Combiner     : MRC with LS channel estimation (per Rx branch)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb      = (int *)malloc(tbsz  * sizeof(int));
    int *tb_crc  = (int *)malloc(K     * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int *txbits  = (int *)malloc(padsz * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc((padsz/bps) * sizeof(cx_t));
    cx_t *tx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid0   = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid1   = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_pilots0 = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *rx_pilots1 = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots0  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots1  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_full0    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *h_full1    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *eq_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    double *allllr   = (double *)malloc(padsz        * sizeof(double));
    double *llr      = (double *)malloc(acsz         * sizeof(double));
    int    *decoded  = (int *)malloc(K               * sizeof(int));

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<42;i++) printf("-");
    printf("\n");

    MIMOChannel mch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        mimo_channel_init(&mch, snr);
        double N0 = 1.0 / pow(10.0, snr/10.0);
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

            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            int nd = nsym < num_data ? nsym : num_data;
            for (int d=0;d<nd;d++) tx_grid[data_pos[d]] = data_syms[d];

            /* channel: one 1x2 Rayleigh draw per trial, applied RE-by-RE */
            cx_t h[2];
            mimo_channel_draw_1x2(h);
            for (int k=0;k<active;k++) {
                cx_t y[2];
                mimo_channel_apply_1x2(&mch, h, tx_grid[k], y);
                rx_grid0[k] = y[0]; rx_grid1[k] = y[1];
            }

            /* LS channel estimation per Rx branch */
            for (int p=0;p<num_pilots;p++) {
                rx_pilots0[p] = rx_grid0[pilot_pos[p]];
                rx_pilots1[p] = rx_grid1[pilot_pos[p]];
            }
            ls_estimate(rx_pilots0, dmrs_sym, num_pilots, h_pilots0);
            ls_estimate(rx_pilots1, dmrs_sym, num_pilots, h_pilots1);
            interpolate_channel(h_pilots0, num_pilots, pilot_pos, active, h_full0);
            interpolate_channel(h_pilots1, num_pilots, pilot_pos, active, h_full1);

            /* MRC combine at data REs */
            double nv_sum = 0.0;
            for (int d=0;d<num_data;d++) {
                cx_t hd[2]  = { h_full0[data_pos[d]], h_full1[data_pos[d]] };
                cx_t yd[2]  = { rx_grid0[data_pos[d]], rx_grid1[data_pos[d]] };
                double nv;
                mrc_combine(hd, yd, N0, &eq_data[d], &nv);
                nv_sum += nv;
            }
            double env = nv_sum / num_data;

            qam_demap_llr(eq_data, nd, mcs.modulation, env, allllr);
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
    printf("\nPDSCH 1x2 SIMO-MRC simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(txbits);
    free(data_syms); free(tx_grid); free(rx_grid0); free(rx_grid1);
    free(rx_pilots0); free(rx_pilots1); free(h_pilots0); free(h_pilots1);
    free(h_full0); free(h_full1); free(eq_data);
    free(allllr); free(llr); free(decoded);
}

/* 1x2 SIMO receive diversity over frequency-selective TDL: same as
   run_pdsch_simo_mrc_simulation() except each Rx branch gets its own
   independent TDL tap-set, so the per-branch channel varies across REs
   instead of being block-flat. LS estimation / interpolation / MRC combine
   stages are unchanged -- they already operate per-RE and don't care
   whether the true channel is flat or frequency-selective. */
void run_pdsch_simo_mrc_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
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

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PDSCH 1x2 SIMO (MRC), TDL Frequency-Selective Fading ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Rx Antennas  : 2\n");
    printf("TB Size      : %d bits\n", tbsz);
    printf("Channel      : TDL per Rx branch (independent tap-sets, DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Combiner     : MRC with LS channel estimation (per Rx branch)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb      = (int *)malloc(tbsz  * sizeof(int));
    int *tb_crc  = (int *)malloc(K     * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int *txbits  = (int *)malloc(padsz * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc((padsz/bps) * sizeof(cx_t));
    cx_t *tx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid0   = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid1   = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_pilots0 = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *rx_pilots1 = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots0  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots1  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_full0    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *h_full1    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *eq_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    double *allllr   = (double *)malloc(padsz        * sizeof(double));
    double *llr      = (double *)malloc(acsz         * sizeof(double));
    int    *decoded  = (int *)malloc(K               * sizeof(int));
    cx_t   *taps0    = (cx_t *)malloc(TDL_MAX_TAPS   * sizeof(cx_t));
    cx_t   *taps1    = (cx_t *)malloc(TDL_MAX_TAPS   * sizeof(cx_t));

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<42;i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double snrlin = pow(10.0, snr/10.0);
        double N0     = 1.0 / snrlin;
        double sigma  = sqrt(1.0 / (2.0 * snrlin));
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

            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            int nd = nsym < num_data ? nsym : num_data;
            for (int d=0;d<nd;d++) tx_grid[data_pos[d]] = data_syms[d];

            /* channel: one independent TDL tap-set per Rx branch, RE-by-RE
               frequency response (models spatially independent multipath) */
            tdl_draw(&tdl_ch, taps0);
            tdl_draw(&tdl_ch, taps1);
            for (int k=0;k<active;k++) {
                cx_t h0 = tdl_freq_response(&tdl_ch, taps0, k);
                cx_t h1 = tdl_freq_response(&tdl_ch, taps1, k);
                rx_grid0[k] = h0 * tx_grid[k] + CX_MAKE(randn()*sigma, randn()*sigma);
                rx_grid1[k] = h1 * tx_grid[k] + CX_MAKE(randn()*sigma, randn()*sigma);
            }

            /* LS channel estimation per Rx branch */
            for (int p=0;p<num_pilots;p++) {
                rx_pilots0[p] = rx_grid0[pilot_pos[p]];
                rx_pilots1[p] = rx_grid1[pilot_pos[p]];
            }
            ls_estimate(rx_pilots0, dmrs_sym, num_pilots, h_pilots0);
            ls_estimate(rx_pilots1, dmrs_sym, num_pilots, h_pilots1);
            interpolate_channel(h_pilots0, num_pilots, pilot_pos, active, h_full0);
            interpolate_channel(h_pilots1, num_pilots, pilot_pos, active, h_full1);

            /* MRC combine at data REs */
            double nv_sum = 0.0;
            for (int d=0;d<num_data;d++) {
                cx_t hd[2]  = { h_full0[data_pos[d]], h_full1[data_pos[d]] };
                cx_t yd[2]  = { rx_grid0[data_pos[d]], rx_grid1[data_pos[d]] };
                double nv;
                mrc_combine(hd, yd, N0, &eq_data[d], &nv);
                nv_sum += nv;
            }
            double env = nv_sum / num_data;

            qam_demap_llr(eq_data, nd, mcs.modulation, env, allllr);
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
    printf("\nPDSCH 1x2 SIMO-MRC + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(txbits);
    free(data_syms); free(tx_grid); free(rx_grid0); free(rx_grid1);
    free(rx_pilots0); free(rx_pilots1); free(h_pilots0); free(h_pilots1);
    free(h_full0); free(h_full1); free(eq_data);
    free(allllr); free(llr); free(decoded); free(taps0); free(taps1);
}

/* 1x2 SIMO receive diversity over frequency-selective TDL, with HARQ
   circular-buffer IR/Chase combining -- combination of
   run_pdsch_simo_mrc_tdl_simulation() (MRC + per-RE TDL channel estimation,
   one Tx layer / 2 Rx branches) and run_pdsch_harq_simulation()'s mother
   codeword + circular-buffer soft combining across retransmission attempts.
   Unlike the SM_2X2+TDL+HARQ case there's only one codeword (single Tx
   layer), so the retransmission loop is a direct single-layer analogue of
   run_pdsch_harq_simulation() with the channel-generation block swapped for
   two independent per-attempt TDL tap-sets (one per Rx branch), redrawn
   every attempt to model time diversity across HARQ rounds. */
void run_pdsch_simo_mrc_tdl_harq_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
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

    int E = num_data * bps;   /* bits sent per (re)transmission occasion */
    int max_dbits = E;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, HARQ_MOTHER_RATE);   /* mother code */
    int ncb = ldpc.coded_size;

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    if (is_chase) rvseq[0] = 0;

    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PDSCH 1x2 SIMO (MRC) HARQ (Circular Buffer %s), TDL Frequency-Selective Fading ===\n",
           is_chase ? "Chase" : "IR");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d)\n", HARQ_MOTHER_RATE, ncb);
    printf("Num RB       : %d\n", num_rb);
    printf("Rx Antennas  : 2\n");
    printf("TB Size      : %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    printf("Channel      : TDL per Rx branch, redrawn every attempt (DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Combiner     : MRC with per-RE LS channel estimation + interpolation\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb        = (int *)malloc(tbsz * sizeof(int));
    int *tb_crc    = (int *)malloc(K    * sizeof(int));
    int *coded_full= (int *)malloc(ncb  * sizeof(int));
    int *selbits   = (int *)malloc(E    * sizeof(int));
    cx_t *data_syms = (cx_t *)malloc(num_data * sizeof(cx_t));
    cx_t *tx_grid   = (cx_t *)malloc(active     * sizeof(cx_t));
    cx_t *rx_grid0  = (cx_t *)malloc(active     * sizeof(cx_t));
    cx_t *rx_grid1  = (cx_t *)malloc(active     * sizeof(cx_t));
    cx_t *rx_pilots0= (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *rx_pilots1= (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_pilots0 = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_pilots1 = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_full0   = (cx_t *)malloc(active     * sizeof(cx_t));
    cx_t *h_full1   = (cx_t *)malloc(active     * sizeof(cx_t));
    cx_t *eq_data   = (cx_t *)malloc(num_data   * sizeof(cx_t));
    double *allllr  = (double *)malloc(E   * sizeof(double));
    double *soft_buf= (double *)malloc(ncb * sizeof(double));
    int    *decoded = (int *)malloc(K      * sizeof(int));
    cx_t   *taps0   = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));
    cx_t   *taps1   = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%10s%14s%14s%14s%12s\n", "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i=0;i<64;i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr/10.0);
        double sigma = sqrt(N0 / 2.0);
        int total_err=0, total_bits=0, blk_err_final=0;
        int total_err_1st=0, blk_err_1st=0;
        long long total_attempts=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded_full);

            for (int i=0;i<ncb;i++) soft_buf[i] = 0.0;

            int crc_ok = 0, be = 0, attempts = 0;
            int be_1st = 0, crc_ok_1st = 0;

            for (int attempt=0; attempt<max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];
                rate_match_select(coded_full, ncb, rv, E, selbits);
                qam_modulate(selbits, E, mcs.modulation, data_syms);

                for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
                for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
                for (int d=0;d<num_data;d++)   tx_grid[data_pos[d]]  = data_syms[d];

                /* channel: fresh independent TDL tap-set per Rx branch,
                   every attempt (time diversity across HARQ rounds) */
                tdl_draw(&tdl_ch, taps0);
                tdl_draw(&tdl_ch, taps1);
                for (int k=0;k<active;k++) {
                    cx_t h0 = tdl_freq_response(&tdl_ch, taps0, k);
                    cx_t h1 = tdl_freq_response(&tdl_ch, taps1, k);
                    rx_grid0[k] = h0 * tx_grid[k] + CX_MAKE(randn()*sigma, randn()*sigma);
                    rx_grid1[k] = h1 * tx_grid[k] + CX_MAKE(randn()*sigma, randn()*sigma);
                }

                for (int p=0;p<num_pilots;p++) {
                    rx_pilots0[p] = rx_grid0[pilot_pos[p]];
                    rx_pilots1[p] = rx_grid1[pilot_pos[p]];
                }
                ls_estimate(rx_pilots0, dmrs_sym, num_pilots, h_pilots0);
                ls_estimate(rx_pilots1, dmrs_sym, num_pilots, h_pilots1);
                interpolate_channel(h_pilots0, num_pilots, pilot_pos, active, h_full0);
                interpolate_channel(h_pilots1, num_pilots, pilot_pos, active, h_full1);

                double nv_sum = 0.0;
                for (int d=0;d<num_data;d++) {
                    cx_t hd[2] = { h_full0[data_pos[d]], h_full1[data_pos[d]] };
                    cx_t yd[2] = { rx_grid0[data_pos[d]], rx_grid1[data_pos[d]] };
                    double nv;
                    mrc_combine(hd, yd, N0, &eq_data[d], &nv);
                    nv_sum += nv;
                }
                double env = nv_sum / num_data;

                qam_demap_llr(eq_data, num_data, mcs.modulation, env, allllr);
                rate_match_combine(soft_buf, ncb, rv, E, allllr);
                ldpc_decode(&ldpc, soft_buf, 25, decoded);
                crc_ok = check_crc(decoded, K, CRC24A);

                be = 0;
                int ml = tbsz < ldpc.info_size-crc_bits ? tbsz : ldpc.info_size-crc_bits;
                if (ml<0) ml=0;
                for (int i=0;i<ml;i++) if (tb[i]!=decoded[i]) be++;

                if (attempt==0) { be_1st = be; crc_ok_1st = crc_ok; }
                attempts = attempt+1;
                if (crc_ok) break;
            }

            total_err  += be;
            total_bits += tbsz;
            if (!crc_ok || be > 0) blk_err_final++;
            total_err_1st += be_1st;
            if (!crc_ok_1st || be_1st > 0) blk_err_1st++;
            total_attempts += attempts;
        }
        double ber       = total_bits > 0 ? (double)total_err/total_bits : 0.0;
        double bler_1st  = (double)blk_err_1st   / cfg->numTrials;
        double bler_final= (double)blk_err_final / cfg->numTrials;
        double avg_tx    = (double)total_attempts / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr, ber, bler_1st, bler_final, avg_tx);
    }
    printf("\nPDSCH 1x2 SIMO-MRC + TDL + HARQ simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded_full); free(selbits);
    free(data_syms); free(tx_grid); free(rx_grid0); free(rx_grid1);
    free(rx_pilots0); free(rx_pilots1); free(h_pilots0); free(h_pilots1);
    free(h_full0); free(h_full1); free(eq_data);
    free(allllr); free(soft_buf); free(decoded); free(taps0); free(taps1);
}

/* 2x2 SU-MIMO spatial multiplexing: 2 independent Tx layers (codewords),
   2 Rx antennas, ZF or MMSE detection (EQUALIZER config). DMRS pilot REs
   are split between the two layers so each layer gets orthogonal pilots;
   data REs are shared (both layers transmitted simultaneously). The
   channel is drawn once per trial (block-flat) and pilot LS ratios are
   averaged over all of a layer's pilot REs to reduce estimation noise. */
void run_pdsch_sm2x2_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr    = get_code_rate(&mcs);
    int    bps   = mcs.modulationOrder;
    int    crc_bits = 24;

    int num_pilots = 6 * num_rb;
    int num_data   = 6 * num_rb;
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);

    /* split pilot REs between the two layers (orthogonal FDM pilots) */
    int nppl = num_pilots / 2;
    int *pilotA = (int *)malloc(nppl * sizeof(int));
    int *pilotB = (int *)malloc(nppl * sizeof(int));
    for (int i=0;i<nppl;i++) { pilotA[i] = pilot_pos[2*i]; pilotB[i] = pilot_pos[2*i+1]; }
    cx_t *dmrsA = (cx_t *)malloc(nppl * sizeof(cx_t));
    cx_t *dmrsB = (cx_t *)malloc(nppl * sizeof(cx_t));
    dmrs_sequence(0x12345678u, nppl, dmrsA);
    dmrs_sequence(0x1abcdef2u, nppl, dmrsB);

    int max_dbits = num_data * bps;   /* per layer; both layers share the same REs */
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);   /* shared by both layers (stateless, const-only use) */
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);

    printf("=== PDSCH 2x2 Spatial Multiplexing (SM_2X2) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Layers       : 2 (independent codewords, shared data REs)\n");
    printf("Rx Antennas  : 2\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Detector     : %s with LS channel estimation (averaged, block-flat)\n",
           use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tbA=(int*)malloc(tbsz*sizeof(int)), *tbB=(int*)malloc(tbsz*sizeof(int));
    int *tb_crcA=(int*)malloc(K*sizeof(int)), *tb_crcB=(int*)malloc(K*sizeof(int));
    int *codedA=(int*)malloc(acsz*sizeof(int)), *codedB=(int*)malloc(acsz*sizeof(int));
    int *txbitsA=(int*)malloc(padsz*sizeof(int)), *txbitsB=(int*)malloc(padsz*sizeof(int));
    cx_t *symsA=(cx_t*)malloc((padsz/bps)*sizeof(cx_t)), *symsB=(cx_t*)malloc((padsz/bps)*sizeof(cx_t));
    cx_t *tx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *tx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *rx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *rx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *x_hatA=(cx_t*)malloc(num_data*sizeof(cx_t)), *x_hatB=(cx_t*)malloc(num_data*sizeof(cx_t));
    double *allllrA=(double*)malloc(padsz*sizeof(double)), *allllrB=(double*)malloc(padsz*sizeof(double));
    double *llrA=(double*)malloc(acsz*sizeof(double)), *llrB=(double*)malloc(acsz*sizeof(double));
    int *decodedA=(int*)malloc(K*sizeof(int)), *decodedB=(int*)malloc(K*sizeof(int));

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<42;i++) printf("-");
    printf("\n");

    MIMOChannel mch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        mimo_channel_init(&mch, snr);
        double N0 = 1.0 / pow(10.0, snr/10.0);
        int total_err=0, total_bits=0, blk_err=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            /* TX: 2 independent codewords */
            gen_random_bits(tbA, tbsz); gen_random_bits(tbB, tbsz);
            attach_crc(tbA, tbsz, CRC24A, tb_crcA);
            attach_crc(tbB, tbsz, CRC24A, tb_crcB);
            ldpc_encode(&ldpc, tb_crcA, codedA);
            ldpc_encode(&ldpc, tb_crcB, codedB);
            memcpy(txbitsA, codedA, acsz*sizeof(int));
            memcpy(txbitsB, codedB, acsz*sizeof(int));
            for (int i=acsz;i<padsz;i++) { txbitsA[i]=0; txbitsB[i]=0; }
            int nsym = padsz / bps;
            qam_modulate(txbitsA, padsz, mcs.modulation, symsA);
            qam_modulate(txbitsB, padsz, mcs.modulation, symsB);

            /* resource grid: layer-specific pilots, shared data */
            for (int k=0;k<active;k++) { tx_grid0[k]=CX_ZERO; tx_grid1[k]=CX_ZERO; }
            for (int p=0;p<nppl;p++) { tx_grid0[pilotA[p]] = dmrsA[p]; tx_grid1[pilotB[p]] = dmrsB[p]; }
            int nd = nsym < num_data ? nsym : num_data;
            for (int d=0;d<nd;d++) { tx_grid0[data_pos[d]] = symsA[d]; tx_grid1[data_pos[d]] = symsB[d]; }

            /* channel: one 2x2 Rayleigh draw per trial, applied RE-by-RE */
            cx_t h[2][2];
            mimo_channel_draw_2x2(h);
            for (int k=0;k<active;k++) {
                cx_t tx[2] = { tx_grid0[k], tx_grid1[k] };
                cx_t y[2];
                mimo_channel_apply_2x2(&mch, h, tx, y);
                rx_grid0[k] = y[0]; rx_grid1[k] = y[1];
            }

            /* channel estimation: average LS ratio over each layer's pilot REs */
            cx_t h00s=CX_ZERO, h10s=CX_ZERO, h01s=CX_ZERO, h11s=CX_ZERO;
            for (int p=0;p<nppl;p++) {
                h00s += rx_grid0[pilotA[p]] / dmrsA[p];
                h10s += rx_grid1[pilotA[p]] / dmrsA[p];
                h01s += rx_grid0[pilotB[p]] / dmrsB[p];
                h11s += rx_grid1[pilotB[p]] / dmrsB[p];
            }
            cx_t h_hat[2][2] = {
                { h00s / nppl, h01s / nppl },
                { h10s / nppl, h11s / nppl }
            };

            /* detection + demap at data REs */
            double nvA_sum=0.0, nvB_sum=0.0;
            for (int d=0;d<num_data;d++) {
                cx_t y[2] = { rx_grid0[data_pos[d]], rx_grid1[data_pos[d]] };
                cx_t xh[2]; double nv[2];
                if (use_mmse) mimo_mmse_detect(h_hat, y, N0, xh, nv);
                else          mimo_zf_detect  (h_hat, y, N0, xh, nv);
                x_hatA[d] = xh[0]; x_hatB[d] = xh[1];
                nvA_sum += nv[0]; nvB_sum += nv[1];
            }
            double envA = nvA_sum / num_data, envB = nvB_sum / num_data;

            qam_demap_llr(x_hatA, nd, mcs.modulation, envA, allllrA);
            qam_demap_llr(x_hatB, nd, mcs.modulation, envB, allllrB);
            int llr_len = acsz < nd*bps ? acsz : nd*bps;
            memcpy(llrA, allllrA, llr_len*sizeof(double));
            memcpy(llrB, allllrB, llr_len*sizeof(double));
            for (int i=llr_len;i<acsz;i++) { llrA[i]=0.0; llrB[i]=0.0; }

            ldpc_decode(&ldpc, llrA, 25, decodedA);
            ldpc_decode(&ldpc, llrB, 25, decodedB);
            int crcA_ok = check_crc(decodedA, K, CRC24A);
            int crcB_ok = check_crc(decodedB, K, CRC24A);
            int ml = tbsz < ldpc.info_size-crc_bits ? tbsz : ldpc.info_size-crc_bits;
            if (ml<0) ml=0;
            int beA=0, beB=0;
            for (int i=0;i<ml;i++) {
                if (tbA[i]!=decodedA[i]) beA++;
                if (tbB[i]!=decodedB[i]) beB++;
            }
            total_err  += beA + beB;
            total_bits += 2*tbsz;
            if (!crcA_ok || beA>0 || !crcB_ok || beB>0) blk_err++;
        }
        double ber  = total_bits > 0 ? (double)total_err/total_bits : 0.0;
        double bler = (double)blk_err / cfg->numTrials;
        printf("%12.1f%15.4e%15.4f\n", snr, ber, bler);
    }
    printf("\nPDSCH 2x2 SM simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(pilotA); free(pilotB);
    free(dmrsA); free(dmrsB);
    free(tbA); free(tbB); free(tb_crcA); free(tb_crcB);
    free(codedA); free(codedB); free(txbitsA); free(txbitsB);
    free(symsA); free(symsB); free(tx_grid0); free(tx_grid1);
    free(rx_grid0); free(rx_grid1); free(x_hatA); free(x_hatB);
    free(allllrA); free(allllrB); free(llrA); free(llrB);
    free(decodedA); free(decodedB);
}

/* 2x2 SU-MIMO spatial multiplexing over frequency-selective TDL: same as
   run_pdsch_sm2x2_simulation() except each of the 4 Tx-Rx antenna pairs
   gets its own independent TDL tap-set, so the 2x2 channel matrix varies
   across REs. Unlike the flat case, channel estimation can no longer
   average the LS ratio over all pilot REs into one h_hat -- instead
   ls_estimate()+interpolate_channel() run once per antenna pair (4x) to
   build per-RE h00/h01/h10/h11, assembled into h_hat[2][2] at each data
   RE. mimo_zf_detect()/mimo_mmse_detect() are unchanged (already per-RE). */
void run_pdsch_sm2x2_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr    = get_code_rate(&mcs);
    int    bps   = mcs.modulationOrder;
    int    crc_bits = 24;

    int num_pilots = 6 * num_rb;
    int num_data   = 6 * num_rb;
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);

    /* split pilot REs between the two layers (orthogonal FDM pilots) */
    int nppl = num_pilots / 2;
    int *pilotA = (int *)malloc(nppl * sizeof(int));
    int *pilotB = (int *)malloc(nppl * sizeof(int));
    for (int i=0;i<nppl;i++) { pilotA[i] = pilot_pos[2*i]; pilotB[i] = pilot_pos[2*i+1]; }
    cx_t *dmrsA = (cx_t *)malloc(nppl * sizeof(cx_t));
    cx_t *dmrsB = (cx_t *)malloc(nppl * sizeof(cx_t));
    dmrs_sequence(0x12345678u, nppl, dmrsA);
    dmrs_sequence(0x1abcdef2u, nppl, dmrsB);

    int max_dbits = num_data * bps;   /* per layer; both layers share the same REs */
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);   /* shared by both layers (stateless, const-only use) */
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PDSCH 2x2 Spatial Multiplexing (SM_2X2), TDL Frequency-Selective Fading ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Layers       : 2 (independent codewords, shared data REs)\n");
    printf("Rx Antennas  : 2\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Channel      : TDL per Tx-Rx antenna pair (4 independent tap-sets, DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Detector     : %s with per-RE LS channel estimation + interpolation\n",
           use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tbA=(int*)malloc(tbsz*sizeof(int)), *tbB=(int*)malloc(tbsz*sizeof(int));
    int *tb_crcA=(int*)malloc(K*sizeof(int)), *tb_crcB=(int*)malloc(K*sizeof(int));
    int *codedA=(int*)malloc(acsz*sizeof(int)), *codedB=(int*)malloc(acsz*sizeof(int));
    int *txbitsA=(int*)malloc(padsz*sizeof(int)), *txbitsB=(int*)malloc(padsz*sizeof(int));
    cx_t *symsA=(cx_t*)malloc((padsz/bps)*sizeof(cx_t)), *symsB=(cx_t*)malloc((padsz/bps)*sizeof(cx_t));
    cx_t *tx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *tx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *rx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *rx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *x_hatA=(cx_t*)malloc(num_data*sizeof(cx_t)), *x_hatB=(cx_t*)malloc(num_data*sizeof(cx_t));
    double *allllrA=(double*)malloc(padsz*sizeof(double)), *allllrB=(double*)malloc(padsz*sizeof(double));
    double *llrA=(double*)malloc(acsz*sizeof(double)), *llrB=(double*)malloc(acsz*sizeof(double));
    int *decodedA=(int*)malloc(K*sizeof(int)), *decodedB=(int*)malloc(K*sizeof(int));

    /* per antenna-pair pilot LS + interpolated full-band channel */
    cx_t *rx_pilotsA0=(cx_t*)malloc(nppl*sizeof(cx_t)), *rx_pilotsA1=(cx_t*)malloc(nppl*sizeof(cx_t));
    cx_t *rx_pilotsB0=(cx_t*)malloc(nppl*sizeof(cx_t)), *rx_pilotsB1=(cx_t*)malloc(nppl*sizeof(cx_t));
    cx_t *h_pilots00=(cx_t*)malloc(nppl*sizeof(cx_t)), *h_pilots10=(cx_t*)malloc(nppl*sizeof(cx_t));
    cx_t *h_pilots01=(cx_t*)malloc(nppl*sizeof(cx_t)), *h_pilots11=(cx_t*)malloc(nppl*sizeof(cx_t));
    cx_t *h00_full=(cx_t*)malloc(active*sizeof(cx_t)), *h10_full=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *h01_full=(cx_t*)malloc(active*sizeof(cx_t)), *h11_full=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *taps00=(cx_t*)malloc(TDL_MAX_TAPS*sizeof(cx_t)), *taps10=(cx_t*)malloc(TDL_MAX_TAPS*sizeof(cx_t));
    cx_t *taps01=(cx_t*)malloc(TDL_MAX_TAPS*sizeof(cx_t)), *taps11=(cx_t*)malloc(TDL_MAX_TAPS*sizeof(cx_t));

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<42;i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double snrlin = pow(10.0, snr/10.0);
        double N0    = 1.0 / snrlin;
        double sigma = sqrt(1.0 / (2.0 * snrlin));
        int total_err=0, total_bits=0, blk_err=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            /* TX: 2 independent codewords */
            gen_random_bits(tbA, tbsz); gen_random_bits(tbB, tbsz);
            attach_crc(tbA, tbsz, CRC24A, tb_crcA);
            attach_crc(tbB, tbsz, CRC24A, tb_crcB);
            ldpc_encode(&ldpc, tb_crcA, codedA);
            ldpc_encode(&ldpc, tb_crcB, codedB);
            memcpy(txbitsA, codedA, acsz*sizeof(int));
            memcpy(txbitsB, codedB, acsz*sizeof(int));
            for (int i=acsz;i<padsz;i++) { txbitsA[i]=0; txbitsB[i]=0; }
            int nsym = padsz / bps;
            qam_modulate(txbitsA, padsz, mcs.modulation, symsA);
            qam_modulate(txbitsB, padsz, mcs.modulation, symsB);

            /* resource grid: layer-specific pilots, shared data */
            for (int k=0;k<active;k++) { tx_grid0[k]=CX_ZERO; tx_grid1[k]=CX_ZERO; }
            for (int p=0;p<nppl;p++) { tx_grid0[pilotA[p]] = dmrsA[p]; tx_grid1[pilotB[p]] = dmrsB[p]; }
            int nd = nsym < num_data ? nsym : num_data;
            for (int d=0;d<nd;d++) { tx_grid0[data_pos[d]] = symsA[d]; tx_grid1[data_pos[d]] = symsB[d]; }

            /* channel: 4 independent TDL tap-sets (one per Tx-Rx antenna
               pair), RE-by-RE 2x2 frequency response */
            tdl_draw(&tdl_ch, taps00); tdl_draw(&tdl_ch, taps01);
            tdl_draw(&tdl_ch, taps10); tdl_draw(&tdl_ch, taps11);
            for (int k=0;k<active;k++) {
                cx_t h00 = tdl_freq_response(&tdl_ch, taps00, k);
                cx_t h01 = tdl_freq_response(&tdl_ch, taps01, k);
                cx_t h10 = tdl_freq_response(&tdl_ch, taps10, k);
                cx_t h11 = tdl_freq_response(&tdl_ch, taps11, k);
                cx_t s0 = h00 * tx_grid0[k] + h01 * tx_grid1[k];
                cx_t s1 = h10 * tx_grid0[k] + h11 * tx_grid1[k];
                rx_grid0[k] = s0 + CX_MAKE(randn()*sigma, randn()*sigma);
                rx_grid1[k] = s1 + CX_MAKE(randn()*sigma, randn()*sigma);
            }

            /* channel estimation: LS at each layer's pilot REs, then
               interpolate across the full band -- 4 antenna-pair channels
               since H varies per RE (averaging, as in the flat case, would
               throw away the frequency selectivity) */
            for (int p=0;p<nppl;p++) {
                rx_pilotsA0[p] = rx_grid0[pilotA[p]]; rx_pilotsA1[p] = rx_grid1[pilotA[p]];
                rx_pilotsB0[p] = rx_grid0[pilotB[p]]; rx_pilotsB1[p] = rx_grid1[pilotB[p]];
            }
            ls_estimate(rx_pilotsA0, dmrsA, nppl, h_pilots00);
            ls_estimate(rx_pilotsA1, dmrsA, nppl, h_pilots10);
            ls_estimate(rx_pilotsB0, dmrsB, nppl, h_pilots01);
            ls_estimate(rx_pilotsB1, dmrsB, nppl, h_pilots11);
            interpolate_channel(h_pilots00, nppl, pilotA, active, h00_full);
            interpolate_channel(h_pilots10, nppl, pilotA, active, h10_full);
            interpolate_channel(h_pilots01, nppl, pilotB, active, h01_full);
            interpolate_channel(h_pilots11, nppl, pilotB, active, h11_full);

            /* detection + demap at data REs */
            double nvA_sum=0.0, nvB_sum=0.0;
            for (int d=0;d<num_data;d++) {
                int re = data_pos[d];
                cx_t h_hat[2][2] = {
                    { h00_full[re], h01_full[re] },
                    { h10_full[re], h11_full[re] }
                };
                cx_t y[2] = { rx_grid0[re], rx_grid1[re] };
                cx_t xh[2]; double nv[2];
                if (use_mmse) mimo_mmse_detect(h_hat, y, N0, xh, nv);
                else          mimo_zf_detect  (h_hat, y, N0, xh, nv);
                x_hatA[d] = xh[0]; x_hatB[d] = xh[1];
                nvA_sum += nv[0]; nvB_sum += nv[1];
            }
            double envA = nvA_sum / num_data, envB = nvB_sum / num_data;

            qam_demap_llr(x_hatA, nd, mcs.modulation, envA, allllrA);
            qam_demap_llr(x_hatB, nd, mcs.modulation, envB, allllrB);
            int llr_len = acsz < nd*bps ? acsz : nd*bps;
            memcpy(llrA, allllrA, llr_len*sizeof(double));
            memcpy(llrB, allllrB, llr_len*sizeof(double));
            for (int i=llr_len;i<acsz;i++) { llrA[i]=0.0; llrB[i]=0.0; }

            ldpc_decode(&ldpc, llrA, 25, decodedA);
            ldpc_decode(&ldpc, llrB, 25, decodedB);
            int crcA_ok = check_crc(decodedA, K, CRC24A);
            int crcB_ok = check_crc(decodedB, K, CRC24A);
            int ml = tbsz < ldpc.info_size-crc_bits ? tbsz : ldpc.info_size-crc_bits;
            if (ml<0) ml=0;
            int beA=0, beB=0;
            for (int i=0;i<ml;i++) {
                if (tbA[i]!=decodedA[i]) beA++;
                if (tbB[i]!=decodedB[i]) beB++;
            }
            total_err  += beA + beB;
            total_bits += 2*tbsz;
            if (!crcA_ok || beA>0 || !crcB_ok || beB>0) blk_err++;
        }
        double ber  = total_bits > 0 ? (double)total_err/total_bits : 0.0;
        double bler = (double)blk_err / cfg->numTrials;
        printf("%12.1f%15.4e%15.4f\n", snr, ber, bler);
    }
    printf("\nPDSCH 2x2 SM + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(pilotA); free(pilotB);
    free(dmrsA); free(dmrsB);
    free(tbA); free(tbB); free(tb_crcA); free(tb_crcB);
    free(codedA); free(codedB); free(txbitsA); free(txbitsB);
    free(symsA); free(symsB); free(tx_grid0); free(tx_grid1);
    free(rx_grid0); free(rx_grid1); free(x_hatA); free(x_hatB);
    free(allllrA); free(allllrB); free(llrA); free(llrB);
    free(decodedA); free(decodedB);
    free(rx_pilotsA0); free(rx_pilotsA1); free(rx_pilotsB0); free(rx_pilotsB1);
    free(h_pilots00); free(h_pilots10); free(h_pilots01); free(h_pilots11);
    free(h00_full); free(h10_full); free(h01_full); free(h11_full);
    free(taps00); free(taps10); free(taps01); free(taps11);
}

/* 2x2 SU-MIMO spatial multiplexing over frequency-selective TDL, with HARQ
   circular-buffer IR/Chase combining -- the full combination of
   run_pdsch_sm2x2_tdl_simulation() (2x2 detection + per-RE TDL channel
   estimation) and run_pdsch_harq_simulation() (mother-codeword + circular
   buffer soft combining across retransmission attempts).

   Each layer (A/B) is an independent codeword with its own mother LDPC
   codeword and its own persistent soft-combining buffer. Simplification
   (implementation-defined, same spirit as other approximations in this
   LLS): both layers are assumed to share the same retransmission occasion,
   i.e. attempt k always carries RV round k for *both* layers rather than
   each layer having a fully independent HARQ process schedule. The 2x2
   TDL channel (4 independent tap-sets, one per Tx-Rx antenna pair) is
   redrawn on every attempt, modeling time diversity across HARQ rounds
   exactly like run_pdsch_harq_simulation() redraws its SISO channel.
   Retransmission continues until both layers' CRC pass or HARQ_MAX_RETX
   is reached. */
void run_pdsch_sm2x2_tdl_harq_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr    = get_code_rate(&mcs);
    int    bps   = mcs.modulationOrder;
    int    crc_bits = 24;

    int num_pilots = 6 * num_rb;
    int num_data   = 6 * num_rb;
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);

    int nppl = num_pilots / 2;
    int *pilotA = (int *)malloc(nppl * sizeof(int));
    int *pilotB = (int *)malloc(nppl * sizeof(int));
    for (int i=0;i<nppl;i++) { pilotA[i] = pilot_pos[2*i]; pilotB[i] = pilot_pos[2*i+1]; }
    cx_t *dmrsA = (cx_t *)malloc(nppl * sizeof(cx_t));
    cx_t *dmrsB = (cx_t *)malloc(nppl * sizeof(cx_t));
    dmrs_sequence(0x12345678u, nppl, dmrsA);
    dmrs_sequence(0x1abcdef2u, nppl, dmrsB);

    int E = num_data * bps;   /* bits sent per layer per (re)transmission occasion */
    int max_dbits = E;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, HARQ_MOTHER_RATE);   /* mother code, shared by both layers */
    int ncb = ldpc.coded_size;

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    if (is_chase) rvseq[0] = 0;

    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PDSCH 2x2 SM (HARQ Circular Buffer %s), TDL Frequency-Selective Fading ===\n",
           is_chase ? "Chase" : "IR");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d)\n", HARQ_MOTHER_RATE, ncb);
    printf("Num RB       : %d\n", num_rb);
    printf("Layers       : 2 (independent codewords, shared retransmission occasion)\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    printf("Channel      : TDL per Tx-Rx antenna pair, redrawn every attempt (DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Detector     : %s with per-RE LS channel estimation + interpolation\n",
           use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tbA=(int*)malloc(tbsz*sizeof(int)), *tbB=(int*)malloc(tbsz*sizeof(int));
    int *tb_crcA=(int*)malloc(K*sizeof(int)), *tb_crcB=(int*)malloc(K*sizeof(int));
    int *coded_fullA=(int*)malloc(ncb*sizeof(int)), *coded_fullB=(int*)malloc(ncb*sizeof(int));
    int *selbitsA=(int*)malloc(E*sizeof(int)), *selbitsB=(int*)malloc(E*sizeof(int));
    cx_t *data_symsA=(cx_t*)malloc(num_data*sizeof(cx_t)), *data_symsB=(cx_t*)malloc(num_data*sizeof(cx_t));
    cx_t *tx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *tx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *rx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *rx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *x_hatA=(cx_t*)malloc(num_data*sizeof(cx_t)), *x_hatB=(cx_t*)malloc(num_data*sizeof(cx_t));
    double *allllrA=(double*)malloc(E*sizeof(double)), *allllrB=(double*)malloc(E*sizeof(double));
    double *soft_bufA=(double*)malloc(ncb*sizeof(double)), *soft_bufB=(double*)malloc(ncb*sizeof(double));
    int *decodedA=(int*)malloc(K*sizeof(int)), *decodedB=(int*)malloc(K*sizeof(int));

    cx_t *rx_pilotsA0=(cx_t*)malloc(nppl*sizeof(cx_t)), *rx_pilotsA1=(cx_t*)malloc(nppl*sizeof(cx_t));
    cx_t *rx_pilotsB0=(cx_t*)malloc(nppl*sizeof(cx_t)), *rx_pilotsB1=(cx_t*)malloc(nppl*sizeof(cx_t));
    cx_t *h_pilots00=(cx_t*)malloc(nppl*sizeof(cx_t)), *h_pilots10=(cx_t*)malloc(nppl*sizeof(cx_t));
    cx_t *h_pilots01=(cx_t*)malloc(nppl*sizeof(cx_t)), *h_pilots11=(cx_t*)malloc(nppl*sizeof(cx_t));
    cx_t *h00_full=(cx_t*)malloc(active*sizeof(cx_t)), *h10_full=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *h01_full=(cx_t*)malloc(active*sizeof(cx_t)), *h11_full=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *taps00=(cx_t*)malloc(TDL_MAX_TAPS*sizeof(cx_t)), *taps10=(cx_t*)malloc(TDL_MAX_TAPS*sizeof(cx_t));
    cx_t *taps01=(cx_t*)malloc(TDL_MAX_TAPS*sizeof(cx_t)), *taps11=(cx_t*)malloc(TDL_MAX_TAPS*sizeof(cx_t));

    printf("%10s%14s%14s%14s%12s\n", "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i=0;i<64;i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double snrlin = pow(10.0, snr/10.0);
        double N0     = 1.0 / snrlin;
        double sigma  = sqrt(1.0 / (2.0 * snrlin));
        int total_err=0, total_bits=0, blk_err_final=0;
        int total_err_1st=0, blk_err_1st=0;
        long long total_attempts=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(tbA, tbsz); gen_random_bits(tbB, tbsz);
            attach_crc(tbA, tbsz, CRC24A, tb_crcA);
            attach_crc(tbB, tbsz, CRC24A, tb_crcB);
            ldpc_encode(&ldpc, tb_crcA, coded_fullA);
            ldpc_encode(&ldpc, tb_crcB, coded_fullB);

            for (int i=0;i<ncb;i++) { soft_bufA[i] = 0.0; soft_bufB[i] = 0.0; }

            int crcA_ok=0, crcB_ok=0, beA=0, beB=0, attempts=0;
            int beA_1st=0, beB_1st=0, crcA_1st=0, crcB_1st=0;

            for (int attempt=0; attempt<max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];
                rate_match_select(coded_fullA, ncb, rv, E, selbitsA);
                rate_match_select(coded_fullB, ncb, rv, E, selbitsB);
                qam_modulate(selbitsA, E, mcs.modulation, data_symsA);
                qam_modulate(selbitsB, E, mcs.modulation, data_symsB);

                for (int k=0;k<active;k++) { tx_grid0[k]=CX_ZERO; tx_grid1[k]=CX_ZERO; }
                for (int p=0;p<nppl;p++) { tx_grid0[pilotA[p]] = dmrsA[p]; tx_grid1[pilotB[p]] = dmrsB[p]; }
                for (int d=0;d<num_data;d++) { tx_grid0[data_pos[d]] = data_symsA[d]; tx_grid1[data_pos[d]] = data_symsB[d]; }

                /* channel: fresh 2x2 TDL draw every attempt (time diversity
                   across HARQ rounds) */
                tdl_draw(&tdl_ch, taps00); tdl_draw(&tdl_ch, taps01);
                tdl_draw(&tdl_ch, taps10); tdl_draw(&tdl_ch, taps11);
                for (int k=0;k<active;k++) {
                    cx_t h00 = tdl_freq_response(&tdl_ch, taps00, k);
                    cx_t h01 = tdl_freq_response(&tdl_ch, taps01, k);
                    cx_t h10 = tdl_freq_response(&tdl_ch, taps10, k);
                    cx_t h11 = tdl_freq_response(&tdl_ch, taps11, k);
                    cx_t s0 = h00 * tx_grid0[k] + h01 * tx_grid1[k];
                    cx_t s1 = h10 * tx_grid0[k] + h11 * tx_grid1[k];
                    rx_grid0[k] = s0 + CX_MAKE(randn()*sigma, randn()*sigma);
                    rx_grid1[k] = s1 + CX_MAKE(randn()*sigma, randn()*sigma);
                }

                for (int p=0;p<nppl;p++) {
                    rx_pilotsA0[p] = rx_grid0[pilotA[p]]; rx_pilotsA1[p] = rx_grid1[pilotA[p]];
                    rx_pilotsB0[p] = rx_grid0[pilotB[p]]; rx_pilotsB1[p] = rx_grid1[pilotB[p]];
                }
                ls_estimate(rx_pilotsA0, dmrsA, nppl, h_pilots00);
                ls_estimate(rx_pilotsA1, dmrsA, nppl, h_pilots10);
                ls_estimate(rx_pilotsB0, dmrsB, nppl, h_pilots01);
                ls_estimate(rx_pilotsB1, dmrsB, nppl, h_pilots11);
                interpolate_channel(h_pilots00, nppl, pilotA, active, h00_full);
                interpolate_channel(h_pilots10, nppl, pilotA, active, h10_full);
                interpolate_channel(h_pilots01, nppl, pilotB, active, h01_full);
                interpolate_channel(h_pilots11, nppl, pilotB, active, h11_full);

                /* detection + per-RE noise variance, averaged over data REs
                   into a single representative variance for the LLR
                   demapper (same approximation as run_pdsch_sm2x2_tdl_simulation()) */
                double nvA_sum=0.0, nvB_sum=0.0;
                for (int d=0;d<num_data;d++) {
                    int re = data_pos[d];
                    cx_t h_hat[2][2] = {
                        { h00_full[re], h01_full[re] },
                        { h10_full[re], h11_full[re] }
                    };
                    cx_t y[2] = { rx_grid0[re], rx_grid1[re] };
                    cx_t xh[2]; double nv[2];
                    if (use_mmse) mimo_mmse_detect(h_hat, y, N0, xh, nv);
                    else          mimo_zf_detect  (h_hat, y, N0, xh, nv);
                    x_hatA[d] = xh[0]; x_hatB[d] = xh[1];
                    nvA_sum += nv[0]; nvB_sum += nv[1];
                }
                double envA = nvA_sum / num_data, envB = nvB_sum / num_data;

                qam_demap_llr(x_hatA, num_data, mcs.modulation, envA, allllrA);
                qam_demap_llr(x_hatB, num_data, mcs.modulation, envB, allllrB);

                rate_match_combine(soft_bufA, ncb, rv, E, allllrA);
                rate_match_combine(soft_bufB, ncb, rv, E, allllrB);
                ldpc_decode(&ldpc, soft_bufA, 25, decodedA);
                ldpc_decode(&ldpc, soft_bufB, 25, decodedB);
                crcA_ok = check_crc(decodedA, K, CRC24A);
                crcB_ok = check_crc(decodedB, K, CRC24A);

                int ml = tbsz < ldpc.info_size-crc_bits ? tbsz : ldpc.info_size-crc_bits;
                if (ml<0) ml=0;
                beA=0; beB=0;
                for (int i=0;i<ml;i++) {
                    if (tbA[i]!=decodedA[i]) beA++;
                    if (tbB[i]!=decodedB[i]) beB++;
                }

                if (attempt==0) {
                    beA_1st=beA; beB_1st=beB; crcA_1st=crcA_ok; crcB_1st=crcB_ok;
                }
                attempts = attempt+1;
                if (crcA_ok && crcB_ok) break;
            }

            total_err  += beA + beB;
            total_bits += 2*tbsz;
            if (!crcA_ok || beA>0 || !crcB_ok || beB>0) blk_err_final++;
            total_err_1st += beA_1st + beB_1st;
            if (!crcA_1st || beA_1st>0 || !crcB_1st || beB_1st>0) blk_err_1st++;
            total_attempts += attempts;
        }
        double ber       = total_bits > 0 ? (double)total_err/total_bits : 0.0;
        double bler_1st  = (double)blk_err_1st   / cfg->numTrials;
        double bler_final= (double)blk_err_final / cfg->numTrials;
        double avg_tx    = (double)total_attempts / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr, ber, bler_1st, bler_final, avg_tx);
    }
    printf("\nPDSCH 2x2 SM + TDL + HARQ simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(pilotA); free(pilotB);
    free(dmrsA); free(dmrsB);
    free(tbA); free(tbB); free(tb_crcA); free(tb_crcB);
    free(coded_fullA); free(coded_fullB); free(selbitsA); free(selbitsB);
    free(data_symsA); free(data_symsB); free(tx_grid0); free(tx_grid1);
    free(rx_grid0); free(rx_grid1); free(x_hatA); free(x_hatB);
    free(allllrA); free(allllrB); free(soft_bufA); free(soft_bufB);
    free(decodedA); free(decodedB);
    free(rx_pilotsA0); free(rx_pilotsA1); free(rx_pilotsB0); free(rx_pilotsB1);
    free(h_pilots00); free(h_pilots10); free(h_pilots01); free(h_pilots11);
    free(h00_full); free(h10_full); free(h01_full); free(h11_full);
    free(taps00); free(taps10); free(taps01); free(taps11);
}

/* HARQ with circular-buffer rate matching (Chase / IR), SISO + DMRS.
   A single low-rate mother LDPC codeword is generated per trial; each
   (re)transmission attempt selects E bits via rate_match_select() at
   the attempt's RV, sends them over a fresh channel realization (models
   time diversity across HARQ rounds), and soft-combines the resulting
   LLRs into a persistent Ncb-length buffer via rate_match_combine()
   before decoding. Reports both the 1st-transmission-only BLER (as if
   HARQ were disabled) and the final BLER after up to HARQ_MAX_RETX
   attempts, plus the average number of transmissions used. */
void run_pdsch_harq_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
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

    int E = num_data * bps;   /* bits sent per (re)transmission occasion */
    int max_dbits = E;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, HARQ_MOTHER_RATE);   /* mother code */
    int ncb = ldpc.coded_size;

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    int use_ray  = (strcmp(cfg->channelModel,"AWGN")!=0);

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    if (is_chase) rvseq[0] = 0;

    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PDSCH HARQ (Circular Buffer %s) ===\n", is_chase ? "Chase" : "IR");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d)\n", HARQ_MOTHER_RATE, ncb);
    printf("Num RB       : %d\n", num_rb);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    printf("Channel      : %s\n", use_ray ? "Flat Rayleigh fading" : "AWGN (H=1)");
    printf("Equalizer    : %s with LS channel estimation\n", use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb        = (int *)malloc(tbsz * sizeof(int));
    int *tb_crc    = (int *)malloc(K    * sizeof(int));
    int *coded_full= (int *)malloc(ncb  * sizeof(int));
    int *selbits   = (int *)malloc(E    * sizeof(int));
    cx_t *data_syms = (cx_t *)malloc(num_data * sizeof(cx_t));
    cx_t *tx_grid   = (cx_t *)malloc(active     * sizeof(cx_t));
    cx_t *rx_grid   = (cx_t *)malloc(active     * sizeof(cx_t));
    cx_t *rx_pilots = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_pilots  = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_full    = (cx_t *)malloc(active     * sizeof(cx_t));
    cx_t *rx_data   = (cx_t *)malloc(num_data   * sizeof(cx_t));
    cx_t *h_data    = (cx_t *)malloc(num_data   * sizeof(cx_t));
    cx_t *eq_data   = (cx_t *)malloc(num_data   * sizeof(cx_t));
    double *allllr  = (double *)malloc(E   * sizeof(double));
    double *soft_buf= (double *)malloc(ncb * sizeof(double));
    int    *decoded = (int *)malloc(K      * sizeof(int));

    printf("%10s%14s%14s%14s%12s\n", "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i=0;i<64;i++) printf("-");
    printf("\n");

    AWGNChannel awgn_ch;
    FlatFadingChannel ray_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        double N0 = 1.0 / pow(10.0, snr/10.0);
        int total_err=0, total_bits=0, blk_err_final=0;
        int total_err_1st=0, blk_err_1st=0;
        long long total_attempts=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded_full);

            for (int i=0;i<ncb;i++) soft_buf[i] = 0.0;

            int crc_ok = 0, be = 0, attempts = 0;
            int be_1st = 0, crc_ok_1st = 0;

            for (int attempt=0; attempt<max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];
                rate_match_select(coded_full, ncb, rv, E, selbits);
                qam_modulate(selbits, E, mcs.modulation, data_syms);

                for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
                for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
                for (int d=0;d<num_data;d++)   tx_grid[data_pos[d]]  = data_syms[d];

                if (use_ray) { awgn_init(&awgn_ch, snr); flat_fading_init(&ray_ch, snr);
                               flat_fading_apply(&ray_ch, tx_grid, active, rx_grid, NULL); }
                else         { awgn_init(&awgn_ch, snr);
                               awgn_add_noise(&awgn_ch, tx_grid, active, 0, rx_grid); }

                for (int p=0;p<num_pilots;p++) rx_pilots[p] = rx_grid[pilot_pos[p]];
                ls_estimate(rx_pilots, dmrs_sym, num_pilots, h_pilots);
                interpolate_channel(h_pilots, num_pilots, pilot_pos, active, h_full);

                for (int d=0;d<num_data;d++) {
                    rx_data[d] = rx_grid[data_pos[d]];
                    h_data[d]  = h_full[data_pos[d]];
                }

                if (use_mmse) {
                    mmse_equalize(rx_data, h_data, num_data, N0, eq_data, NULL);
                    qam_demap_llr_mmse(eq_data, num_data, mcs.modulation, h_data, N0, allllr);
                } else {
                    zf_equalize(rx_data, h_data, num_data, eq_data);
                    double mhp = 0.0;
                    for (int d=0;d<num_data;d++) mhp += CX_NORM(h_data[d]);
                    mhp /= num_data;
                    double env = (mhp > 1e-10) ? N0/mhp : N0;
                    qam_demap_llr(eq_data, num_data, mcs.modulation, env, allllr);
                }

                rate_match_combine(soft_buf, ncb, rv, E, allllr);
                ldpc_decode(&ldpc, soft_buf, 25, decoded);
                crc_ok = check_crc(decoded, K, CRC24A);

                be = 0;
                int ml = tbsz < ldpc.info_size-crc_bits ? tbsz : ldpc.info_size-crc_bits;
                if (ml<0) ml=0;
                for (int i=0;i<ml;i++) if (tb[i]!=decoded[i]) be++;

                if (attempt==0) { be_1st = be; crc_ok_1st = crc_ok; }
                attempts = attempt+1;
                if (crc_ok) break;
            }

            total_err  += be;
            total_bits += tbsz;
            if (!crc_ok || be > 0) blk_err_final++;
            total_err_1st += be_1st;
            if (!crc_ok_1st || be_1st > 0) blk_err_1st++;
            total_attempts += attempts;
        }
        double ber       = total_bits > 0 ? (double)total_err/total_bits : 0.0;
        double bler_1st  = (double)blk_err_1st   / cfg->numTrials;
        double bler_final= (double)blk_err_final / cfg->numTrials;
        double avg_tx    = (double)total_attempts / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr, ber, bler_1st, bler_final, avg_tx);
    }
    printf("\nPDSCH HARQ simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded_full); free(selbits);
    free(data_syms); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(allllr); free(soft_buf); free(decoded);
}

/* Frequency-selective TDL fading, SISO + DMRS. Nearly identical to
   run_pdsch_dmrs_simulation(); only the channel-generation step is
   replaced with a per-RE TDL response (tdl_draw + tdl_channel_apply)
   instead of a single frequency-flat tap. This is the first channel
   model where interpolate_channel() actually interpolates a channel
   that varies across REs. */
void run_pdsch_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
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

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PDSCH + DMRS, TDL Frequency-Selective Fading ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Active SC    : %d\n", active);
    printf("Pilot REs    : %d (DMRS Type 1, 50%% overhead)\n", num_pilots);
    printf("Data REs     : %d\n", num_data);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Channel      : TDL (approx NLOS PDP, DS=%.0fns, %d taps -- not exact TS 38.901 table)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
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
    cx_t   *taps     = (cx_t *)malloc(TDL_MAX_TAPS   * sizeof(cx_t));

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<42;i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
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

            /* channel: one TDL tap-gain draw per trial (block-flat in
               time), frequency response varies per RE */
            tdl_draw(&tdl_ch, taps);
            tdl_channel_apply(&tdl_ch, taps, tx_grid, active, rx_grid, NULL);

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
    printf("\nPDSCH TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(txbits);
    free(data_syms); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(allllr); free(llr); free(decoded); free(taps);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 4×4 SU-MIMO 공간 다중화 시뮬레이션 (블록-평탄 채널)
 *
 * 구조: 4 Tx 레이어 × 4 Rx 안테나, 각 레이어마다 독립 코드워드/LDPC
 *
 * 파일럿 설계: FDM 방식으로 6*num_rb 파일럿 위치를 4개 레이어에 라운드로빈
 *   분배. 레이어 l의 파일럿 RE에서는 레이어 l만 DMRS를 송신하고 나머지 3개
 *   레이어는 0을 송신 → 각 H[r][t]를 독립적으로 LS 추정 가능.
 *   nppl = floor(6*num_rb / 4) (레이어당 파일럿 수)
 *
 * 채널 추정: 블록-평탄 가정 → pilot RE 전체 평균 LS
 *   H_hat[r][t] = mean_p( rx_grid[r][pilotL[t][p]] / dmrsL[t][p] )
 *
 * 검출: ZF (H⁻¹y) 또는 MMSE (de-biased W = (H^H H + N0·I)⁻¹ H^H)
 *   → 4×4 Gauss-Jordan 역행렬로 구현 (mimo.c: inv4x4)
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_sm4x4_simulation(const L1Config *cfg) {
    const int NL = 4;   /* 레이어(= Tx 안테나 = Rx 안테나) 수 */

    int num_rb = cfg->numRB;
    int active = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    /* 파일럿 / 데이터 RE 인덱스 */
    int num_pilots = 6 * num_rb;
    int num_data   = 6 * num_rb;
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);

    /* 레이어별 파일럿 위치: 라운드로빈으로 분배 */
    int nppl = num_pilots / NL;   /* 레이어당 파일럿 수 (나머지 버림) */
    int *pilotL[NL];
    cx_t *dmrsL[NL];
    uint32_t seeds[4] = { 0x12345678u, 0x1abcdef2u, 0xdeadbeef, 0xcafebabe };
    for (int l = 0; l < NL; l++) {
        pilotL[l] = (int   *)malloc(nppl * sizeof(int));
        dmrsL[l]  = (cx_t *)malloc(nppl * sizeof(cx_t));
        for (int p = 0; p < nppl; p++)
            pilotL[l][p] = pilot_pos[NL * p + l];   /* 4-way interleave */
        dmrs_sequence(seeds[l], nppl, dmrsL[l]);
    }

    /* 코드워드 크기 (레이어마다 동일) */
    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K    = tbsz + crc_bits;
    LDPCCodec ldpc;
    ldpc_init(&ldpc, K, cr);
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);
    int nsym  = padsz / bps;
    int nd    = (nsym < num_data) ? nsym : num_data;

    int use_mmse = (strcmp(cfg->equalizer, "MMSE") == 0);

    printf("=== PDSCH 4x4 Spatial Multiplexing (SM_4X4) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Layers       : 4 (independent codewords per layer)\n");
    printf("Tx/Rx Ants   : 4 / 4\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Pilot/layer  : %d REs  (FDM, 4-way interleave)\n", nppl);
    printf("Detector     : %s  (4x4 Gauss-Jordan inverse)\n",
           use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    /* 레이어별 버퍼 */
    int   *tb[NL], *tb_crc[NL], *coded[NL], *txbits[NL], *decoded_buf[NL];
    cx_t  *syms[NL];
    cx_t  *tx_grid[NL], *rx_grid[NL];
    double *allllr[NL], *llr_buf[NL];
    double nv_sum[NL];
    cx_t  x_hat_re[NL];
    double nv_re[NL];

    for (int l = 0; l < NL; l++) {
        tb[l]          = (int   *)malloc(tbsz * sizeof(int));
        tb_crc[l]      = (int   *)malloc(K    * sizeof(int));
        coded[l]       = (int   *)malloc(acsz * sizeof(int));
        txbits[l]      = (int   *)malloc(padsz * sizeof(int));
        syms[l]        = (cx_t *)malloc(nsym  * sizeof(cx_t));
        tx_grid[l]     = (cx_t *)calloc(active, sizeof(cx_t));
        rx_grid[l]     = (cx_t *)malloc(active * sizeof(cx_t));
        allllr[l]      = (double *)malloc(padsz * sizeof(double));
        llr_buf[l]     = (double *)malloc(acsz  * sizeof(double));
        decoded_buf[l] = (int   *)malloc(K      * sizeof(int));
    }

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i = 0; i < 42; i++) printf("-");
    printf("\n");

    MIMOChannel mch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        mimo_channel_init(&mch, snr);
        double N0 = 1.0 / pow(10.0, snr / 10.0);

        long total_err = 0, total_bits = 0;
        int  blk_err   = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① TX: 레이어별 독립 코드워드 생성 */
            for (int l = 0; l < NL; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                ldpc_encode(&ldpc, tb_crc[l], coded[l]);
                memcpy(txbits[l], coded[l], acsz * sizeof(int));
                for (int i = acsz; i < padsz; i++) txbits[l][i] = 0;
                qam_modulate(txbits[l], padsz, mcs.modulation, syms[l]);
            }

            /* ② 리소스 그리드 구성 */
            for (int l = 0; l < NL; l++) {
                memset(tx_grid[l], 0, active * sizeof(cx_t));
                /* 파일럿: 레이어 l만 자신의 파일럿 위치에 DMRS 송신 */
                for (int p = 0; p < nppl; p++)
                    tx_grid[l][pilotL[l][p]] = dmrsL[l][p];
                /* 데이터: 4개 레이어 모두 동일한 data_pos에 심볼 배치 */
                for (int d = 0; d < nd; d++)
                    tx_grid[l][data_pos[d]] = syms[l][d];
            }

            /* ③ 채널 적용: y[r][k] = Σ_t H[r][t] · tx[t][k] + n */
            cx_t h[NL][NL];
            mimo_channel_draw_4x4(h);
            for (int r = 0; r < NL; r++) {
                double sigma = sqrt(N0 / 2.0);
                for (int k = 0; k < active; k++) {
                    cx_t s = 0;
                    for (int t = 0; t < NL; t++) s += h[r][t] * tx_grid[t][k];
                    rx_grid[r][k] = s + CX_MAKE(randn() * sigma, randn() * sigma);
                }
            }

            /* ④ 채널 추정: H_hat[r][t] = mean_p( rx[r][pilotL[t][p]] / dmrsL[t][p] )
             *   레이어 t의 파일럿 RE에서는 레이어 t만 송신했으므로
             *   rx[r][pos] ≈ H[r][t] · dmrs[t][p] + noise → 평균 LS 추정 */
            cx_t h_hat[NL][NL];
            for (int t = 0; t < NL; t++) {
                for (int r = 0; r < NL; r++) h_hat[r][t] = 0;
                for (int p = 0; p < nppl; p++) {
                    int pos = pilotL[t][p];
                    for (int r = 0; r < NL; r++)
                        h_hat[r][t] += rx_grid[r][pos] / dmrsL[t][p];
                }
                for (int r = 0; r < NL; r++) h_hat[r][t] /= nppl;
            }

            /* ⑤ 검출 + LLR 계산 */
            for (int l = 0; l < NL; l++) nv_sum[l] = 0.0;

            for (int d = 0; d < nd; d++) {
                cx_t y[NL];
                for (int r = 0; r < NL; r++) y[r] = rx_grid[r][data_pos[d]];

                if (use_mmse) mimo_mmse_detect_4x4(h_hat, y, N0, x_hat_re, nv_re);
                else          mimo_zf_detect_4x4  (h_hat, y, N0, x_hat_re, nv_re);

                for (int l = 0; l < NL; l++) {
                    syms[l][d] = x_hat_re[l];   /* 추정 심볼 임시 저장 */
                    nv_sum[l] += nv_re[l];
                }
            }

            for (int l = 0; l < NL; l++) {
                double env = nv_sum[l] / nd;
                qam_demap_llr(syms[l], nd, mcs.modulation, env, allllr[l]);
                int llr_len = (acsz < nd * bps) ? acsz : nd * bps;
                memcpy(llr_buf[l], allllr[l], llr_len * sizeof(double));
                for (int i = llr_len; i < acsz; i++) llr_buf[l][i] = 0.0;
            }

            /* ⑥ LDPC 디코딩 + 오류 집계 */
            int trial_err = 0;
            for (int l = 0; l < NL; l++) {
                ldpc_decode(&ldpc, llr_buf[l], 25, decoded_buf[l]);
                int crc_ok = check_crc(decoded_buf[l], K, CRC24A);
                int bit_err = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[l][i] != decoded_buf[l][i]) bit_err++;
                total_err  += bit_err;
                total_bits += tbsz;
                if (!crc_ok || bit_err > 0) trial_err++;
            }
            if (trial_err > 0) blk_err++;   /* 1개 이상 레이어에서 오류 */

            (void)trial;
        }

        double ber  = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double bler = blk_err / (double)cfg->numTrials;
        printf("%12.1f%15.4e%15.4f\n", snr, ber, bler);
    }
    printf("\nPDSCH 4x4 SM simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos);
    for (int l = 0; l < NL; l++) {
        free(pilotL[l]); free(dmrsL[l]);
        free(tb[l]); free(tb_crc[l]); free(coded[l]); free(txbits[l]);
        free(syms[l]); free(tx_grid[l]); free(rx_grid[l]);
        free(allllr[l]); free(llr_buf[l]); free(decoded_buf[l]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 4x4 Spatial Multiplexing (SM_4X4), TDL Frequency-Selective Fading
 *
 * 채널 추정  : Genie-aided (완벽 CSI — 4-way 파일럿 간격 8 SC이 DS=300ns
 *              채널의 코히어런스 대역 ~13 SC에 너무 가까워 LS+선형보간 오류
 *              floor 발생. CL_4PORT TDL과 동일 이유로 genie-aided 사용)
 * 채널 모델  : TDL, 16 독립 tap-set [Rx][Tx], RE별 tdl_freq_response
 * 검출기     : MMSE / ZF (4×4 Gauss-Jordan, per-RE 완벽 H 사용)
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_sm4x4_tdl_simulation(const L1Config *cfg) {
    const int NL = 4;
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;   /* flat 버전과 동일 데이터 RE 수 유지 */
    int *data_pos = (int *)malloc(num_data * sizeof(int));
    dmrs_data_indices(num_rb, data_pos);

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;
    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);
    int nsym  = padsz / bps;
    int nd    = (nsym < num_data) ? nsym : num_data;

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PDSCH 4x4 Spatial Multiplexing (SM_4X4), TDL Frequency-Selective Fading ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Layers       : 4 (independent codewords per layer)\n");
    printf("Tx/Rx Ants   : 4 / 4\n");
    printf("Channel      : TDL per Tx-Rx pair (16 tap-sets, DS=%.0fns, %d taps, genie-aided CSI)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Detector     : %s  (4×4 Gauss-Jordan, per-RE perfect H)\n", use_mmse ? "MMSE" : "ZF");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Data RE/layer: %d  (pilot 오버헤드 동일 회계, genie)\n", num_data);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[NL], *tb_crc[NL], *coded[NL], *txbits[NL], *decoded_buf[NL];
    cx_t  *syms[NL];
    double *allllr[NL], *llr_buf[NL];
    for (int l = 0; l < NL; l++) {
        tb[l]          = (int   *)malloc(tbsz  * sizeof(int));
        tb_crc[l]      = (int   *)malloc(K     * sizeof(int));
        coded[l]       = (int   *)malloc(acsz  * sizeof(int));
        txbits[l]      = (int   *)malloc(padsz * sizeof(int));
        syms[l]        = (cx_t  *)malloc(nsym  * sizeof(cx_t));
        allllr[l]      = (double *)malloc(padsz * sizeof(double));
        llr_buf[l]     = (double *)malloc(acsz  * sizeof(double));
        decoded_buf[l] = (int   *)malloc(K      * sizeof(int));
    }

    cx_t  *taps[NL][NL];
    for (int r = 0; r < NL; r++)
        for (int t = 0; t < NL; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i = 0; i < 42; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    cx_t x_hat_re[NL]; double nv_re[NL];

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long total_err = 0, total_bits = 0;
        int  blk_err   = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① TX */
            for (int l = 0; l < NL; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                ldpc_encode(&ldpc, tb_crc[l], coded[l]);
                memcpy(txbits[l], coded[l], acsz * sizeof(int));
                for (int i = acsz; i < padsz; i++) txbits[l][i] = 0;
                qam_modulate(txbits[l], padsz, mcs.modulation, syms[l]);
            }

            /* ② TDL: 16 tap-set draw */
            for (int r = 0; r < NL; r++)
                for (int t = 0; t < NL; t++)
                    tdl_draw(&tdl_ch, taps[r][t]);

            /* ③ 검출 + LLR (genie-aided: 데이터 RE마다 진짜 H 사용) */
            double nv_sum[NL]; for (int l = 0; l < NL; l++) nv_sum[l] = 0.0;

            for (int d = 0; d < nd; d++) {
                int k = data_pos[d];

                /* 진짜 4×4 채널 H[r][t] = tdl_freq_response */
                cx_t h_true[NL][NL];
                for (int r = 0; r < NL; r++)
                    for (int t = 0; t < NL; t++)
                        h_true[r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);

                /* 수신: y[r] = Σ_t H_rt * sym_t + noise */
                cx_t y[NL];
                for (int r = 0; r < NL; r++) {
                    cx_t s = CX_ZERO;
                    for (int t = 0; t < NL; t++)
                        s += h_true[r][t] * syms[t][d];
                    y[r] = s + CX_MAKE(randn() * sigma, randn() * sigma);
                }

                if (use_mmse) mimo_mmse_detect_4x4(h_true, y, N0, x_hat_re, nv_re);
                else          mimo_zf_detect_4x4  (h_true, y, N0, x_hat_re, nv_re);

                for (int l = 0; l < NL; l++) {
                    syms[l][d] = x_hat_re[l];
                    nv_sum[l] += nv_re[l];
                }
            }

            for (int l = 0; l < NL; l++) {
                double env = nv_sum[l] / nd;
                qam_demap_llr(syms[l], nd, mcs.modulation, env, allllr[l]);
                int llr_len = (acsz < nd * bps) ? acsz : nd * bps;
                memcpy(llr_buf[l], allllr[l], llr_len * sizeof(double));
                for (int i = llr_len; i < acsz; i++) llr_buf[l][i] = 0.0;
            }

            /* ④ LDPC 디코딩 */
            int trial_err = 0;
            for (int l = 0; l < NL; l++) {
                ldpc_decode(&ldpc, llr_buf[l], 25, decoded_buf[l]);
                int crc_ok = check_crc(decoded_buf[l], K, CRC24A);
                int bit_err = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[l][i] != decoded_buf[l][i]) bit_err++;
                total_err  += bit_err;
                total_bits += tbsz;
                if (!crc_ok || bit_err > 0) trial_err++;
            }
            if (trial_err > 0) blk_err++;
        }

        double ber  = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double bler = (double)blk_err / cfg->numTrials;
        printf("%12.1f%15.4e%15.4f\n", snr, ber, bler);
    }
    printf("\nPDSCH 4x4 SM + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(data_pos);
    for (int l = 0; l < NL; l++) {
        free(tb[l]); free(tb_crc[l]); free(coded[l]); free(txbits[l]);
        free(syms[l]); free(allllr[l]); free(llr_buf[l]); free(decoded_buf[l]);
    }
    for (int r = 0; r < NL; r++)
        for (int t = 0; t < NL; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 4x4 Spatial Multiplexing (SM_4X4), HARQ Circular Buffer (Chase / IR)
 * 지원 채널: FLAT_FADING (블록 flat, 시도마다 재추첨) / TDL (genie-aided, 16 tap-set)
 *
 * 채널 추정  : Genie-aided (완벽 CSI — SM_4X4 TDL과 동일, 파일럿 간격 8 SC 문제 회피)
 * HARQ      : rate_match_select / rate_match_combine (SM_2X2 TDL HARQ와 동일 구조)
 * 레이어    : 4개 독립 CW / soft-buffer / LDPC
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_sm4x4_harq_simulation(const L1Config *cfg) {
    const int NL = 4;
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int *data_pos = (int *)malloc(num_data * sizeof(int));
    dmrs_data_indices(num_rb, data_pos);

    int is_tdl  = (strcmp(cfg->channelModel, "TDL") == 0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr   = get_code_rate(&mcs);
    int    bps  = mcs.modulationOrder;
    int    crc_bits = 24;

    int E    = num_data * bps;
    int tbsz = (int)(E * cr);
    if (tbsz < 1)    tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;
    int K = tbsz + crc_bits;

    LDPCCodec ldpc; ldpc_init(&ldpc, K, HARQ_MOTHER_RATE);
    int ncb = ldpc.coded_size;

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PDSCH 4x4 SM (HARQ Circular Buffer %s), %s ===\n",
           is_chase ? "Chase" : "IR",
           is_tdl ? "TDL Frequency-Selective Fading" : "Flat Fading");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d)\n", HARQ_MOTHER_RATE, ncb);
    printf("Num RB       : %d\n", num_rb);
    printf("Layers       : 4 (independent codewords, shared retransmission occasion)\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : TDL per Tx-Rx pair (16 tap-sets, DS=%.0fns, %d taps, genie-aided)\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    else
        printf("Channel      : Flat Fading 4x4 (block-flat, redrawn per HARQ attempt, genie-aided)\n");
    printf("Detector     : %s (genie-aided CSI)\n", use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[NL], *tb_crc[NL], *coded_full[NL], *selbits[NL], *decoded[NL];
    cx_t  *data_syms[NL], *x_hat[NL];
    double *allllr[NL], *soft_buf[NL];
    for (int l = 0; l < NL; l++) {
        tb[l]         = (int *)malloc(tbsz    * sizeof(int));
        tb_crc[l]     = (int *)malloc(K       * sizeof(int));
        coded_full[l] = (int *)malloc(ncb     * sizeof(int));
        selbits[l]    = (int *)malloc(E       * sizeof(int));
        decoded[l]    = (int *)malloc(K       * sizeof(int));
        data_syms[l]  = (cx_t *)malloc(num_data * sizeof(cx_t));
        x_hat[l]      = (cx_t *)malloc(num_data * sizeof(cx_t));
        allllr[l]     = (double *)malloc(E    * sizeof(double));
        soft_buf[l]   = (double *)malloc(ncb  * sizeof(double));
    }
    cx_t *taps[NL][NL];
    for (int r = 0; r < NL; r++)
        for (int t = 0; t < NL; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%10s%14s%14s%14s%12s\n", "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i = 0; i < 64; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double snrlin = pow(10.0, snr / 10.0);
        double N0     = 1.0 / snrlin;
        double sigma  = sqrt(1.0 / (2.0 * snrlin));

        int total_err=0, total_bits=0, blk_err_final=0, blk_err_1st=0;
        long long total_attempts = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {
            for (int l = 0; l < NL; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                ldpc_encode(&ldpc, tb_crc[l], coded_full[l]);
                for (int i = 0; i < ncb; i++) soft_buf[l][i] = 0.0;
            }

            int crc_ok[NL], be[NL], be_1st[NL], crc_1st[NL];
            for (int l = 0; l < NL; l++) { crc_ok[l]=0; be[l]=0; be_1st[l]=0; crc_1st[l]=0; }
            int attempts = 0;

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int l = 0; l < NL; l++) {
                    rate_match_select(coded_full[l], ncb, rv, E, selbits[l]);
                    qam_modulate(selbits[l], E, mcs.modulation, data_syms[l]);
                }

                /* fresh channel draw every attempt (time diversity across HARQ rounds) */
                cx_t h_flat[NL][NL];
                if (!is_tdl) {
                    mimo_channel_draw_4x4(h_flat);
                } else {
                    for (int r = 0; r < NL; r++)
                        for (int t = 0; t < NL; t++)
                            tdl_draw(&tdl_ch, taps[r][t]);
                }

                double nv_sum[NL]; for (int l = 0; l < NL; l++) nv_sum[l] = 0.0;

                for (int d = 0; d < num_data; d++) {
                    int k = data_pos[d];
                    cx_t h_true[NL][NL];
                    if (!is_tdl) {
                        for (int r = 0; r < NL; r++)
                            for (int t = 0; t < NL; t++)
                                h_true[r][t] = h_flat[r][t];
                    } else {
                        for (int r = 0; r < NL; r++)
                            for (int t = 0; t < NL; t++)
                                h_true[r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                    }

                    cx_t tx[NL];
                    for (int l = 0; l < NL; l++) tx[l] = data_syms[l][d];
                    cx_t y[NL];
                    for (int r = 0; r < NL; r++) {
                        cx_t s = CX_ZERO;
                        for (int t = 0; t < NL; t++) s += h_true[r][t] * tx[t];
                        y[r] = s + CX_MAKE(randn() * sigma, randn() * sigma);
                    }

                    cx_t x_hat_re[NL]; double nv_re[NL];
                    if (use_mmse) mimo_mmse_detect_4x4(h_true, y, N0, x_hat_re, nv_re);
                    else          mimo_zf_detect_4x4  (h_true, y, N0, x_hat_re, nv_re);
                    for (int l = 0; l < NL; l++) {
                        x_hat[l][d] = x_hat_re[l];
                        nv_sum[l]  += nv_re[l];
                    }
                }

                int ml = tbsz < ldpc.info_size - crc_bits ? tbsz : ldpc.info_size - crc_bits;
                if (ml < 0) ml = 0;
                for (int l = 0; l < NL; l++) {
                    double env = nv_sum[l] / num_data;
                    qam_demap_llr(x_hat[l], num_data, mcs.modulation, env, allllr[l]);
                    rate_match_combine(soft_buf[l], ncb, rv, E, allllr[l]);
                    ldpc_decode(&ldpc, soft_buf[l], 25, decoded[l]);
                    crc_ok[l] = check_crc(decoded[l], K, CRC24A);
                    int biterr = 0;
                    for (int i = 0; i < ml; i++)
                        if (tb[l][i] != decoded[l][i]) biterr++;
                    be[l] = biterr;
                    if (attempt == 0) { be_1st[l] = biterr; crc_1st[l] = crc_ok[l]; }
                }

                attempts = attempt + 1;
                int all_ok = 1;
                for (int l = 0; l < NL; l++) if (!crc_ok[l]) { all_ok = 0; break; }
                if (all_ok) break;
            }

            int any_err_1st = 0, any_err_final = 0;
            for (int l = 0; l < NL; l++) {
                total_err  += be[l];
                total_bits += tbsz;
                if (!crc_1st[l] || be_1st[l] > 0) any_err_1st   = 1;
                if (!crc_ok[l]  || be[l]    > 0) any_err_final = 1;
            }
            if (any_err_1st)   blk_err_1st++;
            if (any_err_final) blk_err_final++;
            total_attempts += attempts;
        }

        double ber       = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double bler_1st  = (double)blk_err_1st   / cfg->numTrials;
        double bler_final= (double)blk_err_final  / cfg->numTrials;
        double avg_tx    = (double)total_attempts / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr, ber, bler_1st, bler_final, avg_tx);
    }
    printf("\nPDSCH 4x4 SM + %s + HARQ simulation complete.\n",
           is_tdl ? "TDL" : "Flat Fading");

    ldpc_free(&ldpc);
    free(data_pos);
    for (int l = 0; l < NL; l++) {
        free(tb[l]); free(tb_crc[l]); free(coded_full[l]); free(selbits[l]);
        free(decoded[l]); free(data_syms[l]); free(x_hat[l]);
        free(allllr[l]); free(soft_buf[l]);
    }
    for (int r = 0; r < NL; r++)
        for (int t = 0; t < NL; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 4포트 Type I SP 코드북 기반 Closed-Loop PDSCH — RI + PMI 적응 선택
 *
 * 채널 모델  : 블록-평탄 Rayleigh H[4][4] (iid CN(0,1))
 * 채널 추정  : Genie-aided (완벽 CSI — 프리코딩 이득 자체에만 집중)
 * 프리코더   : TS 38.214 Table 5.2.2.2.1-5/6 (N1=2, O1=4, Ng=2)
 *               Rank-1 : 32 후보 (8빔 × 4코피에이징)
 *               Rank-2 : 48 후보 (변형A 16 + 변형B 32)
 * 선택 기준  : 추정 Shannon 용량 최대화 (80 후보 전수 탐색)
 *               Rank-1 : C₁ = log₂(1 + ||H·W||²/N₀)
 *               Rank-2 : C₂ = Σ_l log₂(1 + α_l/(1−α_l))
 * 검출기     : Rank-1 → MRC (4-Rx), Rank-2 → MMSE (4Rx×2Layer)
 *
 * 비교 출력  :
 *   ① Adaptive  — RI+PMI 자동 선택 (rank-1 또는 rank-2 중 용량 우수 쪽)
 *   ② R1-fixed  — rank-1 고정, 최적 PMI 선택 (32 후보 내)
 *   ③ R2-fixed  — rank-2 고정, 최적 PMI 선택 (48 후보 내)
 *
 * 공정 비교  : 동일 채널 H, 동일 노이즈 벡터 n, 동일 정보 비트
 *              → 전략 차이(precoder + rank)만 다름
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_4port_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;   /* OFDM symbol당 데이터 RE (DMRS용 절반 제외) */

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc;
    ldpc_init(&ldpc, K, cr);
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);
    int nsym  = padsz / bps;
    int nd    = (nsym < num_data) ? nsym : num_data;

    printf("=== PDSCH Closed-Loop 4-port Codebook (RI+PMI Adaptive) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 4 / 4\n");
    printf("Codebook     : TS 38.214 Type I SP (N1=2, O1=4, Ng=2)\n");
    printf("Rank Range   : 1~2  (RI+PMI 자동 선택, 추정 용량 기준)\n");
    printf("Tx Corr      : rho=%.2f, rho_xpol=%.2f (%s, Kronecker R_pol(x)R_ant, RX 비상관)\n",
           cfg->spatialCorrTx, cfg->spatialCorrXpol,
           (cfg->spatialCorrTx > 0.0 || cfg->spatialCorrXpol > 0.0) ? "공간상관" : "i.i.d.");
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d  (Genie-aided CSI, pilot 오버헤드 없음)\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    /* 코드워드 버퍼 (최대 2 CW) */
    int   *tb[2], *tb_crc[2], *coded[2], *txbits[2], *dec[2];
    cx_t  *sym[2];
    double *allllr[2], *llr[2];
    cx_t  *rx_hat[2];   /* 검출된 심볼 (LLR 계산용) */
    for (int l = 0; l < 2; l++) {
        tb[l]     = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l] = (int    *)malloc(K     * sizeof(int));
        coded[l]  = (int    *)malloc(acsz  * sizeof(int));
        txbits[l] = (int    *)malloc(padsz * sizeof(int));
        sym[l]    = (cx_t  *)malloc(nsym   * sizeof(cx_t));
        allllr[l] = (double *)malloc(padsz * sizeof(double));
        llr[l]    = (double *)malloc(acsz  * sizeof(double));
        dec[l]    = (int    *)malloc(K     * sizeof(int));
        rx_hat[l] = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    /* 노이즈 버퍼: nd RE × 4 Rx 안테나 */
    cx_t *nbuf = (cx_t *)malloc(nd * 4 * sizeof(cx_t));

    /* 출력 헤더 */
    printf("%-9s  %-12s %-11s  %-12s %-11s  %-12s %-11s  %s\n",
           "SNR(dB)", "BER_Adapt", "BLER_Adapt",
           "BER_R1fix", "BLER_R1fix",
           "BER_R2fix", "BLER_R2fix", "R1%");
    for (int i = 0; i < 98; i++) printf("-");
    printf("\n");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_ad = 0, b_ad = 0;
        long t_r1 = 0, b_r1 = 0;
        long t_r2 = 0, b_r2 = 0;
        int  e_ad = 0, e_r1 = 0, e_r2 = 0;
        int  r1_sel_cnt = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 채널 드로우: H[4][4], 필요 시 Tx 공간상관(Kronecker) 적용 */
            cx_t H[4][4];
            mimo_channel_draw_4x4(H);
            mimo_apply_tx_correlation_4x4(H, cfg->spatialCorrTx, cfg->spatialCorrXpol);

            /* ② RI+PMI 선택 (80 후보 전수 탐색)
             *   → 적응형 best + rank-1 best + rank-2 best 동시 반환 */
            int rank_ad, i1_ad, i13_ad, i2_ad;
            int i1_r1, i2_r1;
            int i1_r2, i13_r2, i2_r2;
            codebook_type1_sp_4port_ri_pmi_select(
                H, N0,
                &rank_ad, &i1_ad, &i13_ad, &i2_ad,
                &i1_r1, &i2_r1,
                &i1_r2, &i13_r2, &i2_r2);
            if (rank_ad == 1) r1_sel_cnt++;

            /* ③ 2개 CW 인코딩 (동일 정보 비트로 3 시나리오 공정 비교) */
            for (int l = 0; l < 2; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                ldpc_encode(&ldpc, tb_crc[l], coded[l]);
                memcpy(txbits[l], coded[l], acsz * sizeof(int));
                for (int i = acsz; i < padsz; i++) txbits[l][i] = 0;
                qam_modulate(txbits[l], padsz, mcs.modulation, sym[l]);
            }

            /* ④ 노이즈 드로우: n[d][r]  (3 시나리오가 동일 노이즈 공유) */
            for (int d = 0; d < nd; d++)
                for (int r = 0; r < 4; r++)
                    nbuf[d * 4 + r] = CX_MAKE(randn() * sigma, randn() * sigma);

            /* ────────────────────────────────────────────────────────────────
             * 시나리오별 유효 채널 계산 + 검출 + 복호 매크로
             *
             * H_eff_r1[4]    : rank-1 유효 채널 (4×1)
             * H_eff_r2[4][2] : rank-2 유효 채널 (4×2)
             * H_eff_ad[4]    : adaptive rank-1 유효 채널 (rank-1 선택 시)
             * H_eff_ad2[4][2]: adaptive rank-2 유효 채널 (rank-2 선택 시)
             * ──────────────────────────────────────────────────────────────── */

            /* === 시나리오 R1-fixed (rank-1, 최적 PMI 고정) === */
            {
                cx_t W[4];
                codebook_type1_sp_4port_rank1(i1_r1, i2_r1, W);
                cx_t h_eff[4];
                for (int r = 0; r < 4; r++) {
                    h_eff[r] = 0.0;
                    for (int t = 0; t < 4; t++) h_eff[r] += H[r][t] * W[t];
                }
                /* y[d][r] = h_eff[r]·sym[0][d] + n[d][r] */
                double nv_sum = 0.0;
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum += nv;
                }
                double env = nv_sum / nd;
                qam_demap_llr(rx_hat[0], nd, mcs.modulation, env, allllr[0]);
                int llen = (acsz < nd * bps) ? acsz : nd * bps;
                memcpy(llr[0], allllr[0], llen * sizeof(double));
                for (int i = llen; i < acsz; i++) llr[0][i] = 0.0;
                ldpc_decode(&ldpc, llr[0], 25, dec[0]);
                int crc_r1 = check_crc(dec[0], K, CRC24A);
                int be_r1 = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != dec[0][i]) be_r1++;
                b_r1 += be_r1;
                t_r1 += tbsz;
                if (!crc_r1 || be_r1 > 0) e_r1++;
            }

            /* === 시나리오 R2-fixed (rank-2, 최적 PMI 고정) === */
            {
                cx_t W2[4][2];
                codebook_type1_sp_4port_rank2(i1_r2, i13_r2, i2_r2, W2);
                cx_t h_eff2[4][2];
                for (int r = 0; r < 4; r++)
                    for (int l = 0; l < 2; l++) {
                        h_eff2[r][l] = 0.0;
                        for (int t = 0; t < 4; t++) h_eff2[r][l] += H[r][t] * W2[t][l];
                    }
                /* y[d][r] = Σ_l h_eff2[r][l]·sym[l][d] + n[d][r] */
                double nv2_sum[2] = {0.0, 0.0};
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff2[r][0] * sym[0][d] + h_eff2[r][1] * sym[1][d]
                               + nbuf[d * 4 + r];
                    cx_t xh2[2]; double nv2[2];
                    mimo_mmse_detect_4rx2(h_eff2, y, N0, xh2, nv2);
                    rx_hat[0][d] = xh2[0];
                    rx_hat[1][d] = xh2[1];
                    nv2_sum[0] += nv2[0];
                    nv2_sum[1] += nv2[1];
                }
                int blk_r2 = 0;
                for (int l = 0; l < 2; l++) {
                    double env = nv2_sum[l] / nd;
                    qam_demap_llr(rx_hat[l], nd, mcs.modulation, env, allllr[l]);
                    int llen = (acsz < nd * bps) ? acsz : nd * bps;
                    memcpy(llr[l], allllr[l], llen * sizeof(double));
                    for (int i = llen; i < acsz; i++) llr[l][i] = 0.0;
                    ldpc_decode(&ldpc, llr[l], 25, dec[l]);
                    int crc_l = check_crc(dec[l], K, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != dec[l][i]) be_l++;
                    b_r2 += be_l;
                    t_r2 += tbsz;
                    if (!crc_l || be_l > 0) blk_r2++;
                }
                if (blk_r2 > 0) e_r2++;
            }

            /* === 시나리오 Adaptive (rank_ad에 따라 분기) === */
            if (rank_ad == 1) {
                cx_t W[4];
                codebook_type1_sp_4port_rank1(i1_ad, i2_ad, W);
                cx_t h_eff[4];
                for (int r = 0; r < 4; r++) {
                    h_eff[r] = 0.0;
                    for (int t = 0; t < 4; t++) h_eff[r] += H[r][t] * W[t];
                }
                double nv_sum = 0.0;
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum += nv;
                }
                double env = nv_sum / nd;
                qam_demap_llr(rx_hat[0], nd, mcs.modulation, env, allllr[0]);
                int llen = (acsz < nd * bps) ? acsz : nd * bps;
                memcpy(llr[0], allllr[0], llen * sizeof(double));
                for (int i = llen; i < acsz; i++) llr[0][i] = 0.0;
                ldpc_decode(&ldpc, llr[0], 25, dec[0]);
                int crc_ad = check_crc(dec[0], K, CRC24A);
                int be_ad = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != dec[0][i]) be_ad++;
                b_ad += be_ad;
                t_ad += tbsz;
                if (!crc_ad || be_ad > 0) e_ad++;

            } else {   /* rank_ad == 2 */
                cx_t W2[4][2];
                codebook_type1_sp_4port_rank2(i1_ad, i13_ad, i2_ad, W2);
                cx_t h_eff2[4][2];
                for (int r = 0; r < 4; r++)
                    for (int l = 0; l < 2; l++) {
                        h_eff2[r][l] = 0.0;
                        for (int t = 0; t < 4; t++) h_eff2[r][l] += H[r][t] * W2[t][l];
                    }
                double nv2_sum[2] = {0.0, 0.0};
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff2[r][0] * sym[0][d] + h_eff2[r][1] * sym[1][d]
                               + nbuf[d * 4 + r];
                    cx_t xh2[2]; double nv2[2];
                    mimo_mmse_detect_4rx2(h_eff2, y, N0, xh2, nv2);
                    rx_hat[0][d] = xh2[0];
                    rx_hat[1][d] = xh2[1];
                    nv2_sum[0] += nv2[0];
                    nv2_sum[1] += nv2[1];
                }
                int blk_ad = 0;
                for (int l = 0; l < 2; l++) {
                    double env = nv2_sum[l] / nd;
                    qam_demap_llr(rx_hat[l], nd, mcs.modulation, env, allllr[l]);
                    int llen = (acsz < nd * bps) ? acsz : nd * bps;
                    memcpy(llr[l], allllr[l], llen * sizeof(double));
                    for (int i = llen; i < acsz; i++) llr[l][i] = 0.0;
                    ldpc_decode(&ldpc, llr[l], 25, dec[l]);
                    int crc_l = check_crc(dec[l], K, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != dec[l][i]) be_l++;
                    b_ad += be_l;
                    t_ad += tbsz;
                    if (!crc_l || be_l > 0) blk_ad++;
                }
                if (blk_ad > 0) e_ad++;
            }

            (void)trial;
        }   /* end trial loop */

        double ber_ad  = t_ad > 0 ? (double)b_ad / t_ad : 0.0;
        double bler_ad = e_ad / (double)cfg->numTrials;
        double ber_r1  = t_r1 > 0 ? (double)b_r1 / t_r1 : 0.0;
        double bler_r1 = e_r1 / (double)cfg->numTrials;
        double ber_r2  = t_r2 > 0 ? (double)b_r2 / t_r2 : 0.0;
        double bler_r2 = e_r2 / (double)cfg->numTrials;
        double r1_rate = r1_sel_cnt * 100.0 / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f  %-12.4e %-11.4f  %.1f\n",
               snr,
               ber_ad, bler_ad,
               ber_r1, bler_r1,
               ber_r2, bler_r2,
               r1_rate);
    }   /* end SNR loop */

    printf("\nPDSCH CL 4-port simulation complete.\n");

    ldpc_free(&ldpc);
    free(nbuf);
    for (int l = 0; l < 2; l++) {
        free(tb[l]); free(tb_crc[l]); free(coded[l]); free(txbits[l]);
        free(sym[l]); free(allllr[l]); free(llr[l]); free(dec[l]);
        free(rx_hat[l]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 4포트 Type I SP 코드북 기반 Closed-Loop PDSCH — TDL 주파수 선택적 페이딩
 *
 * PMI 선택  : Wideband — 모든 data SC의 H[k]를 평균낸 H_avg로 RI+PMI 선택
 *             (3GPP 광대역 PMI 보고 동작과 동일한 원리)
 * 채널 추정 : Genie-aided (tdl_freq_response, 파일럿 오버헤드 없음)
 * Tx 상관   : H[k]마다 mimo_apply_tx_correlation_4x4 적용
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_4port_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int *data_pos = (int *)malloc(num_data * sizeof(int));
    dmrs_data_indices(num_rb, data_pos);

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;
    int K = tbsz + crc_bits;

    LDPCCodec ldpc;
    ldpc_init(&ldpc, K, cr);
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);
    int nsym  = padsz / bps;
    int nd    = (nsym < num_data) ? nsym : num_data;

    printf("=== PDSCH Closed-Loop 4-port Codebook (RI+PMI Adaptive), TDL Fading ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 4 / 4\n");
    printf("Codebook     : TS 38.214 Type I SP (N1=2, O1=4, Ng=2)\n");
    printf("Rank Range   : 1~2  (RI+PMI 자동 선택, Wideband H_avg 기준)\n");
    printf("Tx Corr      : rho=%.2f, rho_xpol=%.2f (%s, Kronecker R_pol(x)R_ant, RX 비상관)\n",
           cfg->spatialCorrTx, cfg->spatialCorrXpol,
           (cfg->spatialCorrTx > 0.0 || cfg->spatialCorrXpol > 0.0) ? "공간상관" : "i.i.d.");
    printf("Channel      : TDL per Tx-Rx pair (16 tap-sets, DS=%.0fns, %d taps, genie-aided)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2], *tb_crc[2], *coded[2], *txbits[2], *dec[2];
    cx_t  *sym[2];
    double *allllr[2], *llr[2];
    cx_t  *rx_hat[2];
    for (int l = 0; l < 2; l++) {
        tb[l]     = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l] = (int    *)malloc(K     * sizeof(int));
        coded[l]  = (int    *)malloc(acsz  * sizeof(int));
        txbits[l] = (int    *)malloc(padsz * sizeof(int));
        sym[l]    = (cx_t  *)malloc(nsym   * sizeof(cx_t));
        allllr[l] = (double *)malloc(padsz * sizeof(double));
        llr[l]    = (double *)malloc(acsz  * sizeof(double));
        dec[l]    = (int    *)malloc(K     * sizeof(int));
        rx_hat[l] = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc(nd * 4 * sizeof(cx_t));

    /* H 캐시: 각 data RE별 상관 적용된 채널 행렬 */
    cx_t (*H_cache)[4][4] = malloc(nd * sizeof(*H_cache));

    cx_t *taps[4][4];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%-9s  %-12s %-11s  %-12s %-11s  %-12s %-11s  %s\n",
           "SNR(dB)", "BER_Adapt", "BLER_Adapt",
           "BER_R1fix", "BLER_R1fix",
           "BER_R2fix", "BLER_R2fix", "R1%");
    for (int i = 0; i < 98; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_ad = 0, b_ad = 0;
        long t_r1 = 0, b_r1 = 0;
        long t_r2 = 0, b_r2 = 0;
        int  e_ad = 0, e_r1 = 0, e_r2 = 0;
        int  r1_sel_cnt = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① TDL 드로우 + H_cache 계산 + H_avg 산출 */
            for (int r = 0; r < 4; r++)
                for (int t = 0; t < 4; t++)
                    tdl_draw(&tdl_ch, taps[r][t]);

            cx_t H_avg[4][4];
            for (int r = 0; r < 4; r++)
                for (int t = 0; t < 4; t++)
                    H_avg[r][t] = CX_ZERO;

            for (int d = 0; d < nd; d++) {
                int k = data_pos[d];
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 4; t++)
                        H_cache[d][r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                /* Tx 상관 적용 */
                mimo_apply_tx_correlation_4x4(H_cache[d], cfg->spatialCorrTx, cfg->spatialCorrXpol);
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 4; t++)
                        H_avg[r][t] += H_cache[d][r][t];
            }
            double inv_nd = 1.0 / nd;
            for (int r = 0; r < 4; r++)
                for (int t = 0; t < 4; t++)
                    H_avg[r][t] *= inv_nd;

            /* ② Wideband RI+PMI 선택 (H_avg 기준) */
            int rank_ad, i1_ad, i13_ad, i2_ad;
            int i1_r1, i2_r1;
            int i1_r2, i13_r2, i2_r2;
            codebook_type1_sp_4port_ri_pmi_select(
                H_avg, N0,
                &rank_ad, &i1_ad, &i13_ad, &i2_ad,
                &i1_r1, &i2_r1,
                &i1_r2, &i13_r2, &i2_r2);
            if (rank_ad == 1) r1_sel_cnt++;

            /* ③ CW 인코딩 */
            for (int l = 0; l < 2; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                ldpc_encode(&ldpc, tb_crc[l], coded[l]);
                memcpy(txbits[l], coded[l], acsz * sizeof(int));
                for (int i = acsz; i < padsz; i++) txbits[l][i] = 0;
                qam_modulate(txbits[l], padsz, mcs.modulation, sym[l]);
            }

            /* ④ 노이즈 드로우 (3 시나리오 공유) */
            for (int d = 0; d < nd; d++)
                for (int r = 0; r < 4; r++)
                    nbuf[d * 4 + r] = CX_MAKE(randn() * sigma, randn() * sigma);

            /* === 시나리오 R1-fixed === */
            {
                cx_t W[4];
                codebook_type1_sp_4port_rank1(i1_r1, i2_r1, W);
                double nv_sum = 0.0;
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4];
                    for (int r = 0; r < 4; r++) {
                        h_eff[r] = CX_ZERO;
                        for (int t = 0; t < 4; t++) h_eff[r] += H_cache[d][r][t] * W[t];
                    }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum += nv;
                }
                double env = nv_sum / nd;
                qam_demap_llr(rx_hat[0], nd, mcs.modulation, env, allllr[0]);
                int llen = (acsz < nd * bps) ? acsz : nd * bps;
                memcpy(llr[0], allllr[0], llen * sizeof(double));
                for (int i = llen; i < acsz; i++) llr[0][i] = 0.0;
                ldpc_decode(&ldpc, llr[0], 25, dec[0]);
                int crc_r1 = check_crc(dec[0], K, CRC24A);
                int be_r1 = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != dec[0][i]) be_r1++;
                b_r1 += be_r1;
                t_r1 += tbsz;
                if (!crc_r1 || be_r1 > 0) e_r1++;
            }

            /* === 시나리오 R2-fixed === */
            {
                cx_t W2[4][2];
                codebook_type1_sp_4port_rank2(i1_r2, i13_r2, i2_r2, W2);
                double nv2_sum[2] = {0.0, 0.0};
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff2[4][2];
                    for (int r = 0; r < 4; r++)
                        for (int l = 0; l < 2; l++) {
                            h_eff2[r][l] = CX_ZERO;
                            for (int t = 0; t < 4; t++) h_eff2[r][l] += H_cache[d][r][t] * W2[t][l];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff2[r][0] * sym[0][d] + h_eff2[r][1] * sym[1][d]
                               + nbuf[d * 4 + r];
                    cx_t xh2[2]; double nv2[2];
                    mimo_mmse_detect_4rx2(h_eff2, y, N0, xh2, nv2);
                    rx_hat[0][d] = xh2[0];
                    rx_hat[1][d] = xh2[1];
                    nv2_sum[0] += nv2[0];
                    nv2_sum[1] += nv2[1];
                }
                int blk_r2 = 0;
                for (int l = 0; l < 2; l++) {
                    double env = nv2_sum[l] / nd;
                    qam_demap_llr(rx_hat[l], nd, mcs.modulation, env, allllr[l]);
                    int llen = (acsz < nd * bps) ? acsz : nd * bps;
                    memcpy(llr[l], allllr[l], llen * sizeof(double));
                    for (int i = llen; i < acsz; i++) llr[l][i] = 0.0;
                    ldpc_decode(&ldpc, llr[l], 25, dec[l]);
                    int crc_l = check_crc(dec[l], K, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != dec[l][i]) be_l++;
                    b_r2 += be_l;
                    t_r2 += tbsz;
                    if (!crc_l || be_l > 0) blk_r2++;
                }
                if (blk_r2 > 0) e_r2++;
            }

            /* === 시나리오 Adaptive === */
            if (rank_ad == 1) {
                cx_t W[4];
                codebook_type1_sp_4port_rank1(i1_ad, i2_ad, W);
                double nv_sum = 0.0;
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4];
                    for (int r = 0; r < 4; r++) {
                        h_eff[r] = CX_ZERO;
                        for (int t = 0; t < 4; t++) h_eff[r] += H_cache[d][r][t] * W[t];
                    }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum += nv;
                }
                double env = nv_sum / nd;
                qam_demap_llr(rx_hat[0], nd, mcs.modulation, env, allllr[0]);
                int llen = (acsz < nd * bps) ? acsz : nd * bps;
                memcpy(llr[0], allllr[0], llen * sizeof(double));
                for (int i = llen; i < acsz; i++) llr[0][i] = 0.0;
                ldpc_decode(&ldpc, llr[0], 25, dec[0]);
                int crc_ad = check_crc(dec[0], K, CRC24A);
                int be_ad = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != dec[0][i]) be_ad++;
                b_ad += be_ad;
                t_ad += tbsz;
                if (!crc_ad || be_ad > 0) e_ad++;
            } else {
                cx_t W2[4][2];
                codebook_type1_sp_4port_rank2(i1_ad, i13_ad, i2_ad, W2);
                double nv2_sum[2] = {0.0, 0.0};
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff2[4][2];
                    for (int r = 0; r < 4; r++)
                        for (int l = 0; l < 2; l++) {
                            h_eff2[r][l] = CX_ZERO;
                            for (int t = 0; t < 4; t++) h_eff2[r][l] += H_cache[d][r][t] * W2[t][l];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff2[r][0] * sym[0][d] + h_eff2[r][1] * sym[1][d]
                               + nbuf[d * 4 + r];
                    cx_t xh2[2]; double nv2[2];
                    mimo_mmse_detect_4rx2(h_eff2, y, N0, xh2, nv2);
                    rx_hat[0][d] = xh2[0];
                    rx_hat[1][d] = xh2[1];
                    nv2_sum[0] += nv2[0];
                    nv2_sum[1] += nv2[1];
                }
                int blk_ad = 0;
                for (int l = 0; l < 2; l++) {
                    double env = nv2_sum[l] / nd;
                    qam_demap_llr(rx_hat[l], nd, mcs.modulation, env, allllr[l]);
                    int llen = (acsz < nd * bps) ? acsz : nd * bps;
                    memcpy(llr[l], allllr[l], llen * sizeof(double));
                    for (int i = llen; i < acsz; i++) llr[l][i] = 0.0;
                    ldpc_decode(&ldpc, llr[l], 25, dec[l]);
                    int crc_l = check_crc(dec[l], K, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != dec[l][i]) be_l++;
                    b_ad += be_l;
                    t_ad += tbsz;
                    if (!crc_l || be_l > 0) blk_ad++;
                }
                if (blk_ad > 0) e_ad++;
            }

            (void)trial;
        }   /* end trial */

        double ber_ad  = t_ad > 0 ? (double)b_ad / t_ad : 0.0;
        double bler_ad = e_ad / (double)cfg->numTrials;
        double ber_r1  = t_r1 > 0 ? (double)b_r1 / t_r1 : 0.0;
        double bler_r1 = e_r1 / (double)cfg->numTrials;
        double ber_r2  = t_r2 > 0 ? (double)b_r2 / t_r2 : 0.0;
        double bler_r2 = e_r2 / (double)cfg->numTrials;
        double r1_rate = r1_sel_cnt * 100.0 / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f  %-12.4e %-11.4f  %.1f\n",
               snr,
               ber_ad, bler_ad,
               ber_r1, bler_r1,
               ber_r2, bler_r2,
               r1_rate);
    }   /* end SNR loop */

    printf("\nPDSCH CL 4-port + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(data_pos);
    free(H_cache);
    free(nbuf);
    for (int l = 0; l < 2; l++) {
        free(tb[l]); free(tb_crc[l]); free(coded[l]); free(txbits[l]);
        free(sym[l]); free(allllr[l]); free(llr[l]); free(dec[l]);
        free(rx_hat[l]);
    }
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 4포트 Type I SP 코드북 기반 Closed-Loop PDSCH — HARQ Circular Buffer
 * 지원 채널: FLAT_FADING / TDL (모두 genie-aided)
 *
 * RI+PMI  : 시도 0에서 H_avg 기반 wideband 선택 후 고정
 *           (5G NR HARQ 재전송은 동일 프리코더 유지)
 * 채널    : 시도마다 재추첨 (시간 다이버시티)
 * CW      : rank=1 → CW[0]만 사용, rank=2 → CW[0]+CW[1] 모두 사용
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_4port_harq_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int *data_pos = (int *)malloc(num_data * sizeof(int));
    dmrs_data_indices(num_rb, data_pos);

    int is_tdl  = (strcmp(cfg->channelModel, "TDL") == 0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr   = get_code_rate(&mcs);
    int    bps  = mcs.modulationOrder;
    int    crc_bits = 24;

    int E    = num_data * bps;
    int tbsz = (int)(E * cr);
    if (tbsz < 1)    tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;
    int K = tbsz + crc_bits;

    LDPCCodec ldpc; ldpc_init(&ldpc, K, HARQ_MOTHER_RATE);
    int ncb = ldpc.coded_size;

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PDSCH CL 4-port (HARQ Circular Buffer %s), %s ===\n",
           is_chase ? "Chase" : "IR",
           is_tdl ? "TDL Frequency-Selective Fading" : "Flat Fading");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d)\n", HARQ_MOTHER_RATE, ncb);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 4 / 4\n");
    printf("Codebook     : TS 38.214 Type I SP (N1=2, O1=4, Ng=2)\n");
    printf("Rank Range   : 1~2  (시도 0에서 H_avg 기반 선택, 이후 고정)\n");
    printf("Tx Corr      : rho=%.2f, rho_xpol=%.2f\n", cfg->spatialCorrTx, cfg->spatialCorrXpol);
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : TDL per Tx-Rx pair (16 tap-sets, DS=%.0fns, %d taps, genie-aided)\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    else
        printf("Channel      : Flat Fading 4x4 (block-flat, redrawn per HARQ attempt, genie-aided)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2], *tb_crc[2], *coded_full[2], *selbits[2], *dec[2];
    cx_t  *sym[2], *rx_hat[2];
    double *allllr[2], *soft_buf[2];
    for (int l = 0; l < 2; l++) {
        tb[l]         = (int   *)malloc(tbsz    * sizeof(int));
        tb_crc[l]     = (int   *)malloc(K       * sizeof(int));
        coded_full[l] = (int   *)malloc(ncb     * sizeof(int));
        selbits[l]    = (int   *)malloc(E       * sizeof(int));
        dec[l]        = (int   *)malloc(K       * sizeof(int));
        sym[l]        = (cx_t  *)malloc(num_data * sizeof(cx_t));
        rx_hat[l]     = (cx_t  *)malloc(num_data * sizeof(cx_t));
        allllr[l]     = (double *)malloc(E      * sizeof(double));
        soft_buf[l]   = (double *)malloc(ncb    * sizeof(double));
    }
    cx_t (*H_cache)[4][4] = malloc(num_data * sizeof(*H_cache));
    cx_t *taps[4][4];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%10s%14s%14s%14s%12s%8s\n",
           "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx", "R1%");
    for (int i = 0; i < 72; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        int total_err=0, total_bits=0, blk_err_final=0, blk_err_1st=0;
        long long total_attempts = 0;
        int r1_sel_cnt = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            for (int l = 0; l < 2; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                ldpc_encode(&ldpc, tb_crc[l], coded_full[l]);
                for (int i = 0; i < ncb; i++) soft_buf[l][i] = 0.0;
            }

            /* RI+PMI 선택: 시도 0에서 결정하여 이후 고정 */
            int rank_fix, i1_fix, i13_fix, i2_fix;
            int _r1i1, _r1i2, _r2i1, _r2i13, _r2i2;  /* 불필요한 출력용 더미 */
            cx_t W_fix[4];       /* rank-1용 */
            cx_t W2_fix[4][2];   /* rank-2용 */

            /* attempt 0 채널을 먼저 그려서 H_avg 계산 → PMI 선택 */
            cx_t H_flat0[4][4];
            if (!is_tdl) {
                mimo_channel_draw_4x4(H_flat0);
                mimo_apply_tx_correlation_4x4(H_flat0, cfg->spatialCorrTx, cfg->spatialCorrXpol);
                codebook_type1_sp_4port_ri_pmi_select(
                    H_flat0, N0,
                    &rank_fix, &i1_fix, &i13_fix, &i2_fix,
                    &_r1i1, &_r1i2, &_r2i1, &_r2i13, &_r2i2);
            } else {
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 4; t++)
                        tdl_draw(&tdl_ch, taps[r][t]);
                cx_t H_avg[4][4];
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 4; t++) H_avg[r][t] = CX_ZERO;
                for (int d = 0; d < num_data; d++) {
                    int k = data_pos[d];
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 4; t++)
                            H_cache[d][r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                    mimo_apply_tx_correlation_4x4(H_cache[d], cfg->spatialCorrTx, cfg->spatialCorrXpol);
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 4; t++)
                            H_avg[r][t] += H_cache[d][r][t];
                }
                double inv = 1.0 / num_data;
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 4; t++) H_avg[r][t] *= inv;
                codebook_type1_sp_4port_ri_pmi_select(
                    H_avg, N0,
                    &rank_fix, &i1_fix, &i13_fix, &i2_fix,
                    &_r1i1, &_r1i2, &_r2i1, &_r2i13, &_r2i2);
            }
            if (rank_fix == 1) {
                r1_sel_cnt++;
                codebook_type1_sp_4port_rank1(i1_fix, i2_fix, W_fix);
            } else {
                codebook_type1_sp_4port_rank2(i1_fix, i13_fix, i2_fix, W2_fix);
            }
            int nCW = rank_fix;   /* rank-1: 1 CW, rank-2: 2 CWs */

            int crc_ok[2]={0,0}, be[2]={0,0}, be_1st[2]={0,0}, crc_1st[2]={0,0};
            int attempts = 0;
            int attempt0_done = 1;   /* 채널은 시도 0에서 이미 그려짐 */

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int l = 0; l < nCW; l++) {
                    rate_match_select(coded_full[l], ncb, rv, E, selbits[l]);
                    qam_modulate(selbits[l], E, mcs.modulation, sym[l]);
                }

                /* 채널: 시도 0은 이미 그려짐, 이후 재추첨 */
                if (attempt > 0 || !attempt0_done) {
                    if (!is_tdl) {
                        mimo_channel_draw_4x4(H_flat0);
                        mimo_apply_tx_correlation_4x4(H_flat0, cfg->spatialCorrTx, cfg->spatialCorrXpol);
                    } else {
                        for (int r = 0; r < 4; r++)
                            for (int t = 0; t < 4; t++)
                                tdl_draw(&tdl_ch, taps[r][t]);
                        for (int d = 0; d < num_data; d++) {
                            int k = data_pos[d];
                            for (int r = 0; r < 4; r++)
                                for (int t = 0; t < 4; t++)
                                    H_cache[d][r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                            mimo_apply_tx_correlation_4x4(H_cache[d], cfg->spatialCorrTx, cfg->spatialCorrXpol);
                        }
                    }
                }
                attempt0_done = 0;   /* 다음 재진입 시 재추첨 트리거 */

                double nv_sum[2] = {0.0, 0.0};

                for (int d = 0; d < num_data; d++) {
                    cx_t tx[2];
                    for (int l = 0; l < nCW; l++) tx[l] = sym[l][d];

                    if (rank_fix == 1) {
                        cx_t h_eff[4];
                        if (!is_tdl) {
                            for (int r = 0; r < 4; r++) {
                                h_eff[r] = CX_ZERO;
                                for (int t = 0; t < 4; t++) h_eff[r] += H_flat0[r][t] * W_fix[t];
                            }
                        } else {
                            for (int r = 0; r < 4; r++) {
                                h_eff[r] = CX_ZERO;
                                for (int t = 0; t < 4; t++) h_eff[r] += H_cache[d][r][t] * W_fix[t];
                            }
                        }
                        cx_t y[4];
                        for (int r = 0; r < 4; r++)
                            y[r] = h_eff[r] * tx[0] + CX_MAKE(randn()*sigma, randn()*sigma);
                        cx_t xh; double nv;
                        mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                        rx_hat[0][d] = xh;
                        nv_sum[0] += nv;
                    } else {
                        cx_t h_eff2[4][2];
                        if (!is_tdl) {
                            for (int r = 0; r < 4; r++)
                                for (int l = 0; l < 2; l++) {
                                    h_eff2[r][l] = CX_ZERO;
                                    for (int t = 0; t < 4; t++) h_eff2[r][l] += H_flat0[r][t] * W2_fix[t][l];
                                }
                        } else {
                            for (int r = 0; r < 4; r++)
                                for (int l = 0; l < 2; l++) {
                                    h_eff2[r][l] = CX_ZERO;
                                    for (int t = 0; t < 4; t++) h_eff2[r][l] += H_cache[d][r][t] * W2_fix[t][l];
                                }
                        }
                        cx_t y[4];
                        for (int r = 0; r < 4; r++)
                            y[r] = h_eff2[r][0]*tx[0] + h_eff2[r][1]*tx[1]
                                   + CX_MAKE(randn()*sigma, randn()*sigma);
                        cx_t xh2[2]; double nv2[2];
                        mimo_mmse_detect_4rx2(h_eff2, y, N0, xh2, nv2);
                        rx_hat[0][d] = xh2[0]; rx_hat[1][d] = xh2[1];
                        nv_sum[0] += nv2[0];   nv_sum[1] += nv2[1];
                    }
                }

                int ml = tbsz < ldpc.info_size - crc_bits ? tbsz : ldpc.info_size - crc_bits;
                if (ml < 0) ml = 0;
                for (int l = 0; l < nCW; l++) {
                    double env = nv_sum[l] / num_data;
                    qam_demap_llr(rx_hat[l], num_data, mcs.modulation, env, allllr[l]);
                    rate_match_combine(soft_buf[l], ncb, rv, E, allllr[l]);
                    ldpc_decode(&ldpc, soft_buf[l], 25, dec[l]);
                    crc_ok[l] = check_crc(dec[l], K, CRC24A);
                    int biterr = 0;
                    for (int i = 0; i < ml; i++)
                        if (tb[l][i] != dec[l][i]) biterr++;
                    be[l] = biterr;
                    if (attempt == 0) { be_1st[l] = biterr; crc_1st[l] = crc_ok[l]; }
                }

                attempts = attempt + 1;
                int all_ok = 1;
                for (int l = 0; l < nCW; l++) if (!crc_ok[l]) { all_ok = 0; break; }
                if (all_ok) break;
            }

            int any_err_1st = 0, any_err_final = 0;
            for (int l = 0; l < nCW; l++) {
                total_err  += be[l];
                total_bits += tbsz;
                if (!crc_1st[l] || be_1st[l] > 0) any_err_1st   = 1;
                if (!crc_ok[l]  || be[l]    > 0) any_err_final = 1;
            }
            if (any_err_1st)   blk_err_1st++;
            if (any_err_final) blk_err_final++;
            total_attempts += attempts;
        }

        double ber       = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double bler_1st  = (double)blk_err_1st   / cfg->numTrials;
        double bler_final= (double)blk_err_final  / cfg->numTrials;
        double avg_tx    = (double)total_attempts / cfg->numTrials;
        double r1_rate   = r1_sel_cnt * 100.0 / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f%8.1f\n",
               snr, ber, bler_1st, bler_final, avg_tx, r1_rate);
    }
    printf("\nPDSCH CL 4-port + %s + HARQ simulation complete.\n",
           is_tdl ? "TDL" : "Flat Fading");

    ldpc_free(&ldpc);
    free(data_pos);
    free(H_cache);
    for (int l = 0; l < 2; l++) {
        free(tb[l]); free(tb_crc[l]); free(coded_full[l]); free(selbits[l]);
        free(dec[l]); free(sym[l]); free(rx_hat[l]);
        free(allllr[l]); free(soft_buf[l]);
    }
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++)
            free(taps[r][t]);
}
