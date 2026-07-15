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

    MCSTableType tbl = (strcmp(cfg->mcsTableType,"TABLE2")==0) ? MCS_TABLE2 : MCS_TABLE1;
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

    MCSTableType tbl = (strcmp(cfg->mcsTableType,"TABLE2")==0) ? MCS_TABLE2 : MCS_TABLE1;
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

    MCSTableType tbl = (strcmp(cfg->mcsTableType,"TABLE2")==0) ? MCS_TABLE2 : MCS_TABLE1;
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
