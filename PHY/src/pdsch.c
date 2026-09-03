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
#include "nr_rate_matching.h"
#include "nr_sch.h"
#include "tdl.h"
#include "codebook.h"
#include "codebook_8port.h"
#include "codebook_32port.h"
#include "eigen_16port.h"
#include "olla.h"
#include "mumimo.h"
#include "beam_mgmt.h"
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
    int B = A + crc_bits;

    /* TS 38.212 5.2.2 code block segmentation (P0-2c, 2026-09-03): unlike
     * every other run_pdsch_ or run_pusch_ function, this pilot no longer
     * clamps A to 8424 -- TB Size can now exceed Kcb and be split into
     * seg.C>1 code blocks, each carrying seg.Kprime bits (incl. its own
     * CRC24B when seg.C>1). All seg.C code blocks share one bg/Zc/Kb (TS
     * 38.212 5.2.2 -- Kb depends on B, not on the per-block size), so a
     * single LDPCCodec instance is reused for all of them. */
    NRSegInfo seg;
    nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;   /* per-block real payload bits, excl. CB CRC --
                                            seg.C*payload == B exactly (nr_seg_compute()) */

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = (int)(A / cr);   /* TS 38.212 5.4.2.1 total rate-matched output
                                    length G for the whole TB -- this legacy
                                    benchmark has no RE grid, so E is derived
                                    from the requested TBS/rate directly
                                    (equivalent role to num_data*bps elsewhere) */
    if (E < 1) E = 1;
    int nsym = (E + bps - 1) / bps;   /* round up to a whole number of QAM symbols */
    E = nsym * bps;

    /* TS 38.212 5.4.2.1 per-code-block E_r (Nl=1: this function has no
     * layer mapping/MIMO) -- code blocks can get slightly different E_r
     * when E isn't an exact multiple of seg.C (floor/ceil split). */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

    printf("=== PDSCH Simulation ===\n");
    printf("MCS Index: %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation: %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate: %.4f (R x 1024 = %.0f)\n", cr, mcs.targetCodeRate);
    printf("TB Size: %d bits\n", A);
    printf("CRC: CRC-24A (%d bits)\n", crc_bits);
    printf("Code Blocks: C=%d (BG%d, Zc=%d, K'=%d bits/CB%s)\n",
           seg.C, seg.bg, seg.Zc, seg.Kprime, seg.C > 1 ? ", CRC24B/CB" : "");
    printf("LDPC Coded Size: %d bits/CB\n", acsz);
    printf("Spectral Efficiency: %.3f bits/RE\n", mcs.spectralEff);
    printf("Trials per SNR: %d\n\n", cfg->numTrials);

    int *tb      = (int *)malloc(A     * sizeof(int));
    int *tb_crc  = (int *)malloc(B     * sizeof(int));
    int *cb_bits = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int **rm     = (int **)malloc(seg.C * sizeof(int *));
    for (int r = 0; r < seg.C; r++) rm[r] = (int *)malloc(Er[r] * sizeof(int));
    int *selbits = (int *)malloc(E     * sizeof(int));
    cx_t *syms   = (cx_t *)malloc(nsym * sizeof(cx_t));
    cx_t *rxsyms = (cx_t *)malloc(nsym * sizeof(cx_t));
    double *allllr   = (double *)malloc(E    * sizeof(double));
    double *soft_buf = (double *)malloc(acsz * sizeof(double));
    int    *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));
    int    *decoded_tb = (int *)malloc(B * sizeof(int));

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
            attach_crc(tb, A, CRC24A, tb_crc);          /* tb_crc[B] */
            nr_seg_split(tb_crc, &seg, cb_bits);        /* cb_bits[C*Kprime], +CRC24B if C>1 */

            for (int r = 0; r < seg.C; r++) {
                const int *info = cb_bits + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, info, coded);
                nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rm[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
            qam_modulate(selbits, E, mcs.modulation, syms);

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

            int roff = 0;
            int cb_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_buf, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllr + roff);
                roff += Er[r];
                ldpc_decode(&ldpc, soft_buf, 25, decoded_cb);
                if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B))
                    cb_crc_ok = 0;
                memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
            }

            int crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);
            int be = 0;
            for (int i=0; i<A; i++) if (tb[i]!=decoded_tb[i]) be++;
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
    free(tb); free(tb_crc); free(cb_bits); free(coded);
    for (int r = 0; r < seg.C; r++) free(rm[r]);
    free(rm); free(Er); free(selbits);
    free(syms); free(rxsyms); free(allllr); free(soft_buf);
    free(decoded_cb); free(decoded_tb);
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

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G for the whole TB */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

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
    int *tb_crc  = (int *)malloc(B     * sizeof(int));
    int *cb_bits = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int **rm     = (int **)malloc(seg.C * sizeof(int *));
    for (int r = 0; r < seg.C; r++) rm[r] = (int *)malloc(Er[r] * sizeof(int));
    int *selbits = (int *)malloc(E     * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *tx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_pilots  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots   = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_full     = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *h_data     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *eq_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    double *allllr   = (double *)malloc(E            * sizeof(double));
    double *soft_buf = (double *)malloc(acsz         * sizeof(double));
    int    *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));
    int    *decoded_tb = (int *)malloc(B * sizeof(int));

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
            nr_seg_split(tb_crc, &seg, cb_bits);
            for (int r = 0; r < seg.C; r++) {
                const int *info = cb_bits + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, info, coded);
                nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rm[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
            qam_modulate(selbits, E, mcs.modulation, data_syms);

            /* build resource grid */
            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<num_data;d++) tx_grid[data_pos[d]] = data_syms[d];

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
                qam_demap_llr_mmse(eq_data, num_data, mcs.modulation, h_data, N0, allllr);
            } else {
                zf_equalize(rx_data, h_data, num_data, eq_data);
                double mhp = 0.0;
                for (int d=0;d<num_data;d++) mhp += CX_NORM(h_data[d]);
                mhp /= num_data;
                double env = (mhp > 1e-10) ? N0/mhp : N0;
                qam_demap_llr(eq_data, num_data, mcs.modulation, env, allllr);
            }
            int roff = 0;
            int cb_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_buf, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllr + roff);
                roff += Er[r];
                ldpc_decode(&ldpc, soft_buf, 25, decoded_cb);
                if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B))
                    cb_crc_ok = 0;
                memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
            }

            int crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);
            int be = 0;
            for (int i=0; i<tbsz; i++) if (tb[i]!=decoded_tb[i]) be++;
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
    free(tb); free(tb_crc); free(cb_bits); free(coded);
    for (int r = 0; r < seg.C; r++) free(rm[r]);
    free(rm); free(Er); free(selbits);
    free(data_syms); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(allllr); free(soft_buf); free(decoded_cb); free(decoded_tb);
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

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G for the whole TB */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

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
    int *tb_crc  = (int *)malloc(B     * sizeof(int));
    int *cb_bits = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int **rm     = (int **)malloc(seg.C * sizeof(int *));
    for (int r = 0; r < seg.C; r++) rm[r] = (int *)malloc(Er[r] * sizeof(int));
    int *selbits = (int *)malloc(E     * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc(num_data     * sizeof(cx_t));
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
    double *allllr   = (double *)malloc(E            * sizeof(double));
    double *soft_buf = (double *)malloc(acsz         * sizeof(double));
    int    *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));
    int    *decoded_tb = (int *)malloc(B * sizeof(int));

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
            nr_seg_split(tb_crc, &seg, cb_bits);
            for (int r = 0; r < seg.C; r++) {
                const int *info = cb_bits + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, info, coded);
                nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rm[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
            qam_modulate(selbits, E, mcs.modulation, data_syms);

            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<num_data;d++) tx_grid[data_pos[d]] = data_syms[d];

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

            qam_demap_llr(eq_data, num_data, mcs.modulation, env, allllr);
            int roff = 0;
            int cb_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_buf, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllr + roff);
                roff += Er[r];
                ldpc_decode(&ldpc, soft_buf, 25, decoded_cb);
                if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B))
                    cb_crc_ok = 0;
                memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
            }

            int crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);
            int be = 0;
            for (int i=0; i<tbsz; i++) if (tb[i]!=decoded_tb[i]) be++;
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
    free(tb); free(tb_crc); free(cb_bits); free(coded);
    for (int r = 0; r < seg.C; r++) free(rm[r]);
    free(rm); free(Er); free(selbits);
    free(data_syms); free(tx_grid); free(rx_grid0); free(rx_grid1);
    free(rx_pilots0); free(rx_pilots1); free(h_pilots0); free(h_pilots1);
    free(h_full0); free(h_full1); free(eq_data);
    free(allllr); free(soft_buf); free(decoded_cb); free(decoded_tb);
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

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G for the whole TB */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

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
    int *tb_crc  = (int *)malloc(B     * sizeof(int));
    int *cb_bits = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int **rm     = (int **)malloc(seg.C * sizeof(int *));
    for (int r = 0; r < seg.C; r++) rm[r] = (int *)malloc(Er[r] * sizeof(int));
    int *selbits = (int *)malloc(E     * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc(num_data     * sizeof(cx_t));
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
    double *allllr   = (double *)malloc(E            * sizeof(double));
    double *soft_buf = (double *)malloc(acsz         * sizeof(double));
    int    *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));
    int    *decoded_tb = (int *)malloc(B * sizeof(int));
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
            nr_seg_split(tb_crc, &seg, cb_bits);
            for (int r = 0; r < seg.C; r++) {
                const int *info = cb_bits + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, info, coded);
                nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rm[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
            qam_modulate(selbits, E, mcs.modulation, data_syms);

            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<num_data;d++) tx_grid[data_pos[d]] = data_syms[d];

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

            qam_demap_llr(eq_data, num_data, mcs.modulation, env, allllr);
            int roff = 0;
            int cb_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_buf, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllr + roff);
                roff += Er[r];
                ldpc_decode(&ldpc, soft_buf, 25, decoded_cb);
                if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B))
                    cb_crc_ok = 0;
                memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
            }

            int crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);
            int be = 0;
            for (int i=0; i<tbsz; i++) if (tb[i]!=decoded_tb[i]) be++;
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
    free(tb); free(tb_crc); free(cb_bits); free(coded);
    for (int r = 0; r < seg.C; r++) free(rm[r]);
    free(rm); free(Er); free(selbits);
    free(data_syms); free(tx_grid); free(rx_grid0); free(rx_grid1);
    free(rx_pilots0); free(rx_pilots1); free(h_pilots0); free(h_pilots1);
    free(h_full0); free(h_full1); free(eq_data);
    free(allllr); free(soft_buf); free(decoded_cb); free(decoded_tb); free(taps0); free(taps1);
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

    /* TS 38.212 5.2.2 multi-code-block segmentation (P0-2c HARQ follow-up,
     * 2026-09-03) -- see run_pdsch_harq_simulation() for the full design
     * note (same pattern: whole-TB retransmission, per-block persistent
     * soft-combining, one shared LDPCCodec for all seg.C blocks). */
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;

    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

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
    printf("Mother Rate  : %.4f (Ncb=%d/CB, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)seg.Kprime / acsz, acsz, ldpc.bg, ldpc.Zc);
    printf("Code Blocks  : C=%d%s\n", seg.C, seg.C > 1 ? " (CRC24B/CB)" : "");
    printf("Num RB       : %d\n", num_rb);
    printf("Rx Antennas  : 2\n");
    printf("TB Size      : %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    printf("Channel      : TDL per Rx branch, redrawn every attempt (DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Combiner     : MRC with per-RE LS channel estimation + interpolation\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb        = (int *)malloc(tbsz * sizeof(int));
    int *tb_crc    = (int *)malloc(B    * sizeof(int));
    int *cb_bits   = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
    int **coded_cw = (int **)malloc(seg.C * sizeof(int *));
    for (int r = 0; r < seg.C; r++) coded_cw[r] = (int *)malloc(acsz * sizeof(int));
    int **rm       = (int **)malloc(seg.C * sizeof(int *));
    for (int r = 0; r < seg.C; r++) rm[r] = (int *)malloc(Er[r] * sizeof(int));
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
    double **soft_buf = (double **)malloc(seg.C * sizeof(double *));
    for (int r = 0; r < seg.C; r++) soft_buf[r] = (double *)malloc(acsz * sizeof(double));
    int    *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));
    int    *decoded_tb = (int *)malloc(B * sizeof(int));
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
            nr_seg_split(tb_crc, &seg, cb_bits);

            for (int r = 0; r < seg.C; r++) {
                const int *info = cb_bits + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, info, coded_cw[r]);
                for (int i=0;i<acsz;i++) soft_buf[r][i] = 0.0;
            }

            int crc_ok = 0, be = 0, attempts = 0;
            int be_1st = 0, crc_ok_1st = 0;

            for (int attempt=0; attempt<max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];
                for (int r = 0; r < seg.C; r++)
                    nr_ldpc_rate_match_select(coded_cw[r], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               rv, Er[r], rm[r]);
                nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
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

                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    nr_ldpc_rate_match_combine(soft_buf[r], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                rv, Er[r], allllr + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[r], 25, decoded_cb);
                    if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
                }
                crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);

                be = 0;
                for (int i=0;i<tbsz;i++) if (tb[i]!=decoded_tb[i]) be++;

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
    free(tb); free(tb_crc); free(cb_bits);
    for (int r = 0; r < seg.C; r++) free(coded_cw[r]);
    free(coded_cw);
    for (int r = 0; r < seg.C; r++) free(rm[r]);
    free(rm); free(Er); free(selbits);
    free(data_syms); free(tx_grid); free(rx_grid0); free(rx_grid1);
    free(rx_pilots0); free(rx_pilots1); free(h_pilots0); free(h_pilots1);
    free(h_full0); free(h_full1); free(eq_data);
    free(allllr);
    for (int r = 0; r < seg.C; r++) free(soft_buf[r]);
    free(soft_buf);
    free(decoded_cb); free(decoded_tb); free(taps0); free(taps1);
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

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by both layers (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);   /* shared by both layers (stateless, const-only use) */
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per layer */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by both layers (same E) */

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
    int *tb_crcA=(int*)malloc(B*sizeof(int)), *tb_crcB=(int*)malloc(B*sizeof(int));
    int *cb_bitsA=(int*)malloc((size_t)seg.C*seg.Kprime*sizeof(int));
    int *cb_bitsB=(int*)malloc((size_t)seg.C*seg.Kprime*sizeof(int));
    int *codedA=(int*)malloc(acsz*sizeof(int)), *codedB=(int*)malloc(acsz*sizeof(int));
    int **rmA=(int**)malloc(seg.C*sizeof(int*)), **rmB=(int**)malloc(seg.C*sizeof(int*));
    for (int r = 0; r < seg.C; r++) { rmA[r]=(int*)malloc(Er[r]*sizeof(int)); rmB[r]=(int*)malloc(Er[r]*sizeof(int)); }
    int *selbitsA=(int*)malloc(E*sizeof(int)), *selbitsB=(int*)malloc(E*sizeof(int));
    cx_t *symsA=(cx_t*)malloc(num_data*sizeof(cx_t)), *symsB=(cx_t*)malloc(num_data*sizeof(cx_t));
    cx_t *tx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *tx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *rx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *rx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *x_hatA=(cx_t*)malloc(num_data*sizeof(cx_t)), *x_hatB=(cx_t*)malloc(num_data*sizeof(cx_t));
    double *allllrA=(double*)malloc(E*sizeof(double)), *allllrB=(double*)malloc(E*sizeof(double));
    double *soft_bufA=(double*)malloc(acsz*sizeof(double)), *soft_bufB=(double*)malloc(acsz*sizeof(double));
    int *decodedA_cb=(int*)malloc(ldpc.info_size*sizeof(int)), *decodedB_cb=(int*)malloc(ldpc.info_size*sizeof(int));
    int *decodedA_tb=(int*)malloc(B*sizeof(int)), *decodedB_tb=(int*)malloc(B*sizeof(int));

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
            nr_seg_split(tb_crcA, &seg, cb_bitsA);
            nr_seg_split(tb_crcB, &seg, cb_bitsB);
            for (int r = 0; r < seg.C; r++) {
                const int *infoA = cb_bitsA + (size_t)r * seg.Kprime;
                const int *infoB = cb_bitsB + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, infoA, codedA);
                ldpc_encode(&ldpc, infoB, codedB);
                nr_ldpc_rate_match_select(codedA, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rmA[r]);
                nr_ldpc_rate_match_select(codedB, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rmB[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rmA, Er, selbitsA);
            nr_seg_concat(&seg, (const int *const *)rmB, Er, selbitsB);
            qam_modulate(selbitsA, E, mcs.modulation, symsA);
            qam_modulate(selbitsB, E, mcs.modulation, symsB);

            /* resource grid: layer-specific pilots, shared data */
            for (int k=0;k<active;k++) { tx_grid0[k]=CX_ZERO; tx_grid1[k]=CX_ZERO; }
            for (int p=0;p<nppl;p++) { tx_grid0[pilotA[p]] = dmrsA[p]; tx_grid1[pilotB[p]] = dmrsB[p]; }
            for (int d=0;d<num_data;d++) { tx_grid0[data_pos[d]] = symsA[d]; tx_grid1[data_pos[d]] = symsB[d]; }

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

            qam_demap_llr(x_hatA, num_data, mcs.modulation, envA, allllrA);
            qam_demap_llr(x_hatB, num_data, mcs.modulation, envB, allllrB);
            int roffA = 0, roffB = 0;
            int cbA_crc_ok = 1, cbB_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_bufA, 0, acsz * sizeof(double));
                memset(soft_bufB, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_bufA, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllrA + roffA);
                nr_ldpc_rate_match_combine(soft_bufB, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllrB + roffB);
                roffA += Er[r]; roffB += Er[r];

                ldpc_decode(&ldpc, soft_bufA, 25, decodedA_cb);
                ldpc_decode(&ldpc, soft_bufB, 25, decodedB_cb);
                if (seg.C > 1 && !check_crc(decodedA_cb, seg.Kprime, CRC24B)) cbA_crc_ok = 0;
                if (seg.C > 1 && !check_crc(decodedB_cb, seg.Kprime, CRC24B)) cbB_crc_ok = 0;
                memcpy(decodedA_tb + (size_t)r * payload, decodedA_cb, payload * sizeof(int));
                memcpy(decodedB_tb + (size_t)r * payload, decodedB_cb, payload * sizeof(int));
            }

            int crcA_ok = cbA_crc_ok && check_crc(decodedA_tb, B, CRC24A);
            int crcB_ok = cbB_crc_ok && check_crc(decodedB_tb, B, CRC24A);
            int beA=0, beB=0;
            for (int i=0;i<tbsz;i++) {
                if (tbA[i]!=decodedA_tb[i]) beA++;
                if (tbB[i]!=decodedB_tb[i]) beB++;
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
    free(cb_bitsA); free(cb_bitsB); free(codedA); free(codedB);
    for (int r = 0; r < seg.C; r++) { free(rmA[r]); free(rmB[r]); }
    free(rmA); free(rmB); free(Er); free(selbitsA); free(selbitsB);
    free(symsA); free(symsB); free(tx_grid0); free(tx_grid1);
    free(rx_grid0); free(rx_grid1); free(x_hatA); free(x_hatB);
    free(allllrA); free(allllrB); free(soft_bufA); free(soft_bufB);
    free(decodedA_cb); free(decodedB_cb); free(decodedA_tb); free(decodedB_tb);
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

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by both layers (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);   /* shared by both layers (stateless, const-only use) */
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per layer */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by both layers (same E) */

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
    int *tb_crcA=(int*)malloc(B*sizeof(int)), *tb_crcB=(int*)malloc(B*sizeof(int));
    int *cb_bitsA=(int*)malloc((size_t)seg.C*seg.Kprime*sizeof(int));
    int *cb_bitsB=(int*)malloc((size_t)seg.C*seg.Kprime*sizeof(int));
    int *codedA=(int*)malloc(acsz*sizeof(int)), *codedB=(int*)malloc(acsz*sizeof(int));
    int **rmA=(int**)malloc(seg.C*sizeof(int*)), **rmB=(int**)malloc(seg.C*sizeof(int*));
    for (int r = 0; r < seg.C; r++) { rmA[r]=(int*)malloc(Er[r]*sizeof(int)); rmB[r]=(int*)malloc(Er[r]*sizeof(int)); }
    int *selbitsA=(int*)malloc(E*sizeof(int)), *selbitsB=(int*)malloc(E*sizeof(int));
    cx_t *symsA=(cx_t*)malloc(num_data*sizeof(cx_t)), *symsB=(cx_t*)malloc(num_data*sizeof(cx_t));
    cx_t *tx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *tx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *rx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *rx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *x_hatA=(cx_t*)malloc(num_data*sizeof(cx_t)), *x_hatB=(cx_t*)malloc(num_data*sizeof(cx_t));
    double *allllrA=(double*)malloc(E*sizeof(double)), *allllrB=(double*)malloc(E*sizeof(double));
    double *soft_bufA=(double*)malloc(acsz*sizeof(double)), *soft_bufB=(double*)malloc(acsz*sizeof(double));
    int *decodedA_cb=(int*)malloc(ldpc.info_size*sizeof(int)), *decodedB_cb=(int*)malloc(ldpc.info_size*sizeof(int));
    int *decodedA_tb=(int*)malloc(B*sizeof(int)), *decodedB_tb=(int*)malloc(B*sizeof(int));

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
            nr_seg_split(tb_crcA, &seg, cb_bitsA);
            nr_seg_split(tb_crcB, &seg, cb_bitsB);
            for (int r = 0; r < seg.C; r++) {
                const int *infoA = cb_bitsA + (size_t)r * seg.Kprime;
                const int *infoB = cb_bitsB + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, infoA, codedA);
                ldpc_encode(&ldpc, infoB, codedB);
                nr_ldpc_rate_match_select(codedA, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rmA[r]);
                nr_ldpc_rate_match_select(codedB, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rmB[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rmA, Er, selbitsA);
            nr_seg_concat(&seg, (const int *const *)rmB, Er, selbitsB);
            qam_modulate(selbitsA, E, mcs.modulation, symsA);
            qam_modulate(selbitsB, E, mcs.modulation, symsB);

            /* resource grid: layer-specific pilots, shared data */
            for (int k=0;k<active;k++) { tx_grid0[k]=CX_ZERO; tx_grid1[k]=CX_ZERO; }
            for (int p=0;p<nppl;p++) { tx_grid0[pilotA[p]] = dmrsA[p]; tx_grid1[pilotB[p]] = dmrsB[p]; }
            for (int d=0;d<num_data;d++) { tx_grid0[data_pos[d]] = symsA[d]; tx_grid1[data_pos[d]] = symsB[d]; }

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

            qam_demap_llr(x_hatA, num_data, mcs.modulation, envA, allllrA);
            qam_demap_llr(x_hatB, num_data, mcs.modulation, envB, allllrB);
            int roffA = 0, roffB = 0;
            int cbA_crc_ok = 1, cbB_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_bufA, 0, acsz * sizeof(double));
                memset(soft_bufB, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_bufA, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllrA + roffA);
                nr_ldpc_rate_match_combine(soft_bufB, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllrB + roffB);
                roffA += Er[r]; roffB += Er[r];

                ldpc_decode(&ldpc, soft_bufA, 25, decodedA_cb);
                ldpc_decode(&ldpc, soft_bufB, 25, decodedB_cb);
                if (seg.C > 1 && !check_crc(decodedA_cb, seg.Kprime, CRC24B)) cbA_crc_ok = 0;
                if (seg.C > 1 && !check_crc(decodedB_cb, seg.Kprime, CRC24B)) cbB_crc_ok = 0;
                memcpy(decodedA_tb + (size_t)r * payload, decodedA_cb, payload * sizeof(int));
                memcpy(decodedB_tb + (size_t)r * payload, decodedB_cb, payload * sizeof(int));
            }

            int crcA_ok = cbA_crc_ok && check_crc(decodedA_tb, B, CRC24A);
            int crcB_ok = cbB_crc_ok && check_crc(decodedB_tb, B, CRC24A);
            int beA=0, beB=0;
            for (int i=0;i<tbsz;i++) {
                if (tbA[i]!=decodedA_tb[i]) beA++;
                if (tbB[i]!=decodedB_tb[i]) beB++;
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
    free(cb_bitsA); free(cb_bitsB); free(codedA); free(codedB);
    for (int r = 0; r < seg.C; r++) { free(rmA[r]); free(rmB[r]); }
    free(rmA); free(rmB); free(Er); free(selbitsA); free(selbitsB);
    free(symsA); free(symsB); free(tx_grid0); free(tx_grid1);
    free(rx_grid0); free(rx_grid1); free(x_hatA); free(x_hatB);
    free(allllrA); free(allllrB); free(soft_bufA); free(soft_bufB);
    free(decodedA_cb); free(decodedB_cb); free(decodedA_tb); free(decodedB_tb);
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

    /* TS 38.212 5.2.2 multi-code-block segmentation (P0-2c HARQ follow-up,
     * 2026-09-03) -- see run_pdsch_harq_simulation() for the full design
     * note. Both independent codewords (A/B) share identical tbsz/cr, so
     * one NRSegInfo/LDPCCodec/Er[] covers both -- but each layer gets its
     * own C code blocks, soft-combining buffers, and whole-codeword
     * (not whole-TB-of-both-layers) retransmission is NOT modeled here:
     * matching the existing single-CB behavior, a retransmission attempt
     * still only stops once BOTH layers' TBs (all of A's and B's code
     * blocks) pass. */
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;

    LDPCCodec ldpc;   /* mother code, shared by both layers' code blocks */
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;

    int *Er = (int *)malloc(seg.C * sizeof(int));   /* identical for A/B: same E */
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

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
    printf("Mother Rate  : %.4f (Ncb=%d/CB, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)seg.Kprime / acsz, acsz, ldpc.bg, ldpc.Zc);
    printf("Code Blocks  : C=%d/layer%s\n", seg.C, seg.C > 1 ? " (CRC24B/CB)" : "");
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
    int *tb_crcA=(int*)malloc(B*sizeof(int)), *tb_crcB=(int*)malloc(B*sizeof(int));
    int *cb_bitsA=(int*)malloc((size_t)seg.C*seg.Kprime*sizeof(int));
    int *cb_bitsB=(int*)malloc((size_t)seg.C*seg.Kprime*sizeof(int));
    int **coded_cwA=(int**)malloc(seg.C*sizeof(int*));
    int **coded_cwB=(int**)malloc(seg.C*sizeof(int*));
    for (int r=0;r<seg.C;r++) { coded_cwA[r]=(int*)malloc(acsz*sizeof(int)); coded_cwB[r]=(int*)malloc(acsz*sizeof(int)); }
    int **rmA=(int**)malloc(seg.C*sizeof(int*)), **rmB=(int**)malloc(seg.C*sizeof(int*));
    for (int r=0;r<seg.C;r++) { rmA[r]=(int*)malloc(Er[r]*sizeof(int)); rmB[r]=(int*)malloc(Er[r]*sizeof(int)); }
    int *selbitsA=(int*)malloc(E*sizeof(int)), *selbitsB=(int*)malloc(E*sizeof(int));
    cx_t *data_symsA=(cx_t*)malloc(num_data*sizeof(cx_t)), *data_symsB=(cx_t*)malloc(num_data*sizeof(cx_t));
    cx_t *tx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *tx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *rx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *rx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *x_hatA=(cx_t*)malloc(num_data*sizeof(cx_t)), *x_hatB=(cx_t*)malloc(num_data*sizeof(cx_t));
    double *allllrA=(double*)malloc(E*sizeof(double)), *allllrB=(double*)malloc(E*sizeof(double));
    double **soft_bufA=(double**)malloc(seg.C*sizeof(double*)), **soft_bufB=(double**)malloc(seg.C*sizeof(double*));
    for (int r=0;r<seg.C;r++) { soft_bufA[r]=(double*)malloc(acsz*sizeof(double)); soft_bufB[r]=(double*)malloc(acsz*sizeof(double)); }
    int *decoded_cbA=(int*)malloc(ldpc.info_size*sizeof(int)), *decoded_cbB=(int*)malloc(ldpc.info_size*sizeof(int));
    int *decoded_tbA=(int*)malloc(B*sizeof(int)), *decoded_tbB=(int*)malloc(B*sizeof(int));

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
            nr_seg_split(tb_crcA, &seg, cb_bitsA);
            nr_seg_split(tb_crcB, &seg, cb_bitsB);
            for (int r=0;r<seg.C;r++) {
                ldpc_encode(&ldpc, cb_bitsA + (size_t)r*seg.Kprime, coded_cwA[r]);
                ldpc_encode(&ldpc, cb_bitsB + (size_t)r*seg.Kprime, coded_cwB[r]);
                for (int i=0;i<acsz;i++) { soft_bufA[r][i] = 0.0; soft_bufB[r][i] = 0.0; }
            }

            int crcA_ok=0, crcB_ok=0, beA=0, beB=0, attempts=0;
            int beA_1st=0, beB_1st=0, crcA_1st=0, crcB_1st=0;

            for (int attempt=0; attempt<max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];
                for (int r=0;r<seg.C;r++) {
                    nr_ldpc_rate_match_select(coded_cwA[r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], rmA[r]);
                    nr_ldpc_rate_match_select(coded_cwB[r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], rmB[r]);
                }
                nr_seg_concat(&seg, (const int *const *)rmA, Er, selbitsA);
                nr_seg_concat(&seg, (const int *const *)rmB, Er, selbitsB);
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

                int roffA = 0, roffB = 0;
                int cbA_crc_ok = 1, cbB_crc_ok = 1;
                for (int r=0;r<seg.C;r++) {
                    nr_ldpc_rate_match_combine(soft_bufA[r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], allllrA + roffA);
                    nr_ldpc_rate_match_combine(soft_bufB[r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], allllrB + roffB);
                    roffA += Er[r]; roffB += Er[r];
                    ldpc_decode(&ldpc, soft_bufA[r], 25, decoded_cbA);
                    ldpc_decode(&ldpc, soft_bufB[r], 25, decoded_cbB);
                    if (seg.C > 1 && !check_crc(decoded_cbA, seg.Kprime, CRC24B)) cbA_crc_ok = 0;
                    if (seg.C > 1 && !check_crc(decoded_cbB, seg.Kprime, CRC24B)) cbB_crc_ok = 0;
                    memcpy(decoded_tbA + (size_t)r*payload, decoded_cbA, payload*sizeof(int));
                    memcpy(decoded_tbB + (size_t)r*payload, decoded_cbB, payload*sizeof(int));
                }
                crcA_ok = cbA_crc_ok && check_crc(decoded_tbA, B, CRC24A);
                crcB_ok = cbB_crc_ok && check_crc(decoded_tbB, B, CRC24A);

                beA=0; beB=0;
                for (int i=0;i<tbsz;i++) {
                    if (tbA[i]!=decoded_tbA[i]) beA++;
                    if (tbB[i]!=decoded_tbB[i]) beB++;
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
    free(cb_bitsA); free(cb_bitsB);
    for (int r=0;r<seg.C;r++) { free(coded_cwA[r]); free(coded_cwB[r]); }
    free(coded_cwA); free(coded_cwB);
    for (int r=0;r<seg.C;r++) { free(rmA[r]); free(rmB[r]); }
    free(rmA); free(rmB); free(Er);
    free(selbitsA); free(selbitsB);
    free(data_symsA); free(data_symsB); free(tx_grid0); free(tx_grid1);
    free(rx_grid0); free(rx_grid1); free(x_hatA); free(x_hatB);
    free(allllrA); free(allllrB);
    for (int r=0;r<seg.C;r++) { free(soft_bufA[r]); free(soft_bufB[r]); }
    free(soft_bufA); free(soft_bufB);
    free(decoded_cbA); free(decoded_cbB); free(decoded_tbA); free(decoded_tbB);
    free(rx_pilotsA0); free(rx_pilotsA1); free(rx_pilotsB0); free(rx_pilotsB1);
    free(h_pilots00); free(h_pilots10); free(h_pilots01); free(h_pilots11);
    free(h00_full); free(h10_full); free(h01_full); free(h11_full);
    free(taps00); free(taps10); free(taps01); free(taps11);
}

/* HARQ with circular-buffer rate matching (Chase / IR), SISO + DMRS.
   A single low-rate mother LDPC codeword is generated per trial; each
   (re)transmission attempt selects E bits via nr_ldpc_rate_match_select()
   (2026-09-02, TS 38.212 5.4.2.1 BG/Zc-aware k0 + filler-skip -- see
   nr_rate_matching.h) at the attempt's RV, sends them over a fresh
   channel realization (models time diversity across HARQ rounds), and
   soft-combines the resulting LLRs into a persistent Ncb-length buffer
   via nr_ldpc_rate_match_combine() before decoding. Reports both the
   1st-transmission-only BLER (as if HARQ were disabled) and the final
   BLER after up to HARQ_MAX_RETX attempts, plus the average number of
   transmissions used. */
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

    /* TS 38.212 5.2.2 multi-code-block segmentation (P0-2c HARQ follow-up,
     * 2026-09-03): 8424-bit clamp removed. B/seg.C code blocks share one
     * bg/Zc/Kb (see nr_sch.h) -> one shared LDPCCodec. Whole-TB HARQ
     * retransmission: every attempt re-sends ALL seg.C code blocks
     * together (this project does not model CBGTI/per-CB retransmission),
     * each with its own persistent soft-combining buffer across attempts. */
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;   /* per-block payload bits, excl. CB CRC */

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;   /* mother codeword size per code block */

    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

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
    printf("Mother Rate  : %.4f (Ncb=%d/CB, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)seg.Kprime / acsz, acsz, ldpc.bg, ldpc.Zc);
    printf("Code Blocks  : C=%d%s\n", seg.C, seg.C > 1 ? " (CRC24B/CB)" : "");
    printf("Num RB       : %d\n", num_rb);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    printf("Channel      : %s\n", use_ray ? "Flat Rayleigh fading" : "AWGN (H=1)");
    printf("Equalizer    : %s with LS channel estimation\n", use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb        = (int *)malloc(tbsz * sizeof(int));
    int *tb_crc    = (int *)malloc(B    * sizeof(int));
    int *cb_bits   = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
    int **coded_cw = (int **)malloc(seg.C * sizeof(int *));   /* per-block mother codeword, encoded once/trial */
    for (int r = 0; r < seg.C; r++) coded_cw[r] = (int *)malloc(acsz * sizeof(int));
    int **rm       = (int **)malloc(seg.C * sizeof(int *));
    for (int r = 0; r < seg.C; r++) rm[r] = (int *)malloc(Er[r] * sizeof(int));
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
    double **soft_buf = (double **)malloc(seg.C * sizeof(double *));   /* persistent per-block soft-combining, across attempts */
    for (int r = 0; r < seg.C; r++) soft_buf[r] = (double *)malloc(acsz * sizeof(double));
    int    *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));   /* per-block decode scratch */
    int    *decoded_tb = (int *)malloc(B * sizeof(int));                /* reassembled TB */

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
            attach_crc(tb, tbsz, CRC24A, tb_crc);          /* tb_crc[B] */
            nr_seg_split(tb_crc, &seg, cb_bits);           /* cb_bits[C*Kprime], +CRC24B if C>1 */

            for (int r = 0; r < seg.C; r++) {
                const int *info = cb_bits + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, info, coded_cw[r]);
                for (int i=0;i<acsz;i++) soft_buf[r][i] = 0.0;
            }

            int crc_ok = 0, be = 0, attempts = 0;
            int be_1st = 0, crc_ok_1st = 0;

            for (int attempt=0; attempt<max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];
                for (int r = 0; r < seg.C; r++)
                    nr_ldpc_rate_match_select(coded_cw[r], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               rv, Er[r], rm[r]);
                nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
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

                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    nr_ldpc_rate_match_combine(soft_buf[r], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                rv, Er[r], allllr + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[r], 25, decoded_cb);
                    if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
                }
                crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);

                be = 0;
                for (int i=0;i<tbsz;i++) if (tb[i]!=decoded_tb[i]) be++;

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
    free(tb); free(tb_crc); free(cb_bits);
    for (int r = 0; r < seg.C; r++) free(coded_cw[r]);
    free(coded_cw);
    for (int r = 0; r < seg.C; r++) free(rm[r]);
    free(rm); free(Er); free(selbits);
    free(data_syms); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(allllr);
    for (int r = 0; r < seg.C; r++) free(soft_buf[r]);
    free(soft_buf);
    free(decoded_cb); free(decoded_tb);
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

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G for the whole TB */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

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
    int *tb_crc  = (int *)malloc(B     * sizeof(int));
    int *cb_bits = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int **rm     = (int **)malloc(seg.C * sizeof(int *));
    for (int r = 0; r < seg.C; r++) rm[r] = (int *)malloc(Er[r] * sizeof(int));
    int *selbits = (int *)malloc(E     * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *tx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_pilots  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots   = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_full     = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *h_data     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *eq_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    double *allllr   = (double *)malloc(E            * sizeof(double));
    double *soft_buf = (double *)malloc(acsz         * sizeof(double));
    int    *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));
    int    *decoded_tb = (int *)malloc(B * sizeof(int));
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
            nr_seg_split(tb_crc, &seg, cb_bits);
            for (int r = 0; r < seg.C; r++) {
                const int *info = cb_bits + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, info, coded);
                nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rm[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
            qam_modulate(selbits, E, mcs.modulation, data_syms);

            /* build resource grid */
            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<num_data;d++) tx_grid[data_pos[d]] = data_syms[d];

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
                qam_demap_llr_mmse(eq_data, num_data, mcs.modulation, h_data, N0, allllr);
            } else {
                zf_equalize(rx_data, h_data, num_data, eq_data);
                double mhp = 0.0;
                for (int d=0;d<num_data;d++) mhp += CX_NORM(h_data[d]);
                mhp /= num_data;
                double env = (mhp > 1e-10) ? N0/mhp : N0;
                qam_demap_llr(eq_data, num_data, mcs.modulation, env, allllr);
            }
            int roff = 0;
            int cb_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_buf, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllr + roff);
                roff += Er[r];
                ldpc_decode(&ldpc, soft_buf, 25, decoded_cb);
                if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B))
                    cb_crc_ok = 0;
                memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
            }

            int crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);
            int be = 0;
            for (int i=0; i<tbsz; i++) if (tb[i]!=decoded_tb[i]) be++;
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
    free(tb); free(tb_crc); free(cb_bits); free(coded);
    for (int r = 0; r < seg.C; r++) free(rm[r]);
    free(rm); free(Er); free(selbits);
    free(data_syms); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(allllr); free(soft_buf); free(decoded_cb); free(decoded_tb); free(taps);
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

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by all NL layers (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per layer */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by all NL layers (same E) */

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
    int   *tb[NL], *tb_crc[NL], *cb_bits[NL], *coded[NL], **rm[NL], *selbits[NL];
    int   *decoded_cb[NL], *decoded_tb[NL];
    cx_t  *syms[NL];
    cx_t  *tx_grid[NL], *rx_grid[NL];
    double *allllr[NL], *soft_buf[NL];
    double nv_sum[NL];
    cx_t  x_hat_re[NL];
    double nv_re[NL];

    for (int l = 0; l < NL; l++) {
        tb[l]          = (int   *)malloc(tbsz * sizeof(int));
        tb_crc[l]      = (int   *)malloc(B    * sizeof(int));
        cb_bits[l]     = (int   *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]       = (int   *)malloc(acsz * sizeof(int));
        rm[l]          = (int  **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l]     = (int   *)malloc(E    * sizeof(int));
        syms[l]        = (cx_t *)malloc(num_data * sizeof(cx_t));
        tx_grid[l]     = (cx_t *)calloc(active, sizeof(cx_t));
        rx_grid[l]     = (cx_t *)malloc(active * sizeof(cx_t));
        allllr[l]      = (double *)malloc(E     * sizeof(double));
        soft_buf[l]    = (double *)malloc(acsz  * sizeof(double));
        decoded_cb[l]  = (int   *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l]  = (int   *)malloc(B * sizeof(int));
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
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[l] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[l]);
                    nr_ldpc_rate_match_select(coded[l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[l][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
                qam_modulate(selbits[l], E, mcs.modulation, syms[l]);
            }

            /* ② 리소스 그리드 구성 */
            for (int l = 0; l < NL; l++) {
                memset(tx_grid[l], 0, active * sizeof(cx_t));
                /* 파일럿: 레이어 l만 자신의 파일럿 위치에 DMRS 송신 */
                for (int p = 0; p < nppl; p++)
                    tx_grid[l][pilotL[l][p]] = dmrsL[l][p];
                /* 데이터: 4개 레이어 모두 동일한 data_pos에 심볼 배치 */
                for (int d = 0; d < num_data; d++)
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

            for (int d = 0; d < num_data; d++) {
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
                double env = nv_sum[l] / num_data;
                qam_demap_llr(syms[l], num_data, mcs.modulation, env, allllr[l]);
            }

            /* ⑥ LDPC 디코딩 + 오류 집계 (코드블록별) */
            int trial_err = 0;
            for (int l = 0; l < NL; l++) {
                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[l], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[l] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                    if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                }
                int crc_ok = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                int bit_err = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[l][i] != decoded_tb[l][i]) bit_err++;
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
    free(pilot_pos); free(data_pos); free(Er);
    for (int l = 0; l < NL; l++) {
        free(pilotL[l]); free(dmrsL[l]);
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(syms[l]); free(tx_grid[l]); free(rx_grid[l]);
        free(allllr[l]); free(soft_buf[l]); free(decoded_cb[l]); free(decoded_tb[l]);
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
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by all NL layers (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per layer */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by all NL layers (same E) */

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

    int   *tb[NL], *tb_crc[NL], *cb_bits[NL], *coded[NL], **rm[NL], *selbits[NL];
    int   *decoded_cb[NL], *decoded_tb[NL];
    cx_t  *syms[NL];
    double *allllr[NL], *soft_buf[NL];
    for (int l = 0; l < NL; l++) {
        tb[l]          = (int   *)malloc(tbsz  * sizeof(int));
        tb_crc[l]      = (int   *)malloc(B     * sizeof(int));
        cb_bits[l]     = (int   *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]       = (int   *)malloc(acsz  * sizeof(int));
        rm[l]          = (int  **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l]     = (int   *)malloc(E     * sizeof(int));
        syms[l]        = (cx_t  *)malloc(num_data * sizeof(cx_t));
        allllr[l]      = (double *)malloc(E      * sizeof(double));
        soft_buf[l]    = (double *)malloc(acsz   * sizeof(double));
        decoded_cb[l]  = (int   *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l]  = (int   *)malloc(B * sizeof(int));
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
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[l] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[l]);
                    nr_ldpc_rate_match_select(coded[l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[l][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
                qam_modulate(selbits[l], E, mcs.modulation, syms[l]);
            }

            /* ② TDL: 16 tap-set draw */
            for (int r = 0; r < NL; r++)
                for (int t = 0; t < NL; t++)
                    tdl_draw(&tdl_ch, taps[r][t]);

            /* ③ 검출 + LLR (genie-aided: 데이터 RE마다 진짜 H 사용) */
            double nv_sum[NL]; for (int l = 0; l < NL; l++) nv_sum[l] = 0.0;

            for (int d = 0; d < num_data; d++) {
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
                double env = nv_sum[l] / num_data;
                qam_demap_llr(syms[l], num_data, mcs.modulation, env, allllr[l]);
            }

            /* ④ LDPC 디코딩 (코드블록별) */
            int trial_err = 0;
            for (int l = 0; l < NL; l++) {
                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[l], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[l] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                    if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                }
                int crc_ok = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                int bit_err = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[l][i] != decoded_tb[l][i]) bit_err++;
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
    free(data_pos); free(Er);
    for (int l = 0; l < NL; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(syms[l]); free(allllr[l]); free(soft_buf[l]); free(decoded_cb[l]); free(decoded_tb[l]);
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
 * HARQ      : nr_ldpc_rate_match_select / nr_ldpc_rate_match_combine (SM_2X2 TDL HARQ와 동일 구조, 2026-09-02 TS 38.212 5.4.2.1 표준 k0로 갱신)
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

    /* TS 38.212 5.2.2 multi-code-block segmentation (P0-2c HARQ follow-up,
     * 2026-09-03) -- see run_pdsch_harq_simulation() design note. All NL=4
     * independent codewords share identical tbsz/cr -> one NRSegInfo/
     * LDPCCodec/Er[] covers all of them; each layer still gets its own C
     * code blocks and persistent soft-combining buffers. */
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;

    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* identical across layers: same E */

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
    printf("Mother Rate  : %.4f (Ncb=%d/CB, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)seg.Kprime / acsz, acsz, ldpc.bg, ldpc.Zc);
    printf("Code Blocks  : C=%d/layer%s\n", seg.C, seg.C > 1 ? " (CRC24B/CB)" : "");
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

    int   *tb[NL], *tb_crc[NL], *cb_bits[NL], **coded_cw[NL], **rm[NL], *selbits[NL];
    int   *decoded_cb[NL], *decoded_tb[NL];
    cx_t  *data_syms[NL], *x_hat[NL];
    double *allllr[NL], **soft_buf[NL];
    for (int l = 0; l < NL; l++) {
        tb[l]         = (int *)malloc(tbsz    * sizeof(int));
        tb_crc[l]     = (int *)malloc(B       * sizeof(int));
        cb_bits[l]    = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded_cw[l]   = (int **)malloc(seg.C  * sizeof(int *));
        rm[l]         = (int **)malloc(seg.C  * sizeof(int *));
        for (int r = 0; r < seg.C; r++) {
            coded_cw[l][r] = (int *)malloc(acsz   * sizeof(int));
            rm[l][r]       = (int *)malloc(Er[r]  * sizeof(int));
        }
        selbits[l]    = (int *)malloc(E       * sizeof(int));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B       * sizeof(int));
        data_syms[l]  = (cx_t *)malloc(num_data * sizeof(cx_t));
        x_hat[l]      = (cx_t *)malloc(num_data * sizeof(cx_t));
        allllr[l]     = (double *)malloc(E    * sizeof(double));
        soft_buf[l]   = (double **)malloc(seg.C * sizeof(double *));
        for (int r = 0; r < seg.C; r++) soft_buf[l][r] = (double *)malloc(acsz * sizeof(double));
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
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    ldpc_encode(&ldpc, cb_bits[l] + (size_t)r * seg.Kprime, coded_cw[l][r]);
                    for (int i = 0; i < acsz; i++) soft_buf[l][r][i] = 0.0;
                }
            }

            int crc_ok[NL], be[NL], be_1st[NL], crc_1st[NL];
            for (int l = 0; l < NL; l++) { crc_ok[l]=0; be[l]=0; be_1st[l]=0; crc_1st[l]=0; }
            int attempts = 0;

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int l = 0; l < NL; l++) {
                    for (int r = 0; r < seg.C; r++)
                        nr_ldpc_rate_match_select(coded_cw[l][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], rm[l][r]);
                    nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
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

                for (int l = 0; l < NL; l++) {
                    double env = nv_sum[l] / num_data;
                    qam_demap_llr(x_hat[l], num_data, mcs.modulation, env, allllr[l]);

                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        nr_ldpc_rate_match_combine(soft_buf[l][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l][r], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B)) cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    crc_ok[l] = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);

                    int biterr = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) biterr++;
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
    free(data_pos); free(Er);
    for (int l = 0; l < NL; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]);
        for (int r = 0; r < seg.C; r++) { free(coded_cw[l][r]); free(rm[l][r]); free(soft_buf[l][r]); }
        free(coded_cw[l]); free(rm[l]); free(soft_buf[l]);
        free(selbits[l]); free(decoded_cb[l]); free(decoded_tb[l]);
        free(data_syms[l]); free(x_hat[l]);
        free(allllr[l]);
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

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by both CWs (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per CW */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by both CWs (same E) */

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
    printf("Data RE/CW   : %d  (Genie-aided CSI, pilot 오버헤드 없음)\n", num_data);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    /* 코드워드 버퍼 (최대 2 CW) */
    int   *tb[2], *tb_crc[2], *cb_bits[2], *coded[2], **rm[2], *selbits[2];
    int   *decoded_cb[2], *decoded_tb[2];
    cx_t  *sym[2];
    double *allllr[2], *soft_buf[2];
    cx_t  *rx_hat[2];   /* 검출된 심볼 (LLR 계산용) */
    for (int l = 0; l < 2; l++) {
        tb[l]      = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l]  = (int    *)malloc(B     * sizeof(int));
        cb_bits[l] = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]   = (int    *)malloc(acsz  * sizeof(int));
        rm[l]      = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l] = (int    *)malloc(E     * sizeof(int));
        sym[l]     = (cx_t  *)malloc(num_data * sizeof(cx_t));
        allllr[l]  = (double *)malloc(E      * sizeof(double));
        soft_buf[l]= (double *)malloc(acsz   * sizeof(double));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B * sizeof(int));
        rx_hat[l]  = (cx_t  *)malloc(num_data * sizeof(cx_t));
    }
    /* 노이즈 버퍼: num_data RE × 4 Rx 안테나 */
    cx_t *nbuf = (cx_t *)malloc(num_data * 4 * sizeof(cx_t));

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
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[l] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[l]);
                    nr_ldpc_rate_match_select(coded[l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[l][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
                qam_modulate(selbits[l], E, mcs.modulation, sym[l]);
            }

            /* ④ 노이즈 드로우: n[d][r]  (3 시나리오가 동일 노이즈 공유) */
            for (int d = 0; d < num_data; d++)
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
                for (int d = 0; d < num_data; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum += nv;
                }
                double env = nv_sum / num_data;
                qam_demap_llr(rx_hat[0], num_data, mcs.modulation, env, allllr[0]);
                int roff0 = 0;
                int cb0_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[0], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[0], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[0] + roff0);
                    roff0 += Er[r];
                    ldpc_decode(&ldpc, soft_buf[0], 25, decoded_cb[0]);
                    if (seg.C > 1 && !check_crc(decoded_cb[0], seg.Kprime, CRC24B))
                        cb0_crc_ok = 0;
                    memcpy(decoded_tb[0] + (size_t)r * payload, decoded_cb[0], payload * sizeof(int));
                }
                int crc_r1 = cb0_crc_ok && check_crc(decoded_tb[0], B, CRC24A);
                int be_r1 = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != decoded_tb[0][i]) be_r1++;
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
                for (int d = 0; d < num_data; d++) {
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
                    double env = nv2_sum[l] / num_data;
                    qam_demap_llr(rx_hat[l], num_data, mcs.modulation, env, allllr[l]);
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        memset(soft_buf[l], 0, acsz * sizeof(double));
                        nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                    /*rv=*/0, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                            cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    int crc_l = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) be_l++;
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
                for (int d = 0; d < num_data; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum += nv;
                }
                double env = nv_sum / num_data;
                qam_demap_llr(rx_hat[0], num_data, mcs.modulation, env, allllr[0]);
                int roff0 = 0;
                int cb0_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[0], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[0], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[0] + roff0);
                    roff0 += Er[r];
                    ldpc_decode(&ldpc, soft_buf[0], 25, decoded_cb[0]);
                    if (seg.C > 1 && !check_crc(decoded_cb[0], seg.Kprime, CRC24B))
                        cb0_crc_ok = 0;
                    memcpy(decoded_tb[0] + (size_t)r * payload, decoded_cb[0], payload * sizeof(int));
                }
                int crc_ad = cb0_crc_ok && check_crc(decoded_tb[0], B, CRC24A);
                int be_ad = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != decoded_tb[0][i]) be_ad++;
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
                for (int d = 0; d < num_data; d++) {
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
                    double env = nv2_sum[l] / num_data;
                    qam_demap_llr(rx_hat[l], num_data, mcs.modulation, env, allllr[l]);
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        memset(soft_buf[l], 0, acsz * sizeof(double));
                        nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                    /*rv=*/0, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                            cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    int crc_l = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) be_l++;
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
    free(nbuf); free(Er);
    for (int l = 0; l < 2; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(sym[l]); free(allllr[l]); free(soft_buf[l]);
        free(decoded_cb[l]); free(decoded_tb[l]);
        free(rx_hat[l]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 8포트 Type I SP 코드북 기반 Closed-Loop PDSCH — 평탄 페이딩 (i.i.d.)
 *
 * run_pdsch_cl_4port_simulation()과 동일한 구조를 8 Tx 포트로 확장.
 * codebook_8port.c(N1=4,O1=4,P=8)의 rank-1/2 프리코더와 RI+PMI 선택기를
 * 사용. Rx는 4안테나 고정(4-port 버전과 동일 관례) — 프리코딩 이후의
 * 유효 채널이 항상 4×rank이므로 mrc_combine_4rx/mimo_mmse_detect_4rx2를
 * 그대로 재사용 가능(Tx 포트 수에 무관한 함수).
 *
 * 채널      : i.i.d. Rayleigh (mimo_channel_draw_4x8) + Tx 공간상관
 *             (mimo_apply_tx_correlation_4x8, Kronecker R_pol(x)R_ant,
 *             N1=4 지수상관 모델 — mimo.h 문서 참조)
 * 채널 추정 : Genie-aided (파일럿 오버헤드 없음, 4-port와 동일 관례)
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_8port_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by both CWs (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per CW */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by both CWs (same E) */

    printf("=== PDSCH Closed-Loop 8-port Codebook (RI+PMI Adaptive) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 8 / 4\n");
    printf("Codebook     : TS 38.214 Type I SP (N1=4, O1=4, Ng=2)\n");
    printf("Rank Range   : 1~2  (RI+PMI 자동 선택, 추정 용량 기준)\n");
    printf("Tx Corr      : rho=%.2f, rho_xpol=%.2f (%s, Kronecker R_pol(x)R_ant N1=4, RX 비상관)\n",
           cfg->spatialCorrTx, cfg->spatialCorrXpol,
           (cfg->spatialCorrTx > 0.0 || cfg->spatialCorrXpol > 0.0) ? "공간상관" : "i.i.d.");
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d  (Genie-aided CSI, pilot 오버헤드 없음)\n", num_data);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2], *tb_crc[2], *cb_bits[2], *coded[2], **rm[2], *selbits[2];
    int   *decoded_cb[2], *decoded_tb[2];
    cx_t  *sym[2];
    double *allllr[2], *soft_buf[2];
    cx_t  *rx_hat[2];
    for (int l = 0; l < 2; l++) {
        tb[l]      = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l]  = (int    *)malloc(B     * sizeof(int));
        cb_bits[l] = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]   = (int    *)malloc(acsz  * sizeof(int));
        rm[l]      = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l] = (int    *)malloc(E     * sizeof(int));
        sym[l]     = (cx_t  *)malloc(num_data * sizeof(cx_t));
        allllr[l]  = (double *)malloc(E      * sizeof(double));
        soft_buf[l]= (double *)malloc(acsz   * sizeof(double));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B * sizeof(int));
        rx_hat[l]  = (cx_t  *)malloc(num_data * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc(num_data * 4 * sizeof(cx_t));

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

            /* ① 채널 드로우: H[4][8], i.i.d. Rayleigh */
            cx_t H[4][8];
            mimo_channel_draw_4x8(H);
            mimo_apply_tx_correlation_4x8(H, cfg->spatialCorrTx, cfg->spatialCorrXpol);

            /* ② RI+PMI 선택 (320 후보 전수 탐색) */
            int rank_ad, i1_ad, i13_ad, i2_ad;
            int i1_r1, i2_r1;
            int i1_r2, i13_r2, i2_r2;
            codebook_type1_sp_8port_ri_pmi_select(
                H, N0,
                &rank_ad, &i1_ad, &i13_ad, &i2_ad,
                &i1_r1, &i2_r1,
                &i1_r2, &i13_r2, &i2_r2);
            if (rank_ad == 1) r1_sel_cnt++;

            /* ③ 2개 CW 인코딩 */
            for (int l = 0; l < 2; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[l] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[l]);
                    nr_ldpc_rate_match_select(coded[l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[l][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
                qam_modulate(selbits[l], E, mcs.modulation, sym[l]);
            }

            /* ④ 노이즈 드로우 */
            for (int d = 0; d < num_data; d++)
                for (int r = 0; r < 4; r++)
                    nbuf[d * 4 + r] = CX_MAKE(randn() * sigma, randn() * sigma);

            /* === 시나리오 R1-fixed === */
            {
                cx_t W[8];
                codebook_type1_sp_8port_rank1(i1_r1, i2_r1, W);
                cx_t h_eff[4];
                for (int r = 0; r < 4; r++) {
                    h_eff[r] = 0.0;
                    for (int t = 0; t < 8; t++) h_eff[r] += H[r][t] * W[t];
                }
                double nv_sum = 0.0;
                for (int d = 0; d < num_data; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum += nv;
                }
                double env = nv_sum / num_data;
                qam_demap_llr(rx_hat[0], num_data, mcs.modulation, env, allllr[0]);
                int roff0 = 0;
                int cb0_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[0], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[0], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[0] + roff0);
                    roff0 += Er[r];
                    ldpc_decode(&ldpc, soft_buf[0], 25, decoded_cb[0]);
                    if (seg.C > 1 && !check_crc(decoded_cb[0], seg.Kprime, CRC24B))
                        cb0_crc_ok = 0;
                    memcpy(decoded_tb[0] + (size_t)r * payload, decoded_cb[0], payload * sizeof(int));
                }
                int crc_r1 = cb0_crc_ok && check_crc(decoded_tb[0], B, CRC24A);
                int be_r1 = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != decoded_tb[0][i]) be_r1++;
                b_r1 += be_r1;
                t_r1 += tbsz;
                if (!crc_r1 || be_r1 > 0) e_r1++;
            }

            /* === 시나리오 R2-fixed === */
            {
                cx_t W2[8][2];
                codebook_type1_sp_8port_rank2(i1_r2, i13_r2, i2_r2, W2);
                cx_t h_eff2[4][2];
                for (int r = 0; r < 4; r++)
                    for (int l = 0; l < 2; l++) {
                        h_eff2[r][l] = 0.0;
                        for (int t = 0; t < 8; t++) h_eff2[r][l] += H[r][t] * W2[t][l];
                    }
                double nv2_sum[2] = {0.0, 0.0};
                for (int d = 0; d < num_data; d++) {
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
                    double env = nv2_sum[l] / num_data;
                    qam_demap_llr(rx_hat[l], num_data, mcs.modulation, env, allllr[l]);
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        memset(soft_buf[l], 0, acsz * sizeof(double));
                        nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                    /*rv=*/0, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                            cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    int crc_l = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) be_l++;
                    b_r2 += be_l;
                    t_r2 += tbsz;
                    if (!crc_l || be_l > 0) blk_r2++;
                }
                if (blk_r2 > 0) e_r2++;
            }

            /* === 시나리오 Adaptive === */
            if (rank_ad == 1) {
                cx_t W[8];
                codebook_type1_sp_8port_rank1(i1_ad, i2_ad, W);
                cx_t h_eff[4];
                for (int r = 0; r < 4; r++) {
                    h_eff[r] = 0.0;
                    for (int t = 0; t < 8; t++) h_eff[r] += H[r][t] * W[t];
                }
                double nv_sum = 0.0;
                for (int d = 0; d < num_data; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum += nv;
                }
                double env = nv_sum / num_data;
                qam_demap_llr(rx_hat[0], num_data, mcs.modulation, env, allllr[0]);
                int roff0 = 0;
                int cb0_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[0], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[0], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[0] + roff0);
                    roff0 += Er[r];
                    ldpc_decode(&ldpc, soft_buf[0], 25, decoded_cb[0]);
                    if (seg.C > 1 && !check_crc(decoded_cb[0], seg.Kprime, CRC24B))
                        cb0_crc_ok = 0;
                    memcpy(decoded_tb[0] + (size_t)r * payload, decoded_cb[0], payload * sizeof(int));
                }
                int crc_ad = cb0_crc_ok && check_crc(decoded_tb[0], B, CRC24A);
                int be_ad = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != decoded_tb[0][i]) be_ad++;
                b_ad += be_ad;
                t_ad += tbsz;
                if (!crc_ad || be_ad > 0) e_ad++;

            } else {
                cx_t W2[8][2];
                codebook_type1_sp_8port_rank2(i1_ad, i13_ad, i2_ad, W2);
                cx_t h_eff2[4][2];
                for (int r = 0; r < 4; r++)
                    for (int l = 0; l < 2; l++) {
                        h_eff2[r][l] = 0.0;
                        for (int t = 0; t < 8; t++) h_eff2[r][l] += H[r][t] * W2[t][l];
                    }
                double nv2_sum[2] = {0.0, 0.0};
                for (int d = 0; d < num_data; d++) {
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
                    double env = nv2_sum[l] / num_data;
                    qam_demap_llr(rx_hat[l], num_data, mcs.modulation, env, allllr[l]);
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        memset(soft_buf[l], 0, acsz * sizeof(double));
                        nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                    /*rv=*/0, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                            cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    int crc_l = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) be_l++;
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

    printf("\nPDSCH CL 8-port simulation complete.\n");

    ldpc_free(&ldpc);
    free(nbuf); free(Er);
    for (int l = 0; l < 2; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(sym[l]); free(allllr[l]); free(soft_buf[l]);
        free(decoded_cb[l]); free(decoded_tb[l]);
        free(rx_hat[l]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 4포트 Type I SP 코드북 기반 Closed-Loop PDSCH — TDL 주파수 선택적 페이딩
 *
 * PMI 선택  : Wideband — 모든 data SC의 H[k]를 평균낸 H_avg로 RI+PMI 선택
 *             (3GPP 광대역 PMI 보고 동작과 동일한 원리)
 * 채널 추정 : CHAN_EST_METHOD=NONE(genie, 기본값, 기존 동작과 완전히 동일)
 *             /LS/MMSE/DFT — EIGEN_16PORT TDL에서 만든 채널추정 체인을
 *             재사용. NONE이 아니면 파일럿(comb-2 DMRS 패턴) 위치에서
 *             노이즈 섞인 관측치를 만들어(Tx 상관도 파일럿에 동일하게
 *             적용) LS/MMSE/DFT로 추정한 뒤 wideband 평균 — 실제 하향링크
 *             전송은 항상 RE별 진짜 H_cache(genie)로 통과(불일치가 잔여
 *             간섭으로 나타남, EIGEN_16PORT TDL과 동일 철학).
 * Tx 상관   : H[k]마다 mimo_apply_tx_correlation_4x4 적용 (데이터·파일럿 공통)
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_4port_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int num_pilots = 6 * num_rb;
    int num_active = 12 * num_rb;
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    dmrs_data_indices(num_rb, data_pos);
    dmrs_pilot_indices(num_rb, pilot_pos);

    int est_none = (strcmp(cfg->chanEstMethod, "NONE") == 0);
    int est_ls   = (strcmp(cfg->chanEstMethod, "LS")   == 0);
    int est_mmse = (strcmp(cfg->chanEstMethod, "MMSE") == 0);
    int est_dft  = (strcmp(cfg->chanEstMethod, "DFT")  == 0);

    int dft_num_taps = 0;
    double scs_hz = (double)cfg->scsKHz * 1000.0;
    if (est_dft) {
        double t_sample = 1.0 / ((double)num_active * scs_hz);
        dft_num_taps = (int)ceil(8.0 * cfg->tdlDelaySpreadNs * 1e-9 / t_sample);
        if (dft_num_taps < 2) dft_num_taps = 2;
        if (dft_num_taps > num_active) dft_num_taps = num_active;
    }

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;
    int B = tbsz + crc_bits;

    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by both CWs (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per CW */
    int nd   = num_data;         /* always the full RE budget now that E=num_data*bps
                                     (was min(nsym,num_data) under the old truncation) */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by both CWs (same E) */

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
    printf("Chan Est     : %s\n", cfg->chanEstMethod);
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2], *tb_crc[2], *cb_bits[2], *coded[2], **rm[2], *selbits[2];
    int   *decoded_cb[2], *decoded_tb[2];
    cx_t  *sym[2];
    double *allllr[2], *soft_buf[2];
    cx_t  *rx_hat[2];
    for (int l = 0; l < 2; l++) {
        tb[l]      = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l]  = (int    *)malloc(B     * sizeof(int));
        cb_bits[l] = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]   = (int    *)malloc(acsz  * sizeof(int));
        rm[l]      = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l] = (int    *)malloc(E     * sizeof(int));
        sym[l]     = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[l]  = (double *)malloc(E     * sizeof(double));
        soft_buf[l]= (double *)malloc(acsz  * sizeof(double));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B * sizeof(int));
        rx_hat[l]  = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc(nd * 4 * sizeof(cx_t));

    /* H 캐시: 각 data RE별 상관 적용된 채널 행렬 */
    cx_t (*H_cache)[4][4] = malloc(nd * sizeof(*H_cache));

    cx_t *taps[4][4];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    /* 채널추정용 버퍼: 파일럿 위치별 4×4 상관채널 + (r,t) 쌍별 파일럿 시퀀스 */
    cx_t (*Hp_full)[4][4] = est_none ? NULL : malloc(num_pilots * sizeof(*Hp_full));
    cx_t *h_ls_pilot  = est_none ? NULL : (cx_t *)malloc(num_pilots * sizeof(cx_t));

    /* DFT 채널추정 사전계산(EIGEN_16PORT TDL과 동일한 "선형 임펄스 응답"
     * 기법 — Tx 공간상관은 이 선형함수와 무관, w_dft는 그대로 재사용 가능). */
    cx_t *w_dft = NULL;
    if (est_dft) {
        w_dft = (cx_t *)malloc(num_pilots * sizeof(cx_t));
        cx_t *basis    = (cx_t *)malloc(num_pilots * sizeof(cx_t));
        cx_t *full_tmp = (cx_t *)malloc(num_active * sizeof(cx_t));
        cx_t *dft_tmp  = (cx_t *)malloc(num_active * sizeof(cx_t));
        for (int p = 0; p < num_pilots; p++) {
            for (int q = 0; q < num_pilots; q++) basis[q] = (p == q) ? 1.0 : 0.0;
            interpolate_channel(basis, num_pilots, pilot_pos, num_active, full_tmp);
            dft_channel_estimate(full_tmp, num_active, dft_num_taps, dft_tmp);
            cx_t s = CX_ZERO;
            for (int d = 0; d < num_data; d++) s += dft_tmp[data_pos[d]];
            w_dft[p] = s / (double)num_data;
        }
        free(basis); free(full_tmp); free(dft_tmp);
    }

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

        cx_t *mmse_W = NULL;
        if (est_mmse) {
            mmse_W = (cx_t *)malloc((size_t)num_pilots * sizeof(cx_t));
            mmse_build_avg_filter(pilot_pos, num_pilots, data_pos, nd,
                                   cfg->tdlDelaySpreadNs, scs_hz, N0, mmse_W);
        }

        long t_ad = 0, b_ad = 0;
        long t_r1 = 0, b_r1 = 0;
        long t_r2 = 0, b_r2 = 0;
        int  e_ad = 0, e_r1 = 0, e_r2 = 0;
        int  r1_sel_cnt = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① TDL 드로우 + H_cache(RE별 진짜 채널) 계산 */
            for (int r = 0; r < 4; r++)
                for (int t = 0; t < 4; t++)
                    tdl_draw(&tdl_ch, taps[r][t]);

            for (int d = 0; d < nd; d++) {
                int k = data_pos[d];
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 4; t++)
                        H_cache[d][r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                mimo_apply_tx_correlation_4x4(H_cache[d], cfg->spatialCorrTx, cfg->spatialCorrXpol);
            }

            /* ② 프리코더 설계용 wideband 채널 (H_avg) — genie 또는 파일럿 기반 추정 */
            cx_t H_avg[4][4];
            if (est_none) {
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 4; t++) {
                        cx_t s = CX_ZERO;
                        for (int d = 0; d < nd; d++) s += H_cache[d][r][t];
                        H_avg[r][t] = s / (double)nd;
                    }
            } else {
                /* 파일럿 위치의 진짜(상관 적용) 채널 + 노이즈 → LS 관측치 */
                for (int p = 0; p < num_pilots; p++) {
                    int k = pilot_pos[p];
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 4; t++)
                            Hp_full[p][r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                    mimo_apply_tx_correlation_4x4(Hp_full[p], cfg->spatialCorrTx, cfg->spatialCorrXpol);
                }
                for (int r = 0; r < 4; r++) {
                    for (int t = 0; t < 4; t++) {
                        for (int p = 0; p < num_pilots; p++)
                            h_ls_pilot[p] = Hp_full[p][r][t] + CX_MAKE(randn() * sigma, randn() * sigma);

                        if (est_ls) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += h_ls_pilot[p];
                            H_avg[r][t] = s / (double)num_pilots;
                        } else if (est_mmse) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += mmse_W[p] * h_ls_pilot[p];
                            H_avg[r][t] = s;
                        } else {   /* est_dft */
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += w_dft[p] * h_ls_pilot[p];
                            H_avg[r][t] = s;
                        }
                    }
                }
            }

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
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[l] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[l]);
                    nr_ldpc_rate_match_select(coded[l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[l][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
                qam_modulate(selbits[l], E, mcs.modulation, sym[l]);
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
                int roff0 = 0;
                int cb0_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[0], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[0], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[0] + roff0);
                    roff0 += Er[r];
                    ldpc_decode(&ldpc, soft_buf[0], 25, decoded_cb[0]);
                    if (seg.C > 1 && !check_crc(decoded_cb[0], seg.Kprime, CRC24B))
                        cb0_crc_ok = 0;
                    memcpy(decoded_tb[0] + (size_t)r * payload, decoded_cb[0], payload * sizeof(int));
                }
                int crc_r1 = cb0_crc_ok && check_crc(decoded_tb[0], B, CRC24A);
                int be_r1 = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != decoded_tb[0][i]) be_r1++;
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
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        memset(soft_buf[l], 0, acsz * sizeof(double));
                        nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                    /*rv=*/0, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                            cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    int crc_l = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) be_l++;
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
                int roff0 = 0;
                int cb0_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[0], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[0], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[0] + roff0);
                    roff0 += Er[r];
                    ldpc_decode(&ldpc, soft_buf[0], 25, decoded_cb[0]);
                    if (seg.C > 1 && !check_crc(decoded_cb[0], seg.Kprime, CRC24B))
                        cb0_crc_ok = 0;
                    memcpy(decoded_tb[0] + (size_t)r * payload, decoded_cb[0], payload * sizeof(int));
                }
                int crc_ad = cb0_crc_ok && check_crc(decoded_tb[0], B, CRC24A);
                int be_ad = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != decoded_tb[0][i]) be_ad++;
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
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        memset(soft_buf[l], 0, acsz * sizeof(double));
                        nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                    /*rv=*/0, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                            cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    int crc_l = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) be_l++;
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

        if (mmse_W) free(mmse_W);
    }   /* end SNR loop */

    printf("\nPDSCH CL 4-port + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(data_pos); free(pilot_pos);
    free(H_cache);
    free(nbuf); free(Er);
    if (Hp_full) free(Hp_full);
    if (h_ls_pilot) free(h_ls_pilot);
    if (w_dft) free(w_dft);
    for (int l = 0; l < 2; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(sym[l]); free(allllr[l]); free(soft_buf[l]);
        free(decoded_cb[l]); free(decoded_tb[l]);
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

    /* TS 38.212 5.2.2 multi-code-block segmentation (P0-2c HARQ follow-up,
     * 2026-09-03) -- see run_pdsch_harq_simulation() design note. Both CWs
     * share identical tbsz/cr -> one NRSegInfo/LDPCCodec/Er[] for both. */
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;

    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

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
    printf("Mother Rate  : %.4f (Ncb=%d/CB, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)seg.Kprime / acsz, acsz, ldpc.bg, ldpc.Zc);
    printf("Code Blocks  : C=%d/CW%s\n", seg.C, seg.C > 1 ? " (CRC24B/CB)" : "");
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

    int   *tb[2], *tb_crc[2], *cb_bits[2], **coded_cw[2], **rm[2], *selbits[2];
    int   *decoded_cb[2], *decoded_tb[2];
    cx_t  *sym[2], *rx_hat[2];
    double *allllr[2], **soft_buf[2];
    for (int l = 0; l < 2; l++) {
        tb[l]         = (int   *)malloc(tbsz    * sizeof(int));
        tb_crc[l]     = (int   *)malloc(B       * sizeof(int));
        cb_bits[l]    = (int   *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded_cw[l]   = (int  **)malloc(seg.C   * sizeof(int *));
        rm[l]         = (int  **)malloc(seg.C   * sizeof(int *));
        for (int r = 0; r < seg.C; r++) {
            coded_cw[l][r] = (int *)malloc(acsz  * sizeof(int));
            rm[l][r]       = (int *)malloc(Er[r] * sizeof(int));
        }
        selbits[l]    = (int   *)malloc(E       * sizeof(int));
        decoded_cb[l] = (int   *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int   *)malloc(B       * sizeof(int));
        sym[l]        = (cx_t  *)malloc(num_data * sizeof(cx_t));
        rx_hat[l]     = (cx_t  *)malloc(num_data * sizeof(cx_t));
        allllr[l]     = (double *)malloc(E      * sizeof(double));
        soft_buf[l]   = (double **)malloc(seg.C * sizeof(double *));
        for (int r = 0; r < seg.C; r++) soft_buf[l][r] = (double *)malloc(acsz * sizeof(double));
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
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    ldpc_encode(&ldpc, cb_bits[l] + (size_t)r * seg.Kprime, coded_cw[l][r]);
                    for (int i = 0; i < acsz; i++) soft_buf[l][r][i] = 0.0;
                }
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
                    for (int r = 0; r < seg.C; r++)
                        nr_ldpc_rate_match_select(coded_cw[l][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], rm[l][r]);
                    nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
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

                for (int l = 0; l < nCW; l++) {
                    double env = nv_sum[l] / num_data;
                    qam_demap_llr(rx_hat[l], num_data, mcs.modulation, env, allllr[l]);
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        nr_ldpc_rate_match_combine(soft_buf[l][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l][r], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B)) cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    crc_ok[l] = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int biterr = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) biterr++;
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
    free(Er);
    for (int l = 0; l < 2; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]);
        for (int r = 0; r < seg.C; r++) { free(coded_cw[l][r]); free(rm[l][r]); free(soft_buf[l][r]); }
        free(coded_cw[l]); free(rm[l]); free(soft_buf[l]);
        free(selbits[l]); free(decoded_cb[l]); free(decoded_tb[l]);
        free(sym[l]); free(rx_hat[l]);
        free(allllr[l]);
    }
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 8포트 Type I SP 코드북 기반 Closed-Loop PDSCH — TDL 주파수 선택적 페이딩
 *
 * 4-port TDL 버전(run_pdsch_cl_4port_tdl_simulation) 구조를 8 Tx 포트로 확장.
 * PMI 선택  : Wideband — 모든 data SC의 H[k]를 평균낸 H_avg로 RI+PMI 선택
 * 채널 추정 : CHAN_EST_METHOD=NONE(genie, 기본값)/LS/MMSE/DFT — 4-port TDL과
 *             동일한 채널추정 체인(파일럿=comb-2 DMRS, Tx 상관도 파일럿에
 *             동일 적용). NONE이 아니면 wideband PMI 설계용 H_avg만 추정치로
 *             바뀌고 실제 하향링크는 항상 RE별 진짜 H_cache로 통과.
 * Tx 상관   : mimo_apply_tx_correlation_4x8, RE별 H_cache[d]에 적용 후 H_avg
 *             누적(4-port와 동일 순서) — N1=4 지수상관 모델, mimo.h 문서 참조
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_8port_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int num_pilots = 6 * num_rb;
    int num_active = 12 * num_rb;
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    dmrs_data_indices(num_rb, data_pos);
    dmrs_pilot_indices(num_rb, pilot_pos);

    int est_none = (strcmp(cfg->chanEstMethod, "NONE") == 0);
    int est_ls   = (strcmp(cfg->chanEstMethod, "LS")   == 0);
    int est_mmse = (strcmp(cfg->chanEstMethod, "MMSE") == 0);
    int est_dft  = (strcmp(cfg->chanEstMethod, "DFT")  == 0);

    double scs_hz = (double)cfg->scsKHz * 1000.0;
    int dft_num_taps = 0;
    if (est_dft) {
        double t_sample = 1.0 / ((double)num_active * scs_hz);
        dft_num_taps = (int)ceil(8.0 * cfg->tdlDelaySpreadNs * 1e-9 / t_sample);
        if (dft_num_taps < 2) dft_num_taps = 2;
        if (dft_num_taps > num_active) dft_num_taps = num_active;
    }

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;
    int B = tbsz + crc_bits;

    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by both CWs (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per CW */
    int nd   = num_data;         /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by both CWs (same E) */

    printf("=== PDSCH Closed-Loop 8-port Codebook (RI+PMI Adaptive), TDL Fading ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 8 / 4\n");
    printf("Codebook     : TS 38.214 Type I SP (N1=4, O1=4, Ng=2)\n");
    printf("Rank Range   : 1~2  (RI+PMI 자동 선택, Wideband H_avg 기준)\n");
    printf("Tx Corr      : rho=%.2f, rho_xpol=%.2f (%s, Kronecker R_pol(x)R_ant N1=4, RX 비상관)\n",
           cfg->spatialCorrTx, cfg->spatialCorrXpol,
           (cfg->spatialCorrTx > 0.0 || cfg->spatialCorrXpol > 0.0) ? "공간상관" : "i.i.d.");
    printf("Channel      : TDL per Tx-Rx pair (32 tap-sets, DS=%.0fns, %d taps, genie-aided)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Chan Est     : %s\n", cfg->chanEstMethod);
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2], *tb_crc[2], *cb_bits[2], *coded[2], **rm[2], *selbits[2];
    int   *decoded_cb[2], *decoded_tb[2];
    cx_t  *sym[2];
    double *allllr[2], *soft_buf[2];
    cx_t  *rx_hat[2];
    for (int l = 0; l < 2; l++) {
        tb[l]      = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l]  = (int    *)malloc(B     * sizeof(int));
        cb_bits[l] = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]   = (int    *)malloc(acsz  * sizeof(int));
        rm[l]      = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l] = (int    *)malloc(E     * sizeof(int));
        sym[l]     = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[l]  = (double *)malloc(E     * sizeof(double));
        soft_buf[l]= (double *)malloc(acsz  * sizeof(double));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B * sizeof(int));
        rx_hat[l]  = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc(nd * 4 * sizeof(cx_t));

    /* H 캐시: 각 data RE별 채널 행렬 (4Rx x 8Tx) */
    cx_t (*H_cache)[8] = malloc(nd * 4 * sizeof(*H_cache));

    cx_t *taps[4][8];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 8; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    /* 채널추정용 버퍼: 파일럿 위치별 4×8 상관채널 + (r,t) 쌍별 파일럿 시퀀스 */
    cx_t (*Hp_full)[8] = est_none ? NULL : malloc(num_pilots * 4 * sizeof(*Hp_full));
    cx_t *h_ls_pilot  = est_none ? NULL : (cx_t *)malloc(num_pilots * sizeof(cx_t));

    cx_t *w_dft = NULL;
    if (est_dft) {
        w_dft = (cx_t *)malloc(num_pilots * sizeof(cx_t));
        cx_t *basis    = (cx_t *)malloc(num_pilots * sizeof(cx_t));
        cx_t *full_tmp = (cx_t *)malloc(num_active * sizeof(cx_t));
        cx_t *dft_tmp  = (cx_t *)malloc(num_active * sizeof(cx_t));
        for (int p = 0; p < num_pilots; p++) {
            for (int q = 0; q < num_pilots; q++) basis[q] = (p == q) ? 1.0 : 0.0;
            interpolate_channel(basis, num_pilots, pilot_pos, num_active, full_tmp);
            dft_channel_estimate(full_tmp, num_active, dft_num_taps, dft_tmp);
            cx_t s = CX_ZERO;
            for (int d = 0; d < num_data; d++) s += dft_tmp[data_pos[d]];
            w_dft[p] = s / (double)num_data;
        }
        free(basis); free(full_tmp); free(dft_tmp);
    }

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

        cx_t *mmse_W = NULL;
        if (est_mmse) {
            mmse_W = (cx_t *)malloc((size_t)num_pilots * sizeof(cx_t));
            mmse_build_avg_filter(pilot_pos, num_pilots, data_pos, nd,
                                   cfg->tdlDelaySpreadNs, scs_hz, N0, mmse_W);
        }

        long t_ad = 0, b_ad = 0;
        long t_r1 = 0, b_r1 = 0;
        long t_r2 = 0, b_r2 = 0;
        int  e_ad = 0, e_r1 = 0, e_r2 = 0;
        int  r1_sel_cnt = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① TDL 드로우 + H_cache(RE별 진짜 채널) 계산 */
            for (int r = 0; r < 4; r++)
                for (int t = 0; t < 8; t++)
                    tdl_draw(&tdl_ch, taps[r][t]);

            for (int d = 0; d < nd; d++) {
                int k = data_pos[d];
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 8; t++)
                        H_cache[d * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                mimo_apply_tx_correlation_4x8(&H_cache[d * 4], cfg->spatialCorrTx, cfg->spatialCorrXpol);
            }

            /* ② 프리코더 설계용 wideband 채널 (H_avg) — genie 또는 파일럿 기반 추정 */
            cx_t H_avg[4][8];
            if (est_none) {
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 8; t++) {
                        cx_t s = CX_ZERO;
                        for (int d = 0; d < nd; d++) s += H_cache[d * 4 + r][t];
                        H_avg[r][t] = s / (double)nd;
                    }
            } else {
                for (int p = 0; p < num_pilots; p++) {
                    int k = pilot_pos[p];
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 8; t++)
                            Hp_full[p * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                    mimo_apply_tx_correlation_4x8(&Hp_full[p * 4], cfg->spatialCorrTx, cfg->spatialCorrXpol);
                }
                for (int r = 0; r < 4; r++) {
                    for (int t = 0; t < 8; t++) {
                        for (int p = 0; p < num_pilots; p++)
                            h_ls_pilot[p] = Hp_full[p * 4 + r][t] + CX_MAKE(randn() * sigma, randn() * sigma);

                        if (est_ls) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += h_ls_pilot[p];
                            H_avg[r][t] = s / (double)num_pilots;
                        } else if (est_mmse) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += mmse_W[p] * h_ls_pilot[p];
                            H_avg[r][t] = s;
                        } else {   /* est_dft */
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += w_dft[p] * h_ls_pilot[p];
                            H_avg[r][t] = s;
                        }
                    }
                }
            }

            /* ③ Wideband RI+PMI 선택 (H_avg 기준, 320 후보 전수 탐색) */
            int rank_ad, i1_ad, i13_ad, i2_ad;
            int i1_r1, i2_r1;
            int i1_r2, i13_r2, i2_r2;
            codebook_type1_sp_8port_ri_pmi_select(
                H_avg, N0,
                &rank_ad, &i1_ad, &i13_ad, &i2_ad,
                &i1_r1, &i2_r1,
                &i1_r2, &i13_r2, &i2_r2);
            if (rank_ad == 1) r1_sel_cnt++;

            /* ③ CW 인코딩 */
            for (int l = 0; l < 2; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[l] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[l]);
                    nr_ldpc_rate_match_select(coded[l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[l][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
                qam_modulate(selbits[l], E, mcs.modulation, sym[l]);
            }

            /* ④ 노이즈 드로우 (3 시나리오 공유) */
            for (int d = 0; d < nd; d++)
                for (int r = 0; r < 4; r++)
                    nbuf[d * 4 + r] = CX_MAKE(randn() * sigma, randn() * sigma);

            /* === 시나리오 R1-fixed === */
            {
                cx_t W[8];
                codebook_type1_sp_8port_rank1(i1_r1, i2_r1, W);
                double nv_sum = 0.0;
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4];
                    for (int r = 0; r < 4; r++) {
                        h_eff[r] = CX_ZERO;
                        for (int t = 0; t < 8; t++) h_eff[r] += H_cache[d * 4 + r][t] * W[t];
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
                int roff0 = 0;
                int cb0_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[0], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[0], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[0] + roff0);
                    roff0 += Er[r];
                    ldpc_decode(&ldpc, soft_buf[0], 25, decoded_cb[0]);
                    if (seg.C > 1 && !check_crc(decoded_cb[0], seg.Kprime, CRC24B))
                        cb0_crc_ok = 0;
                    memcpy(decoded_tb[0] + (size_t)r * payload, decoded_cb[0], payload * sizeof(int));
                }
                int crc_r1 = cb0_crc_ok && check_crc(decoded_tb[0], B, CRC24A);
                int be_r1 = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != decoded_tb[0][i]) be_r1++;
                b_r1 += be_r1;
                t_r1 += tbsz;
                if (!crc_r1 || be_r1 > 0) e_r1++;
            }

            /* === 시나리오 R2-fixed === */
            {
                cx_t W2[8][2];
                codebook_type1_sp_8port_rank2(i1_r2, i13_r2, i2_r2, W2);
                double nv2_sum[2] = {0.0, 0.0};
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff2[4][2];
                    for (int r = 0; r < 4; r++)
                        for (int l = 0; l < 2; l++) {
                            h_eff2[r][l] = CX_ZERO;
                            for (int t = 0; t < 8; t++) h_eff2[r][l] += H_cache[d * 4 + r][t] * W2[t][l];
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
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        memset(soft_buf[l], 0, acsz * sizeof(double));
                        nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                    /*rv=*/0, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                            cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    int crc_l = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) be_l++;
                    b_r2 += be_l;
                    t_r2 += tbsz;
                    if (!crc_l || be_l > 0) blk_r2++;
                }
                if (blk_r2 > 0) e_r2++;
            }

            /* === 시나리오 Adaptive === */
            if (rank_ad == 1) {
                cx_t W[8];
                codebook_type1_sp_8port_rank1(i1_ad, i2_ad, W);
                double nv_sum = 0.0;
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4];
                    for (int r = 0; r < 4; r++) {
                        h_eff[r] = CX_ZERO;
                        for (int t = 0; t < 8; t++) h_eff[r] += H_cache[d * 4 + r][t] * W[t];
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
                int roff0 = 0;
                int cb0_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[0], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[0], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[0] + roff0);
                    roff0 += Er[r];
                    ldpc_decode(&ldpc, soft_buf[0], 25, decoded_cb[0]);
                    if (seg.C > 1 && !check_crc(decoded_cb[0], seg.Kprime, CRC24B))
                        cb0_crc_ok = 0;
                    memcpy(decoded_tb[0] + (size_t)r * payload, decoded_cb[0], payload * sizeof(int));
                }
                int crc_ad = cb0_crc_ok && check_crc(decoded_tb[0], B, CRC24A);
                int be_ad = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[0][i] != decoded_tb[0][i]) be_ad++;
                b_ad += be_ad;
                t_ad += tbsz;
                if (!crc_ad || be_ad > 0) e_ad++;
            } else {
                cx_t W2[8][2];
                codebook_type1_sp_8port_rank2(i1_ad, i13_ad, i2_ad, W2);
                double nv2_sum[2] = {0.0, 0.0};
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff2[4][2];
                    for (int r = 0; r < 4; r++)
                        for (int l = 0; l < 2; l++) {
                            h_eff2[r][l] = CX_ZERO;
                            for (int t = 0; t < 8; t++) h_eff2[r][l] += H_cache[d * 4 + r][t] * W2[t][l];
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
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        memset(soft_buf[l], 0, acsz * sizeof(double));
                        nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                    /*rv=*/0, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B))
                            cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    int crc_l = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int be_l = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) be_l++;
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

        if (mmse_W) free(mmse_W);
    }   /* end SNR loop */

    printf("\nPDSCH CL 8-port + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(data_pos); free(pilot_pos);
    free(H_cache);
    free(nbuf); free(Er);
    if (Hp_full) free(Hp_full);
    if (h_ls_pilot) free(h_ls_pilot);
    if (w_dft) free(w_dft);
    for (int l = 0; l < 2; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(sym[l]); free(allllr[l]); free(soft_buf[l]);
        free(decoded_cb[l]); free(decoded_tb[l]);
        free(rx_hat[l]);
    }
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 8; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 8포트 Type I SP 코드북 기반 Closed-Loop PDSCH — HARQ Circular Buffer
 * 지원 채널: FLAT_FADING / TDL (모두 genie-aided)
 *
 * 4-port HARQ 버전(run_pdsch_cl_4port_harq_simulation) 구조를 8 Tx 포트로 확장.
 * RI+PMI  : 시도 0에서 H_avg 기반 wideband 선택 후 고정
 * 채널    : 시도마다 재추첨 (시간 다이버시티)
 * CW      : rank=1 → CW[0]만 사용, rank=2 → CW[0]+CW[1] 모두 사용
 * Tx 상관 : mimo_apply_tx_correlation_4x8 — FLAT은 매 시도 H_flat0에, TDL은
 *           매 시도 RE별 H_cache[d]에 적용(위 TDL 버전과 동일 방식)
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_8port_harq_simulation(const L1Config *cfg) {
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

    /* TS 38.212 5.2.2 multi-code-block segmentation (P0-2c HARQ follow-up,
     * 2026-09-03) -- see run_pdsch_harq_simulation() design note. Both CWs
     * share identical tbsz/cr -> one NRSegInfo/LDPCCodec/Er[] for both. */
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;

    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PDSCH CL 8-port (HARQ Circular Buffer %s), %s ===\n",
           is_chase ? "Chase" : "IR",
           is_tdl ? "TDL Frequency-Selective Fading" : "Flat Fading");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d/CB, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)seg.Kprime / acsz, acsz, ldpc.bg, ldpc.Zc);
    printf("Code Blocks  : C=%d/CW%s\n", seg.C, seg.C > 1 ? " (CRC24B/CB)" : "");
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 8 / 4\n");
    printf("Codebook     : TS 38.214 Type I SP (N1=4, O1=4, Ng=2)\n");
    printf("Rank Range   : 1~2  (시도 0에서 H_avg 기반 선택, 이후 고정)\n");
    printf("Tx Corr      : rho=%.2f, rho_xpol=%.2f (%s, Kronecker R_pol(x)R_ant N1=4, RX 비상관)\n",
           cfg->spatialCorrTx, cfg->spatialCorrXpol,
           (cfg->spatialCorrTx > 0.0 || cfg->spatialCorrXpol > 0.0) ? "공간상관" : "i.i.d.");
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : TDL per Tx-Rx pair (32 tap-sets, DS=%.0fns, %d taps, genie-aided)\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    else
        printf("Channel      : Flat Fading 4x8 (block-flat, redrawn per HARQ attempt, genie-aided)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2], *tb_crc[2], *cb_bits[2], **coded_cw[2], **rm[2], *selbits[2];
    int   *decoded_cb[2], *decoded_tb[2];
    cx_t  *sym[2], *rx_hat[2];
    double *allllr[2], **soft_buf[2];
    for (int l = 0; l < 2; l++) {
        tb[l]         = (int   *)malloc(tbsz    * sizeof(int));
        tb_crc[l]     = (int   *)malloc(B       * sizeof(int));
        cb_bits[l]    = (int   *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded_cw[l]   = (int  **)malloc(seg.C   * sizeof(int *));
        rm[l]         = (int  **)malloc(seg.C   * sizeof(int *));
        for (int r = 0; r < seg.C; r++) {
            coded_cw[l][r] = (int *)malloc(acsz  * sizeof(int));
            rm[l][r]       = (int *)malloc(Er[r] * sizeof(int));
        }
        selbits[l]    = (int   *)malloc(E       * sizeof(int));
        decoded_cb[l] = (int   *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int   *)malloc(B       * sizeof(int));
        sym[l]        = (cx_t  *)malloc(num_data * sizeof(cx_t));
        rx_hat[l]     = (cx_t  *)malloc(num_data * sizeof(cx_t));
        allllr[l]     = (double *)malloc(E      * sizeof(double));
        soft_buf[l]   = (double **)malloc(seg.C * sizeof(double *));
        for (int r = 0; r < seg.C; r++) soft_buf[l][r] = (double *)malloc(acsz * sizeof(double));
    }
    cx_t (*H_cache)[8] = malloc(num_data * 4 * sizeof(*H_cache));
    cx_t *taps[4][8];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 8; t++)
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
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    ldpc_encode(&ldpc, cb_bits[l] + (size_t)r * seg.Kprime, coded_cw[l][r]);
                    for (int i = 0; i < acsz; i++) soft_buf[l][r][i] = 0.0;
                }
            }

            /* RI+PMI 선택: 시도 0에서 결정하여 이후 고정 */
            int rank_fix, i1_fix, i13_fix, i2_fix;
            int _r1i1, _r1i2, _r2i1, _r2i13, _r2i2;  /* 불필요한 출력용 더미 */
            cx_t W_fix[8];       /* rank-1용 */
            cx_t W2_fix[8][2];   /* rank-2용 */

            /* attempt 0 채널을 먼저 그려서 H_avg 계산 → PMI 선택 */
            cx_t H_flat0[4][8];
            if (!is_tdl) {
                mimo_channel_draw_4x8(H_flat0);
                mimo_apply_tx_correlation_4x8(H_flat0, cfg->spatialCorrTx, cfg->spatialCorrXpol);
                codebook_type1_sp_8port_ri_pmi_select(
                    H_flat0, N0,
                    &rank_fix, &i1_fix, &i13_fix, &i2_fix,
                    &_r1i1, &_r1i2, &_r2i1, &_r2i13, &_r2i2);
            } else {
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 8; t++)
                        tdl_draw(&tdl_ch, taps[r][t]);
                cx_t H_avg[4][8];
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 8; t++) H_avg[r][t] = CX_ZERO;
                for (int d = 0; d < num_data; d++) {
                    int k = data_pos[d];
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 8; t++)
                            H_cache[d * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                    mimo_apply_tx_correlation_4x8(&H_cache[d * 4], cfg->spatialCorrTx, cfg->spatialCorrXpol);
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 8; t++)
                            H_avg[r][t] += H_cache[d * 4 + r][t];
                }
                double inv = 1.0 / num_data;
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 8; t++) H_avg[r][t] *= inv;
                codebook_type1_sp_8port_ri_pmi_select(
                    H_avg, N0,
                    &rank_fix, &i1_fix, &i13_fix, &i2_fix,
                    &_r1i1, &_r1i2, &_r2i1, &_r2i13, &_r2i2);
            }
            if (rank_fix == 1) {
                r1_sel_cnt++;
                codebook_type1_sp_8port_rank1(i1_fix, i2_fix, W_fix);
            } else {
                codebook_type1_sp_8port_rank2(i1_fix, i13_fix, i2_fix, W2_fix);
            }
            int nCW = rank_fix;   /* rank-1: 1 CW, rank-2: 2 CWs */

            int crc_ok[2]={0,0}, be[2]={0,0}, be_1st[2]={0,0}, crc_1st[2]={0,0};
            int attempts = 0;
            int attempt0_done = 1;   /* 채널은 시도 0에서 이미 그려짐 */

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int l = 0; l < nCW; l++) {
                    for (int r = 0; r < seg.C; r++)
                        nr_ldpc_rate_match_select(coded_cw[l][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], rm[l][r]);
                    nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
                    qam_modulate(selbits[l], E, mcs.modulation, sym[l]);
                }

                /* 채널: 시도 0은 이미 그려짐, 이후 재추첨 */
                if (attempt > 0 || !attempt0_done) {
                    if (!is_tdl) {
                        mimo_channel_draw_4x8(H_flat0);
                        mimo_apply_tx_correlation_4x8(H_flat0, cfg->spatialCorrTx, cfg->spatialCorrXpol);
                    } else {
                        for (int r = 0; r < 4; r++)
                            for (int t = 0; t < 8; t++)
                                tdl_draw(&tdl_ch, taps[r][t]);
                        for (int d = 0; d < num_data; d++) {
                            int k = data_pos[d];
                            for (int r = 0; r < 4; r++)
                                for (int t = 0; t < 8; t++)
                                    H_cache[d * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                            mimo_apply_tx_correlation_4x8(&H_cache[d * 4], cfg->spatialCorrTx, cfg->spatialCorrXpol);
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
                                for (int t = 0; t < 8; t++) h_eff[r] += H_flat0[r][t] * W_fix[t];
                            }
                        } else {
                            for (int r = 0; r < 4; r++) {
                                h_eff[r] = CX_ZERO;
                                for (int t = 0; t < 8; t++) h_eff[r] += H_cache[d * 4 + r][t] * W_fix[t];
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
                                    for (int t = 0; t < 8; t++) h_eff2[r][l] += H_flat0[r][t] * W2_fix[t][l];
                                }
                        } else {
                            for (int r = 0; r < 4; r++)
                                for (int l = 0; l < 2; l++) {
                                    h_eff2[r][l] = CX_ZERO;
                                    for (int t = 0; t < 8; t++) h_eff2[r][l] += H_cache[d * 4 + r][t] * W2_fix[t][l];
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

                for (int l = 0; l < nCW; l++) {
                    double env = nv_sum[l] / num_data;
                    qam_demap_llr(rx_hat[l], num_data, mcs.modulation, env, allllr[l]);
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        nr_ldpc_rate_match_combine(soft_buf[l][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l][r], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B)) cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    crc_ok[l] = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int biterr = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) biterr++;
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
    printf("\nPDSCH CL 8-port + %s + HARQ simulation complete.\n",
           is_tdl ? "TDL" : "Flat Fading");

    ldpc_free(&ldpc);
    free(data_pos);
    free(H_cache);
    free(Er);
    for (int l = 0; l < 2; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]);
        for (int r = 0; r < seg.C; r++) { free(coded_cw[l][r]); free(rm[l][r]); free(soft_buf[l][r]); }
        free(coded_cw[l]); free(rm[l]); free(soft_buf[l]);
        free(selbits[l]); free(decoded_cb[l]); free(decoded_tb[l]);
        free(sym[l]); free(rx_hat[l]);
        free(allllr[l]);
    }
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 8; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 32포트 Type I SP 코드북 기반 Closed-Loop PDSCH — 평탄 페이딩 (i.i.d.)
 * "Massive MIMO(64안테나 소자)" 요청에 대한 스펙 정합 대응 — 상세 근거는
 * codebook_32port.h 문서 참조(Rel-18 Type I SP 최대 32 CSI-RS 포트,
 * (N1,N2)=(4,4)가 32소자/편파 중 유일한 정사각 2D 배열).
 *
 * RI+PMI  : rank 1~4 전수탐색(codebook_type1_sp_32port_ri_pmi_select,
 *           5120 후보) — 4-port/8-port와 달리 "Adaptive" 단일 시나리오만
 *           보고한다. rank별 고정(R1fix~R4fix) 4개 시나리오를 전부 추가로
 *           도는 것은 코드워드 인코딩 배수만 늘리고(최대 4개) 검증
 *           가치는 낮아 범위에서 제외 — 대신 SNR별 rank 선택 분포(%)를
 *           출력해 실제 massive MIMO 다중화 이득이 SNR에 따라 어떻게
 *           나타나는지 직접 보여준다.
 * 검출    : rank1=mrc_combine_4rx, rank2=mimo_mmse_detect_4rx2,
 *           rank3=mimo_mmse_detect_4rx3(신규), rank4=mimo_mmse_detect_4x4
 *           (프리코딩 후 유효 채널이 항상 4×rank라 Tx 포트 수 32와
 *           무관하게 그대로 재사용 가능 — 8-port와 동일 논리)
 * Tx 상관 : mimo_apply_tx_correlation_4x32 — N1=N2=4 2D Kronecker 상관
 *           (수평/수직 지수상관 + XPD), mimo.h 문서 참조
 * 채널 추정 : Genie-aided (파일럿 오버헤드 없음, 기존 관례와 동일)
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_32port_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by all up-to-4 CWs (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per CW */
    int nd   = num_data;         /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by all CWs (same E) */

    printf("=== PDSCH Closed-Loop 32-port Codebook (RI+PMI Adaptive, rank 1~4) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 32 / 4  (64 물리 소자 = 32포트 × 2편파)\n");
    printf("Codebook     : TS 38.214 Type I SP (N1=4, N2=4, O1=4, O2=4, Ng=2)\n");
    printf("Rank Range   : 1~4  (RI+PMI 자동 선택, 추정 용량 기준, 5120 후보)\n");
    printf("Tx Corr      : rho_h=%.2f, rho_v=%.2f, rho_xpol=%.2f (%s, Kronecker 2D)\n",
           cfg->spatialCorrTx, cfg->spatialCorrTxVert, cfg->spatialCorrXpol,
           (cfg->spatialCorrTx > 0.0 || cfg->spatialCorrTxVert > 0.0 || cfg->spatialCorrXpol > 0.0)
               ? "공간상관" : "i.i.d.");
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d  (Genie-aided CSI, pilot 오버헤드 없음)\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[4], *tb_crc[4], *cb_bits[4], *coded[4], **rm[4], *selbits[4];
    int   *decoded_cb[4], *decoded_tb[4];
    cx_t  *sym[4];
    double *allllr[4], *soft_buf[4];
    cx_t  *rx_hat[4];
    for (int l = 0; l < 4; l++) {
        tb[l]      = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l]  = (int    *)malloc(B     * sizeof(int));
        cb_bits[l] = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]   = (int    *)malloc(acsz  * sizeof(int));
        rm[l]      = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l] = (int    *)malloc(E     * sizeof(int));
        sym[l]     = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[l]  = (double *)malloc(E     * sizeof(double));
        soft_buf[l]= (double *)malloc(acsz  * sizeof(double));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B * sizeof(int));
        rx_hat[l]  = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc(nd * 4 * sizeof(cx_t));

    printf("%-9s  %-12s %-11s  %-8s  %-6s %-6s %-6s %-6s\n",
           "SNR(dB)", "BER", "BLER", "AvgRank", "R1%", "R2%", "R3%", "R4%");
    for (int i = 0; i < 76; i++) printf("-");
    printf("\n");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits = 0, b_err = 0;
        int  e_blk = 0;
        long rank_cnt[5] = {0, 0, 0, 0, 0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 채널 드로우: H[4][32], i.i.d. Rayleigh + Tx 공간상관 */
            cx_t H[4][32];
            mimo_channel_draw_4x32(H);
            mimo_apply_tx_correlation_4x32(H, cfg->spatialCorrTx, cfg->spatialCorrTxVert, cfg->spatialCorrXpol);

            /* ② RI+PMI 선택 (rank 1~4, 5120 후보 전수 탐색) */
            int rank, l1, l2, i13, i2;
            codebook_type1_sp_32port_ri_pmi_select(H, N0, &rank, &l1, &l2, &i13, &i2);
            rank_cnt[rank]++;
            rank_sum += rank;
            int nCW = rank;

            /* ③ nCW개 CW 인코딩 */
            for (int c = 0; c < nCW; c++) {
                gen_random_bits(tb[c], tbsz);
                attach_crc(tb[c], tbsz, CRC24A, tb_crc[c]);
                nr_seg_split(tb_crc[c], &seg, cb_bits[c]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[c] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[c]);
                    nr_ldpc_rate_match_select(coded[c], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[c][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[c], Er, selbits[c]);
                qam_modulate(selbits[c], E, mcs.modulation, sym[c]);
            }

            /* ④ 노이즈 드로우 */
            for (int d = 0; d < nd; d++)
                for (int r = 0; r < 4; r++)
                    nbuf[d * 4 + r] = CX_MAKE(randn() * sigma, randn() * sigma);

            /* ⑤ 프리코딩 + 검출 (rank별 분기) */
            double nv_sum[4] = {0.0, 0.0, 0.0, 0.0};

            if (rank == 1) {
                cx_t W[32];
                codebook_type1_sp_32port_rank1(l1, l2, i2, W);
                cx_t h_eff[4];
                for (int r = 0; r < 4; r++) {
                    h_eff[r] = 0.0;
                    for (int t = 0; t < 32; t++) h_eff[r] += H[r][t] * W[t];
                }
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum[0] += nv;
                }
            } else if (rank == 2) {
                cx_t W[32][2];
                codebook_type1_sp_32port_rank2(l1, l2, i13, i2, W);
                cx_t h_eff[4][2];
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 2; c++) {
                        h_eff[r][c] = 0.0;
                        for (int t = 0; t < 32; t++) h_eff[r][c] += H[r][t] * W[t][c];
                    }
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d] + nbuf[d * 4 + r];
                    cx_t xh[2]; double nv[2];
                    mimo_mmse_detect_4rx2(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1];
                }
            } else if (rank == 3) {
                cx_t W[32][3];
                codebook_type1_sp_32port_rank3(l1, l2, i13, i2, W);
                cx_t h_eff[4][3];
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 3; c++) {
                        h_eff[r][c] = 0.0;
                        for (int t = 0; t < 32; t++) h_eff[r][c] += H[r][t] * W[t][c];
                    }
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + nbuf[d * 4 + r];
                    cx_t xh[3]; double nv[3];
                    mimo_mmse_detect_4rx3(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1]; rx_hat[2][d] = xh[2];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1]; nv_sum[2] += nv[2];
                }
            } else {   /* rank == 4 */
                cx_t W[32][4];
                codebook_type1_sp_32port_rank4(l1, l2, i13, i2, W);
                cx_t h_eff[4][4];
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 4; c++) {
                        h_eff[r][c] = 0.0;
                        for (int t = 0; t < 32; t++) h_eff[r][c] += H[r][t] * W[t][c];
                    }
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + h_eff[r][3] * sym[3][d]
                               + nbuf[d * 4 + r];
                    cx_t xh[4]; double nv[4];
                    mimo_mmse_detect_4x4(h_eff, y, N0, xh, nv);
                    for (int c = 0; c < 4; c++) { rx_hat[c][d] = xh[c]; nv_sum[c] += nv[c]; }
                }
            }

            /* ⑥ 복조/복호 (nCW개 CW 공통 처리) */
            int blk_err = 0;
            for (int c = 0; c < nCW; c++) {
                double env = nv_sum[c] / nd;
                qam_demap_llr(rx_hat[c], nd, mcs.modulation, env, allllr[c]);
                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[c], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[c], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[c] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[c], 25, decoded_cb[c]);
                    if (seg.C > 1 && !check_crc(decoded_cb[c], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[c] + (size_t)r * payload, decoded_cb[c], payload * sizeof(int));
                }
                int crc_c = cb_crc_ok && check_crc(decoded_tb[c], B, CRC24A);
                int be_c = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[c][i] != decoded_tb[c][i]) be_c++;
                b_err  += be_c;
                t_bits += tbsz;
                if (!crc_c || be_c > 0) blk_err = 1;
            }
            if (blk_err) e_blk++;
        }   /* end trial loop */

        double ber  = t_bits > 0 ? (double)b_err / t_bits : 0.0;
        double bler = e_blk / (double)cfg->numTrials;
        double avg_rank = rank_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-8.2f  %-6.1f %-6.1f %-6.1f %-6.1f\n",
               snr, ber, bler, avg_rank,
               rank_cnt[1] * 100.0 / cfg->numTrials,
               rank_cnt[2] * 100.0 / cfg->numTrials,
               rank_cnt[3] * 100.0 / cfg->numTrials,
               rank_cnt[4] * 100.0 / cfg->numTrials);
    }   /* end SNR loop */

    printf("\nPDSCH CL 32-port simulation complete.\n");

    ldpc_free(&ldpc);
    free(nbuf); free(Er);
    for (int l = 0; l < 4; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(sym[l]); free(allllr[l]); free(soft_buf[l]);
        free(decoded_cb[l]); free(decoded_tb[l]);
        free(rx_hat[l]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 16포트 Eigen-Beamforming(SVD) 기반 개루프 PDSCH — 평탄 페이딩 (i.i.d.)
 * 비-코드북(non-codebook) 방식 — eigen_16port.h 문서 참조.
 *
 * RI+PMI 대신 rank 적응만 존재(PMI 개념 자체가 없음 — 프리코더가 codebook
 * 후보 탐색이 아니라 H의 SVD에서 직접 계산됨, genie-aided CSI로 완전한
 * H를 안다고 가정, 실제로는 TDD SRS reciprocity에 대응).
 * 프리코더: W = V[:,0:rank] / sqrt(rank) (V는 eigen_bf_16port_svd의 우특이
 * 벡터, 이미 열마다 단위노름 — 스케일만 적용). 유효 채널 H·V[:,i] = σ_i·U[:,i]
 * (SVD 정의)라 스트림 간 자동 직교 — CL_32PORT와 동일한 검출기
 * (mrc_combine_4rx/mimo_mmse_detect_4rx2/4rx3/4x4)를 그대로 재사용.
 * Tx 상관/TDL/HARQ 미지원(다른 massive MIMO 모드들과 동일 범위 판단).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_eigen_16port_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by all up-to-4 CWs (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per CW */
    int nd   = num_data;         /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by all CWs (same E) */

    printf("=== PDSCH Eigen-Beamforming 16-port (SVD, Open-Loop, Rank Adaptive) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 16 / 4\n");
    printf("Precoding    : SVD eigen-beamforming (non-codebook, genie-aided H)\n");
    printf("Rank Range   : 1~4  (등력 분배 기준 rank 적응, water-filling 미적용)\n");
    printf("Tx Corr      : i.i.d. (16-port 공간상관 미구현)\n");
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d  (Genie-aided CSI, pilot 오버헤드 없음)\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[4], *tb_crc[4], *cb_bits[4], *coded[4], **rm[4], *selbits[4];
    int   *decoded_cb[4], *decoded_tb[4];
    cx_t  *sym[4];
    double *allllr[4], *soft_buf[4];
    cx_t  *rx_hat[4];
    for (int l = 0; l < 4; l++) {
        tb[l]      = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l]  = (int    *)malloc(B     * sizeof(int));
        cb_bits[l] = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]   = (int    *)malloc(acsz  * sizeof(int));
        rm[l]      = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l] = (int    *)malloc(E     * sizeof(int));
        sym[l]     = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[l]  = (double *)malloc(E     * sizeof(double));
        soft_buf[l]= (double *)malloc(acsz  * sizeof(double));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B * sizeof(int));
        rx_hat[l]  = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc(nd * 4 * sizeof(cx_t));

    printf("%-9s  %-12s %-11s  %-8s  %-6s %-6s %-6s %-6s\n",
           "SNR(dB)", "BER", "BLER", "AvgRank", "R1%", "R2%", "R3%", "R4%");
    for (int i = 0; i < 76; i++) printf("-");
    printf("\n");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma_noise = sqrt(N0 / 2.0);

        long t_bits = 0, b_err = 0;
        int  e_blk = 0;
        long rank_cnt[5] = {0, 0, 0, 0, 0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 채널 드로우 + SVD */
            cx_t H[4][16];
            mimo_channel_draw_4x16(H);
            double sv[4];
            cx_t Umat[4][4], Vmat[16][4];
            eigen_bf_16port_svd(H, sv, Umat, Vmat);

            /* ② Rank 적응 (등력 분배 추정 용량 기준) */
            int rank;
            eigen_bf_16port_select_rank(sv, N0, &rank);
            rank_cnt[rank]++;
            rank_sum += rank;
            int nCW = rank;

            /* ③ nCW개 CW 인코딩 */
            for (int c = 0; c < nCW; c++) {
                gen_random_bits(tb[c], tbsz);
                attach_crc(tb[c], tbsz, CRC24A, tb_crc[c]);
                nr_seg_split(tb_crc[c], &seg, cb_bits[c]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[c] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[c]);
                    nr_ldpc_rate_match_select(coded[c], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[c][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[c], Er, selbits[c]);
                qam_modulate(selbits[c], E, mcs.modulation, sym[c]);
            }

            /* ④ 노이즈 드로우 */
            for (int d = 0; d < nd; d++)
                for (int r = 0; r < 4; r++)
                    nbuf[d * 4 + r] = CX_MAKE(randn() * sigma_noise, randn() * sigma_noise);

            /* ⑤ 프리코딩(W=V[:,0:rank]/√rank) + 검출 (rank별 분기, CL_32PORT와 동일 패턴) */
            double nv_sum[4] = {0.0, 0.0, 0.0, 0.0};
            double pnorm = 1.0 / sqrt((double)rank);

            if (rank == 1) {
                cx_t h_eff[4];
                for (int r = 0; r < 4; r++) h_eff[r] = pnorm * sv[0] * Umat[r][0];
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum[0] += nv;
                }
            } else if (rank == 2) {
                cx_t h_eff[4][2];
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 2; c++) h_eff[r][c] = pnorm * sv[c] * Umat[r][c];
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d] + nbuf[d * 4 + r];
                    cx_t xh[2]; double nv[2];
                    mimo_mmse_detect_4rx2(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1];
                }
            } else if (rank == 3) {
                cx_t h_eff[4][3];
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 3; c++) h_eff[r][c] = pnorm * sv[c] * Umat[r][c];
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + nbuf[d * 4 + r];
                    cx_t xh[3]; double nv[3];
                    mimo_mmse_detect_4rx3(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1]; rx_hat[2][d] = xh[2];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1]; nv_sum[2] += nv[2];
                }
            } else {   /* rank == 4 */
                cx_t h_eff[4][4];
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 4; c++) h_eff[r][c] = pnorm * sv[c] * Umat[r][c];
                for (int d = 0; d < nd; d++) {
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + h_eff[r][3] * sym[3][d]
                               + nbuf[d * 4 + r];
                    cx_t xh[4]; double nv[4];
                    mimo_mmse_detect_4x4(h_eff, y, N0, xh, nv);
                    for (int c = 0; c < 4; c++) { rx_hat[c][d] = xh[c]; nv_sum[c] += nv[c]; }
                }
            }

            /* ⑥ 복조/복호 (nCW개 CW 공통 처리) */
            int blk_err = 0;
            for (int c = 0; c < nCW; c++) {
                double env = nv_sum[c] / nd;
                qam_demap_llr(rx_hat[c], nd, mcs.modulation, env, allllr[c]);
                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[c], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[c], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[c] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[c], 25, decoded_cb[c]);
                    if (seg.C > 1 && !check_crc(decoded_cb[c], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[c] + (size_t)r * payload, decoded_cb[c], payload * sizeof(int));
                }
                int crc_c = cb_crc_ok && check_crc(decoded_tb[c], B, CRC24A);
                int be_c = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[c][i] != decoded_tb[c][i]) be_c++;
                b_err  += be_c;
                t_bits += tbsz;
                if (!crc_c || be_c > 0) blk_err = 1;
            }
            if (blk_err) e_blk++;
        }   /* end trial loop */

        double ber  = t_bits > 0 ? (double)b_err / t_bits : 0.0;
        double bler = e_blk / (double)cfg->numTrials;
        double avg_rank = rank_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-8.2f  %-6.1f %-6.1f %-6.1f %-6.1f\n",
               snr, ber, bler, avg_rank,
               rank_cnt[1] * 100.0 / cfg->numTrials,
               rank_cnt[2] * 100.0 / cfg->numTrials,
               rank_cnt[3] * 100.0 / cfg->numTrials,
               rank_cnt[4] * 100.0 / cfg->numTrials);
    }   /* end SNR loop */

    printf("\nPDSCH Eigen 16-port simulation complete.\n");

    ldpc_free(&ldpc);
    free(nbuf); free(Er);
    for (int l = 0; l < 4; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(sym[l]); free(allllr[l]); free(soft_buf[l]);
        free(decoded_cb[l]); free(decoded_tb[l]);
        free(rx_hat[l]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 16포트 Eigen-Beamforming(SVD) 기반 개루프 PDSCH — TDL + Imperfect CSI
 *
 * run_pdsch_eigen_16port_simulation()(평탄, genie-aided H)의 TDL 확장이자,
 * channel_estimation.c의 LS/MMSE/DFT 채널추정을 실제로 빔포머 설계에
 * 배선한 첫 사례. EIGEN16_CHAN_EST 설정(NONE/LS/MMSE/DFT)에 따라:
 *
 *   프리코더 설계용 채널  : 4×16 wideband H_est(파일럿 REs에서 UL SRS
 *                          reciprocity를 모사한 노이즈 있는 관측 → 선택한
 *                          방식으로 추정 → 파일럿(또는 활성 부반송파) 평균
 *                          으로 wideband화, CL_4PORT/8PORT TDL의 "Wideband
 *                          PMI, H_avg" 관례와 동일)
 *   실제 하향링크 전송    : RE별 진짜 주파수선택적 H_true(TDL, genie)
 *
 * 두 채널의 불일치(추정 오차 + wideband 프리코더 대 주파수선택적 채널의
 * 근본적 불일치, 후자는 CL_4PORT/8PORT TDL에도 이미 있는 특성)가 실제
 * 잔여 스트림간 간섭으로 나타난다 — 검출은 flat 버전과 동일하게 기존
 * 검출기(mrc_combine_4rx/mimo_mmse_detect_4rx2/4rx3/4x4) 재사용(이번엔
 * h_eff가 더 이상 대각이 아니므로 MMSE 보정이 실제로 의미 있게 작동).
 *
 * NONE  : 파일럿/노이즈 없이 H_true를 그대로 wideband 평균 — genie 상한
 *         (flat 버전과 동일 철학), MMSE/DFT/LS와 직접 비교하는 기준선.
 * LS    : 파일럿에서의 순수 잡음 섞인 관측치를 그대로 평균(스무딩 없음).
 * MMSE  : mmse_build_filter()로 SNR당 1회 Wiener 필터 계산 후
 *         mmse_apply_filter()를 64개 (Rx,Tx) 안테나쌍 × 매 트라이얼에
 *         재사용 — M×M 역행렬을 트라이얼마다 다시 풀지 않음(성능).
 * DFT   : dft_channel_estimate() 시간영역 절단, num_taps는 tdlDelaySpreadNs의
 *         8배를 활성 부반송파 샘플주기로 나눈 값(구현 정의 휴리스틱 —
 *         이전 세션 standalone 하네스 검증에서 t20이 t10보다 고SNR
 *         바닥이 훨씬 낮았던 것을 반영해 다소 넉넉하게 잡음).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_eigen_16port_tdl_simulation(const L1Config *cfg) {
    int num_rb     = cfg->numRB;
    int num_data   = 6 * num_rb;
    int num_pilots = 6 * num_rb;
    int num_active = 12 * num_rb;
    int *data_pos   = (int *)malloc(num_data   * sizeof(int));
    int *pilot_pos  = (int *)malloc(num_pilots * sizeof(int));
    dmrs_data_indices(num_rb, data_pos);
    dmrs_pilot_indices(num_rb, pilot_pos);

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int est_none = (strcmp(cfg->eigen16ChanEst, "NONE") == 0);
    int est_ls   = (strcmp(cfg->eigen16ChanEst, "LS")   == 0);
    int est_mmse = (strcmp(cfg->eigen16ChanEst, "MMSE") == 0);
    int est_dft  = (strcmp(cfg->eigen16ChanEst, "DFT")  == 0);

    int dft_num_taps = 0;
    if (est_dft) {
        double t_sample = 1.0 / ((double)num_active * scs_hz);
        dft_num_taps = (int)ceil(8.0 * cfg->tdlDelaySpreadNs * 1e-9 / t_sample);
        if (dft_num_taps < 2) dft_num_taps = 2;
        if (dft_num_taps > num_active) dft_num_taps = num_active;
    }

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by all up-to-4 CWs (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per CW */
    int nd   = num_data;         /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by all CWs (same E) */

    printf("=== PDSCH Eigen-Beamforming 16-port (SVD, Open-Loop), TDL + Imperfect CSI ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 16 / 4\n");
    printf("Precoding    : SVD eigen-beamforming (non-codebook)\n");
    printf("Chan Est     : %s\n", cfg->eigen16ChanEst);
    if (est_dft) printf("DFT Taps     : %d (of %d active SC)\n", dft_num_taps, num_active);
    printf("Rank Range   : 1~4  (등력 분배 기준 rank 적응, wideband H_est 기준)\n");
    printf("Channel      : TDL per Tx-Rx pair (64 tap-sets, DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[4], *tb_crc[4], *cb_bits[4], *coded[4], **rm[4], *selbits[4];
    int   *decoded_cb[4], *decoded_tb[4];
    cx_t  *sym[4];
    double *allllr[4], *soft_buf[4];
    cx_t  *rx_hat[4];
    for (int l = 0; l < 4; l++) {
        tb[l]      = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l]  = (int    *)malloc(B     * sizeof(int));
        cb_bits[l] = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]   = (int    *)malloc(acsz  * sizeof(int));
        rm[l]      = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l] = (int    *)malloc(E     * sizeof(int));
        sym[l]     = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[l]  = (double *)malloc(E     * sizeof(double));
        soft_buf[l]= (double *)malloc(acsz  * sizeof(double));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B * sizeof(int));
        rx_hat[l]  = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc(nd * 4 * sizeof(cx_t));

    /* H 캐시: RE별 진짜 채널 (4Rx x 16Tx), 실제 하향링크 전송에 사용 */
    cx_t (*H_cache)[16] = (cx_t (*)[16])malloc((size_t)nd * 4 * sizeof(*H_cache));

    cx_t *taps[4][16];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 16; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    /* 채널추정용 버퍼 (LS/MMSE/DFT 공용, (r,t) 쌍마다 재사용) */
    cx_t *h_ls_pilot   = (cx_t *)malloc(num_pilots * sizeof(cx_t));

    /* DFT 경로 사전계산: interpolate_channel→IDFT→절단→DFT→data RE 평균
     * 전체 파이프라인은 파일럿 벡터 h_ls_pilot에 대해 "선형"이다(모든
     * 단계가 선형연산이고 dft_num_taps는 SNR/트라이얼/안테나쌍과 무관하게
     * 고정) — 즉 H_est_avg[r][t] = w_dft · h_ls_pilot인 고정 가중치 벡터
     * w_dft(길이 num_pilots)가 존재한다. w_dft는 시뮬레이션 시작 시 표준
     * 기저벡터(단위벡터) num_pilots개를 파이프라인에 흘려 "임펄스 응답"
     * 을 측정하는 방식(중첩의 원리)으로 딱 1회만 계산 — 이후 트라이얼×
     * 64안테나쌍마다는 O(num_active²) 전체 파이프라인 대신 O(num_pilots)
     * 내적 한 번만 하면 되어 압도적으로 빠르다(2026-08-31, DFT 경로가
     * MMSE/LS보다 15~20배 느렸던 것을 이 최적화로 해소 — 상세는
     * docs/analysis/history.md 참조). MMSE도 이미 SNR당 1회 필터 계산 +
     * O(M²) 재사용 패턴(mmse_build_filter/mmse_apply_filter)이라 동일한
     * 최적화 철학. */
    cx_t *w_dft = NULL;
    if (est_dft) {
        w_dft = (cx_t *)malloc(num_pilots * sizeof(cx_t));
        cx_t *basis    = (cx_t *)malloc(num_pilots * sizeof(cx_t));
        cx_t *full_tmp = (cx_t *)malloc(num_active * sizeof(cx_t));
        cx_t *dft_tmp  = (cx_t *)malloc(num_active * sizeof(cx_t));
        for (int p = 0; p < num_pilots; p++) {
            for (int q = 0; q < num_pilots; q++) basis[q] = (p == q) ? 1.0 : 0.0;
            interpolate_channel(basis, num_pilots, pilot_pos, num_active, full_tmp);
            dft_channel_estimate(full_tmp, num_active, dft_num_taps, dft_tmp);
            cx_t s = CX_ZERO;
            for (int d = 0; d < num_data; d++) s += dft_tmp[data_pos[d]];
            w_dft[p] = s / (double)num_data;
        }
        free(basis); free(full_tmp); free(dft_tmp);
    }

    printf("%-9s  %-12s %-11s  %-8s  %-6s %-6s %-6s %-6s\n",
           "SNR(dB)", "BER", "BLER", "AvgRank", "R1%", "R2%", "R3%", "R4%");
    for (int i = 0; i < 76; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        /* MMSE Wiener 필터는 SNR(N0)에만 의존 — SNR 포인트당 1회 계산,
           트라이얼/안테나쌍마다 재사용(mmse_channel_estimate() 안에서
           매번 M×M 역행렬을 다시 푸는 것보다 훨씬 빠름). */
        cx_t *mmse_W = NULL;
        if (est_mmse) {
            mmse_W = (cx_t *)malloc((size_t)num_pilots * sizeof(cx_t));
            mmse_build_avg_filter(pilot_pos, num_pilots, data_pos, nd,
                                   cfg->tdlDelaySpreadNs, scs_hz, N0, mmse_W);
        }

        long t_bits = 0, b_err = 0;
        int  e_blk = 0;
        long rank_cnt[5] = {0, 0, 0, 0, 0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① TDL 드로우 + H_cache(RE별 진짜 채널) 계산 */
            for (int r = 0; r < 4; r++)
                for (int t = 0; t < 16; t++)
                    tdl_draw(&tdl_ch, taps[r][t]);

            for (int d = 0; d < nd; d++) {
                int k = data_pos[d];
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 16; t++)
                        H_cache[d * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
            }

            /* ② 프리코더 설계용 wideband 채널 추정 (H_est_avg, 4x16) */
            cx_t H_est_avg[4][16];
            if (est_none) {
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 16; t++) {
                        cx_t s = CX_ZERO;
                        for (int d = 0; d < nd; d++) s += H_cache[d * 4 + r][t];
                        H_est_avg[r][t] = s / (double)nd;
                    }
            } else {
                for (int r = 0; r < 4; r++) {
                    for (int t = 0; t < 16; t++) {
                        for (int p = 0; p < num_pilots; p++) {
                            cx_t htrue = tdl_freq_response(&tdl_ch, taps[r][t], pilot_pos[p]);
                            h_ls_pilot[p] = htrue + CX_MAKE(randn() * sigma, randn() * sigma);
                        }

                        if (est_ls) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += h_ls_pilot[p];
                            H_est_avg[r][t] = s / (double)num_pilots;
                        } else if (est_mmse) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += mmse_W[p] * h_ls_pilot[p];
                            H_est_avg[r][t] = s;
                        } else {   /* est_dft — 사전계산한 w_dft로 O(num_pilots) 내적만 (위 주석 참조) */
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += w_dft[p] * h_ls_pilot[p];
                            H_est_avg[r][t] = s;
                        }
                    }
                }
            }

            /* ③ 추정 채널로 SVD + rank 적응 (프리코더 설계) */
            double sv[4];
            cx_t Umat[4][4], Vmat[16][4];
            eigen_bf_16port_svd(H_est_avg, sv, Umat, Vmat);
            int rank;
            eigen_bf_16port_select_rank(sv, N0, &rank);
            rank_cnt[rank]++;
            rank_sum += rank;
            int nCW = rank;

            double pnorm = 1.0 / sqrt((double)rank);
            cx_t W[16][4];
            for (int t = 0; t < 16; t++)
                for (int c = 0; c < rank; c++)
                    W[t][c] = pnorm * Vmat[t][c];

            /* ④ nCW개 CW 인코딩 */
            for (int c = 0; c < nCW; c++) {
                gen_random_bits(tb[c], tbsz);
                attach_crc(tb[c], tbsz, CRC24A, tb_crc[c]);
                nr_seg_split(tb_crc[c], &seg, cb_bits[c]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[c] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[c]);
                    nr_ldpc_rate_match_select(coded[c], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[c][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[c], Er, selbits[c]);
                qam_modulate(selbits[c], E, mcs.modulation, sym[c]);
            }

            /* ⑤ 노이즈 드로우 */
            for (int d = 0; d < nd; d++)
                for (int r = 0; r < 4; r++)
                    nbuf[d * 4 + r] = CX_MAKE(randn() * sigma, randn() * sigma);

            /* ⑥ 프리코딩(진짜 H_cache × 추정 채널 기반 W, 불일치 반영) + 검출 */
            double nv_sum[4] = {0.0, 0.0, 0.0, 0.0};

            if (rank == 1) {
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4];
                    for (int r = 0; r < 4; r++) {
                        h_eff[r] = CX_ZERO;
                        for (int t = 0; t < 16; t++) h_eff[r] += H_cache[d * 4 + r][t] * W[t][0];
                    }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum[0] += nv;
                }
            } else if (rank == 2) {
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4][2];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 2; c++) {
                            h_eff[r][c] = CX_ZERO;
                            for (int t = 0; t < 16; t++) h_eff[r][c] += H_cache[d * 4 + r][t] * W[t][c];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d] + nbuf[d * 4 + r];
                    cx_t xh[2]; double nv[2];
                    mimo_mmse_detect_4rx2(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1];
                }
            } else if (rank == 3) {
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4][3];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 3; c++) {
                            h_eff[r][c] = CX_ZERO;
                            for (int t = 0; t < 16; t++) h_eff[r][c] += H_cache[d * 4 + r][t] * W[t][c];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + nbuf[d * 4 + r];
                    cx_t xh[3]; double nv[3];
                    mimo_mmse_detect_4rx3(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1]; rx_hat[2][d] = xh[2];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1]; nv_sum[2] += nv[2];
                }
            } else {   /* rank == 4 */
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4][4];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 4; c++) {
                            h_eff[r][c] = CX_ZERO;
                            for (int t = 0; t < 16; t++) h_eff[r][c] += H_cache[d * 4 + r][t] * W[t][c];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + h_eff[r][3] * sym[3][d]
                               + nbuf[d * 4 + r];
                    cx_t xh[4]; double nv[4];
                    mimo_mmse_detect_4x4(h_eff, y, N0, xh, nv);
                    for (int c = 0; c < 4; c++) { rx_hat[c][d] = xh[c]; nv_sum[c] += nv[c]; }
                }
            }

            /* ⑦ 복조/복호 */
            int blk_err = 0;
            for (int c = 0; c < nCW; c++) {
                double env = nv_sum[c] / nd;
                qam_demap_llr(rx_hat[c], nd, mcs.modulation, env, allllr[c]);
                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[c], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[c], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[c] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[c], 25, decoded_cb[c]);
                    if (seg.C > 1 && !check_crc(decoded_cb[c], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[c] + (size_t)r * payload, decoded_cb[c], payload * sizeof(int));
                }
                int crc_c = cb_crc_ok && check_crc(decoded_tb[c], B, CRC24A);
                int be_c = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[c][i] != decoded_tb[c][i]) be_c++;
                b_err  += be_c;
                t_bits += tbsz;
                if (!crc_c || be_c > 0) blk_err = 1;
            }
            if (blk_err) e_blk++;
        }   /* end trial loop */

        double ber  = t_bits > 0 ? (double)b_err / t_bits : 0.0;
        double bler = e_blk / (double)cfg->numTrials;
        double avg_rank = rank_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-8.2f  %-6.1f %-6.1f %-6.1f %-6.1f\n",
               snr, ber, bler, avg_rank,
               rank_cnt[1] * 100.0 / cfg->numTrials,
               rank_cnt[2] * 100.0 / cfg->numTrials,
               rank_cnt[3] * 100.0 / cfg->numTrials,
               rank_cnt[4] * 100.0 / cfg->numTrials);

        if (mmse_W) free(mmse_W);
    }   /* end SNR loop */

    printf("\nPDSCH Eigen 16-port + TDL(%s CSI) simulation complete.\n", cfg->eigen16ChanEst);

    ldpc_free(&ldpc);
    free(data_pos); free(pilot_pos);
    free(H_cache);
    free(nbuf); free(Er);
    free(h_ls_pilot);
    if (w_dft) free(w_dft);
    for (int l = 0; l < 4; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(sym[l]); free(allllr[l]); free(soft_buf[l]);
        free(decoded_cb[l]); free(decoded_tb[l]);
        free(rx_hat[l]);
    }
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 16; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 16포트 Eigen-Beamforming(SVD) — TDL + Imperfect CSI + Subband 프리코딩
 *
 * run_pdsch_eigen_16port_tdl_simulation()(wideband 프리코더 1개)의 확장.
 * 2026-08-31, wideband 버전으로 LS/MMSE/DFT를 비교했을 때 세 방식의 BLER
 * 차이가 거의 안 보이는 현상을 발견 — 원인은 120개 파일럿을 wideband
 * 스칼라 하나로 평균해버려서 MMSE의 "주파수상관 활용" 이득이 평균화
 * 자체의 노이즈 저감에 묻히기 때문(RE별 채널추정 자체의 NMSE에서는 MMSE가
 * 확실히 우위임을 별도 검증함). 이 함수는 그 근본 원인을 해소 — 실제
 * 3GPP 동작과 동일하게 **RI(rank)는 wideband로 한 번만 결정**하되(RI 보고는
 * 원래 wideband/주기적), **프리코더의 "방향"(빔포밍 벡터)은 PRG(Precoding
 * Resource block Group, 4RB 단위 — 실제 gNB의 precoderGranularity 개념에
 * 대응하는 구현 정의 값, 3GPP가 이 그래뉼래러티 자체를 강제하지는 않음)
 * 마다 별도로** 계산한다.
 *
 * 순서: ① wideband H_est_avg로 SVD+rank 선택(기존과 동일) → ② PRG별로
 * 그 PRG의 파일럿만 사용해 H_est_sb를 추정 → SVD → 이미 정해진 rank만큼
 * 상위 특이벡터를 그 PRG의 프리코더로 사용 → ③ 각 데이터 RE는 자신이
 * 속한 PRG의 프리코더로 프리코딩(실제 채널은 여전히 RE별 진짜 H_cache).
 *
 * PRG_SIZE_RB=4 (num_rb=20이면 5개 PRG, 마지막 PRG는 num_rb%4가 있으면
 * 더 작을 수 있음). MMSE/DFT 둘 다 PRG마다 별도의 필터/가중치가 필요 —
 * MMSE는 SNR 포인트당 PRG 수만큼(mmse_build_filter를 PRG별 파일럿
 * 부분배열로 호출), DFT는 시뮬레이션 시작 시 PRG별 w_dft를 1회씩
 * 사전계산(wideband 버전과 동일한 "선형 임펄스 응답" 최적화, PRG마다
 * 파일럿 수가 적어 M이 작으므로 원래도 더 빠름).
 * ─────────────────────────────────────────────────────────────────────────── */
#define EIGEN16_PRG_SIZE_RB 4

void run_pdsch_eigen_16port_subband_simulation(const L1Config *cfg) {
    int num_rb     = cfg->numRB;
    int num_data   = 6 * num_rb;
    int num_pilots = 6 * num_rb;
    int *data_pos   = (int *)malloc(num_data   * sizeof(int));
    int *pilot_pos  = (int *)malloc(num_pilots * sizeof(int));
    dmrs_data_indices(num_rb, data_pos);
    dmrs_pilot_indices(num_rb, pilot_pos);

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int est_none = (strcmp(cfg->eigen16ChanEst, "NONE") == 0);
    int est_ls   = (strcmp(cfg->eigen16ChanEst, "LS")   == 0);
    int est_mmse = (strcmp(cfg->eigen16ChanEst, "MMSE") == 0);
    int est_dft  = (strcmp(cfg->eigen16ChanEst, "DFT")  == 0);

    /* PRG 구조: PRG g는 RB [rb_start(g), rb_end(g)) 를 담당.
     * data_pos/pilot_pos가 RB 순서대로 6개씩 채워져 있으므로 배열 인덱스
     * 6*rb_start(g) .. 6*rb_end(g)-1 가 정확히 그 PRG의 파일럿/데이터. */
    int num_prg = (num_rb + EIGEN16_PRG_SIZE_RB - 1) / EIGEN16_PRG_SIZE_RB;
    int *prg_rb_start  = (int *)malloc(num_prg * sizeof(int));
    int *prg_num_rb    = (int *)malloc(num_prg * sizeof(int));
    int *prg_n_pilots  = (int *)malloc(num_prg * sizeof(int));
    int *prg_n_active  = (int *)malloc(num_prg * sizeof(int));
    int *prg_n_data    = (int *)malloc(num_prg * sizeof(int));
    for (int g = 0; g < num_prg; g++) {
        int rb0 = g * EIGEN16_PRG_SIZE_RB;
        int rb1 = rb0 + EIGEN16_PRG_SIZE_RB;
        if (rb1 > num_rb) rb1 = num_rb;
        prg_rb_start[g] = rb0;
        prg_num_rb[g]   = rb1 - rb0;
        prg_n_pilots[g] = 6  * prg_num_rb[g];
        prg_n_active[g] = 12 * prg_num_rb[g];
        prg_n_data[g]   = 6  * prg_num_rb[g];
    }
    /* PRG별 프리코더 저장 버퍼 — num_prg는 numRB에 비례해 얼마든지 커질 수
     * 있어(NR 최대 273RB → num_prg 최대 69) 고정크기 배열 대신 동적 할당. */
    cx_t (*Wg_store)[16][4] = (cx_t (*)[16][4])malloc((size_t)num_prg * sizeof(*Wg_store));

    int dft_num_taps_prg_max = 0;   /* 헤더 출력용 참고값(첫 PRG 기준) */
    if (est_dft) {
        double t_sample0 = 1.0 / ((double)prg_n_active[0] * scs_hz);
        dft_num_taps_prg_max = (int)ceil(8.0 * cfg->tdlDelaySpreadNs * 1e-9 / t_sample0);
        if (dft_num_taps_prg_max < 2) dft_num_taps_prg_max = 2;
        if (dft_num_taps_prg_max > prg_n_active[0]) dft_num_taps_prg_max = prg_n_active[0];
    }

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by all up-to-4 CWs (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per CW */
    int nd   = num_data;         /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by all CWs (same E) */

    printf("=== PDSCH Eigen-Beamforming 16-port (SVD, Open-Loop), TDL + Imperfect CSI + Subband Precoding ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 16 / 4\n");
    printf("Precoding    : SVD eigen-beamforming (non-codebook), PRG=%d RB (%d PRGs)\n",
           EIGEN16_PRG_SIZE_RB, num_prg);
    printf("Chan Est     : %s\n", cfg->eigen16ChanEst);
    if (est_dft) printf("DFT Taps     : ~%d (PRG0 기준, PRG마다 다를 수 있음)\n", dft_num_taps_prg_max);
    printf("Rank Range   : 1~4  (RI는 wideband 1회 결정, 프리코더 방향은 PRG별)\n");
    printf("Channel      : TDL per Tx-Rx pair (64 tap-sets, DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[4], *tb_crc[4], *cb_bits[4], *coded[4], **rm[4], *selbits[4];
    int   *decoded_cb[4], *decoded_tb[4];
    cx_t  *sym[4];
    double *allllr[4], *soft_buf[4];
    cx_t  *rx_hat[4];
    for (int l = 0; l < 4; l++) {
        tb[l]      = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l]  = (int    *)malloc(B     * sizeof(int));
        cb_bits[l] = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]   = (int    *)malloc(acsz  * sizeof(int));
        rm[l]      = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l] = (int    *)malloc(E     * sizeof(int));
        sym[l]     = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[l]  = (double *)malloc(E     * sizeof(double));
        soft_buf[l]= (double *)malloc(acsz  * sizeof(double));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B * sizeof(int));
        rx_hat[l]  = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc(nd * 4 * sizeof(cx_t));

    /* H 캐시: RE별 진짜 채널 (4Rx x 16Tx), 실제 하향링크 전송에 사용 */
    cx_t (*H_cache)[16] = (cx_t (*)[16])malloc((size_t)nd * 4 * sizeof(*H_cache));

    cx_t *taps[4][16];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 16; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    /* 채널추정용 버퍼 (wideband: 전체 파일럿 / subband: PRG당 최대 파일럿 수) */
    cx_t *h_ls_pilot   = (cx_t *)malloc(num_pilots * sizeof(cx_t));

    /* PRG별 DFT 가중치 w_dft_prg[g] (길이 prg_n_pilots[g]) — wideband 버전과
     * 동일한 "선형 임펄스 응답" 사전계산을 PRG마다 그 PRG의 로컬 파일럿/
     * 활성 부반송파 범위로 수행. DFT/IDFT는 PRG 내부 상대 인덱스(0..prg_n_
     * active[g]-1) 기준이므로 pilot_pos를 PRG 시작 위치만큼 offset 보정. */
    cx_t **w_dft_prg = NULL;
    if (est_dft) {
        w_dft_prg = (cx_t **)malloc(num_prg * sizeof(cx_t *));
        for (int g = 0; g < num_prg; g++) {
            int M = prg_n_pilots[g], A = prg_n_active[g], D = prg_n_data[g];
            int offset_sc = prg_rb_start[g] * 12;
            double t_sample = 1.0 / ((double)A * scs_hz);
            int taps_g = (int)ceil(8.0 * cfg->tdlDelaySpreadNs * 1e-9 / t_sample);
            if (taps_g < 2) taps_g = 2;
            if (taps_g > A) taps_g = A;

            int *local_pilot = (int *)malloc(M * sizeof(int));
            int *local_data  = (int *)malloc(D * sizeof(int));
            for (int i = 0; i < M; i++) local_pilot[i] = pilot_pos[6 * prg_rb_start[g] + i] - offset_sc;
            for (int i = 0; i < D; i++) local_data[i]  = data_pos[6 * prg_rb_start[g] + i]  - offset_sc;

            w_dft_prg[g] = (cx_t *)malloc(M * sizeof(cx_t));
            cx_t *basis    = (cx_t *)malloc(M * sizeof(cx_t));
            cx_t *full_tmp = (cx_t *)malloc(A * sizeof(cx_t));
            cx_t *dft_tmp  = (cx_t *)malloc(A * sizeof(cx_t));
            for (int p = 0; p < M; p++) {
                for (int q = 0; q < M; q++) basis[q] = (p == q) ? 1.0 : 0.0;
                interpolate_channel(basis, M, local_pilot, A, full_tmp);
                dft_channel_estimate(full_tmp, A, taps_g, dft_tmp);
                cx_t s = CX_ZERO;
                for (int d = 0; d < D; d++) s += dft_tmp[local_data[d]];
                w_dft_prg[g][p] = s / (double)D;
            }
            free(basis); free(full_tmp); free(dft_tmp);
            free(local_pilot); free(local_data);
        }
    }

    printf("%-9s  %-12s %-11s  %-8s  %-6s %-6s %-6s %-6s\n",
           "SNR(dB)", "BER", "BLER", "AvgRank", "R1%", "R2%", "R3%", "R4%");
    for (int i = 0; i < 76; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        cx_t *mmse_W = NULL;              /* wideband 필터 (rank 선택용) */
        cx_t **mmse_W_prg = NULL;         /* PRG별 필터 (프리코더 방향용) */
        if (est_mmse) {
            mmse_W = (cx_t *)malloc((size_t)num_pilots * sizeof(cx_t));
            mmse_build_avg_filter(pilot_pos, num_pilots, data_pos, nd,
                                   cfg->tdlDelaySpreadNs, scs_hz, N0, mmse_W);

            /* PRG별 필터는 그 PRG의 파일럿→그 PRG의 data RE 평균을 타깃으로
             * 하는 avg-최적 필터(2026-09-01, subband 프리코딩 검증 중 발견한
             * "point-최적 MMSE는 subband 평균 타깃에 최적이 아니다" 문제의
             * 정식 해결책 — mmse_build_avg_filter 문서 참조). */
            mmse_W_prg = (cx_t **)malloc(num_prg * sizeof(cx_t *));
            for (int g = 0; g < num_prg; g++) {
                int M = prg_n_pilots[g];
                int rb0 = prg_rb_start[g];
                int d0 = 6 * rb0, d1 = d0 + prg_n_data[g];
                if (d1 > nd) d1 = nd;
                int cnt = (d1 > d0) ? (d1 - d0) : 0;
                mmse_W_prg[g] = (cx_t *)malloc((size_t)M * sizeof(cx_t));
                if (cnt > 0)
                    mmse_build_avg_filter(&pilot_pos[6 * rb0], M, &data_pos[d0], cnt,
                                           cfg->tdlDelaySpreadNs, scs_hz, N0, mmse_W_prg[g]);
                else
                    for (int p = 0; p < M; p++) mmse_W_prg[g][p] = 1.0 / (double)M;
            }
        }

        long t_bits = 0, b_err = 0;
        int  e_blk = 0;
        long rank_cnt[5] = {0, 0, 0, 0, 0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① TDL 드로우 + H_cache(RE별 진짜 채널) 계산 */
            for (int r = 0; r < 4; r++)
                for (int t = 0; t < 16; t++)
                    tdl_draw(&tdl_ch, taps[r][t]);

            for (int d = 0; d < nd; d++) {
                int k = data_pos[d];
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 16; t++)
                        H_cache[d * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
            }

            /* ② wideband 채널 추정 → SVD + rank 적응 (RI는 wideband 1회 결정) */
            cx_t H_est_avg[4][16];
            if (est_none) {
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 16; t++) {
                        cx_t s = CX_ZERO;
                        for (int d = 0; d < nd; d++) s += H_cache[d * 4 + r][t];
                        H_est_avg[r][t] = s / (double)nd;
                    }
            } else {
                for (int r = 0; r < 4; r++) {
                    for (int t = 0; t < 16; t++) {
                        for (int p = 0; p < num_pilots; p++) {
                            cx_t htrue = tdl_freq_response(&tdl_ch, taps[r][t], pilot_pos[p]);
                            h_ls_pilot[p] = htrue + CX_MAKE(randn() * sigma, randn() * sigma);
                        }
                        if (est_ls) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += h_ls_pilot[p];
                            H_est_avg[r][t] = s / (double)num_pilots;
                        } else if (est_mmse) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += mmse_W[p] * h_ls_pilot[p];
                            H_est_avg[r][t] = s;
                        } else {   /* est_dft: wideband w_dft는 필요 없음 — rank 선택은
                                      전체 파일럿 단순평균으로도 충분(RI는 거친 결정이라
                                      wideband DFT 스무딩까지는 불필요, PRG별 DFT가 핵심) */
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += h_ls_pilot[p];
                            H_est_avg[r][t] = s / (double)num_pilots;
                        }
                    }
                }
            }

            double sv[4];
            cx_t Umat[4][4], Vmat[16][4];
            eigen_bf_16port_svd(H_est_avg, sv, Umat, Vmat);
            int rank;
            eigen_bf_16port_select_rank(sv, N0, &rank);
            rank_cnt[rank]++;
            rank_sum += rank;
            int nCW = rank;
            double pnorm = 1.0 / sqrt((double)rank);

            /* ③ PRG별 채널 추정 → SVD → 그 PRG의 프리코더(Wg_store[g], 이미
             *    정해진 rank만큼의 열) — wideband에서 고른 rank를 그대로 쓰되
             *    방향은 PRG 고유의 추정 채널에서 다시 계산. Wg_store는 함수
             *    상단에서 num_prg 크기로 이미 할당됨(g마다 이번 트라이얼 값으로
             *    덮어씀). */
            for (int g = 0; g < num_prg; g++) {
                int rb0 = prg_rb_start[g], M = prg_n_pilots[g];
                cx_t H_est_sb[4][16];

                if (est_none) {
                    /* 그 PRG의 데이터 RE만 평균 — 로컬 genie 상한 */
                    int d0 = 6 * rb0, d1 = d0 + prg_n_data[g];
                    if (d1 > nd) d1 = nd;   /* nd < num_data일 수 있음 */
                    int cnt = (d1 > d0) ? (d1 - d0) : 0;
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 16; t++) {
                            cx_t s = CX_ZERO;
                            for (int d = d0; d < d1; d++) s += H_cache[d * 4 + r][t];
                            H_est_sb[r][t] = (cnt > 0) ? s / (double)cnt : H_est_avg[r][t];
                        }
                } else {
                    for (int r = 0; r < 4; r++) {
                        for (int t = 0; t < 16; t++) {
                            for (int p = 0; p < M; p++) {
                                cx_t htrue = tdl_freq_response(&tdl_ch, taps[r][t], pilot_pos[6 * rb0 + p]);
                                h_ls_pilot[p] = htrue + CX_MAKE(randn() * sigma, randn() * sigma);
                            }
                            if (est_ls) {
                                cx_t s = CX_ZERO;
                                for (int p = 0; p < M; p++) s += h_ls_pilot[p];
                                H_est_sb[r][t] = s / (double)M;
                            } else if (est_mmse) {
                                cx_t s = CX_ZERO;
                                for (int p = 0; p < M; p++) s += mmse_W_prg[g][p] * h_ls_pilot[p];
                                H_est_sb[r][t] = s;
                            } else {   /* est_dft */
                                cx_t s = CX_ZERO;
                                for (int p = 0; p < M; p++) s += w_dft_prg[g][p] * h_ls_pilot[p];
                                H_est_sb[r][t] = s;
                            }
                        }
                    }
                }

                double sv_g[4];
                cx_t Umat_g[4][4], Vmat_g[16][4];
                eigen_bf_16port_svd(H_est_sb, sv_g, Umat_g, Vmat_g);
                for (int t = 0; t < 16; t++)
                    for (int c = 0; c < rank; c++)
                        Wg_store[g][t][c] = pnorm * Vmat_g[t][c];
            }

            /* ④ nCW개 CW 인코딩 */
            for (int c = 0; c < nCW; c++) {
                gen_random_bits(tb[c], tbsz);
                attach_crc(tb[c], tbsz, CRC24A, tb_crc[c]);
                nr_seg_split(tb_crc[c], &seg, cb_bits[c]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[c] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[c]);
                    nr_ldpc_rate_match_select(coded[c], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[c][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[c], Er, selbits[c]);
                qam_modulate(selbits[c], E, mcs.modulation, sym[c]);
            }

            /* ⑤ 노이즈 드로우 */
            for (int d = 0; d < nd; d++)
                for (int r = 0; r < 4; r++)
                    nbuf[d * 4 + r] = CX_MAKE(randn() * sigma, randn() * sigma);

            /* ⑥ 프리코딩(RE가 속한 PRG의 W 사용) + 검출 */
            double nv_sum[4] = {0.0, 0.0, 0.0, 0.0};

            if (rank == 1) {
                for (int d = 0; d < nd; d++) {
                    int g = (d / 6) / EIGEN16_PRG_SIZE_RB;
                    cx_t h_eff[4];
                    for (int r = 0; r < 4; r++) {
                        h_eff[r] = CX_ZERO;
                        for (int t = 0; t < 16; t++) h_eff[r] += H_cache[d * 4 + r][t] * Wg_store[g][t][0];
                    }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum[0] += nv;
                }
            } else if (rank == 2) {
                for (int d = 0; d < nd; d++) {
                    int g = (d / 6) / EIGEN16_PRG_SIZE_RB;
                    cx_t h_eff[4][2];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 2; c++) {
                            h_eff[r][c] = CX_ZERO;
                            for (int t = 0; t < 16; t++) h_eff[r][c] += H_cache[d * 4 + r][t] * Wg_store[g][t][c];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d] + nbuf[d * 4 + r];
                    cx_t xh[2]; double nv[2];
                    mimo_mmse_detect_4rx2(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1];
                }
            } else if (rank == 3) {
                for (int d = 0; d < nd; d++) {
                    int g = (d / 6) / EIGEN16_PRG_SIZE_RB;
                    cx_t h_eff[4][3];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 3; c++) {
                            h_eff[r][c] = CX_ZERO;
                            for (int t = 0; t < 16; t++) h_eff[r][c] += H_cache[d * 4 + r][t] * Wg_store[g][t][c];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + nbuf[d * 4 + r];
                    cx_t xh[3]; double nv[3];
                    mimo_mmse_detect_4rx3(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1]; rx_hat[2][d] = xh[2];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1]; nv_sum[2] += nv[2];
                }
            } else {   /* rank == 4 */
                for (int d = 0; d < nd; d++) {
                    int g = (d / 6) / EIGEN16_PRG_SIZE_RB;
                    cx_t h_eff[4][4];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 4; c++) {
                            h_eff[r][c] = CX_ZERO;
                            for (int t = 0; t < 16; t++) h_eff[r][c] += H_cache[d * 4 + r][t] * Wg_store[g][t][c];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + h_eff[r][3] * sym[3][d]
                               + nbuf[d * 4 + r];
                    cx_t xh[4]; double nv[4];
                    mimo_mmse_detect_4x4(h_eff, y, N0, xh, nv);
                    for (int c = 0; c < 4; c++) { rx_hat[c][d] = xh[c]; nv_sum[c] += nv[c]; }
                }
            }

            /* ⑦ 복조/복호 */
            int blk_err = 0;
            for (int c = 0; c < nCW; c++) {
                double env = nv_sum[c] / nd;
                qam_demap_llr(rx_hat[c], nd, mcs.modulation, env, allllr[c]);
                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[c], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[c], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[c] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[c], 25, decoded_cb[c]);
                    if (seg.C > 1 && !check_crc(decoded_cb[c], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[c] + (size_t)r * payload, decoded_cb[c], payload * sizeof(int));
                }
                int crc_c = cb_crc_ok && check_crc(decoded_tb[c], B, CRC24A);
                int be_c = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[c][i] != decoded_tb[c][i]) be_c++;
                b_err  += be_c;
                t_bits += tbsz;
                if (!crc_c || be_c > 0) blk_err = 1;
            }
            if (blk_err) e_blk++;
        }   /* end trial loop */

        double ber  = t_bits > 0 ? (double)b_err / t_bits : 0.0;
        double bler = e_blk / (double)cfg->numTrials;
        double avg_rank = rank_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-8.2f  %-6.1f %-6.1f %-6.1f %-6.1f\n",
               snr, ber, bler, avg_rank,
               rank_cnt[1] * 100.0 / cfg->numTrials,
               rank_cnt[2] * 100.0 / cfg->numTrials,
               rank_cnt[3] * 100.0 / cfg->numTrials,
               rank_cnt[4] * 100.0 / cfg->numTrials);

        if (mmse_W) free(mmse_W);
        if (mmse_W_prg) {
            for (int g = 0; g < num_prg; g++) free(mmse_W_prg[g]);
            free(mmse_W_prg);
        }
    }   /* end SNR loop */

    printf("\nPDSCH Eigen 16-port + TDL(%s CSI, Subband Precoding) simulation complete.\n",
           cfg->eigen16ChanEst);

    ldpc_free(&ldpc);
    free(data_pos); free(pilot_pos);
    free(prg_rb_start); free(prg_num_rb); free(prg_n_pilots); free(prg_n_active); free(prg_n_data);
    free(Wg_store);
    free(H_cache);
    free(nbuf); free(Er);
    free(h_ls_pilot);
    if (w_dft_prg) {
        for (int g = 0; g < num_prg; g++) free(w_dft_prg[g]);
        free(w_dft_prg);
    }
    for (int l = 0; l < 4; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(sym[l]); free(allllr[l]); free(soft_buf[l]);
        free(decoded_cb[l]); free(decoded_tb[l]);
        free(rx_hat[l]);
    }
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 16; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 32포트 Type I SP 코드북 기반 Closed-Loop PDSCH — TDL 주파수 선택적 페이딩
 *
 * run_pdsch_cl_32port_simulation()(평탄) 구조를 8-port TDL 버전과 동일한
 * 방식으로 TDL로 확장 — Wideband PMI(모든 data RE의 H를 Tx 상관 적용 후
 * 평균한 H_avg로 RI+PMI 선택, 320→5120 후보), "Adaptive" 단일 시나리오만
 * 보고(고정-rank 4개 시나리오는 flat 버전과 동일한 이유로 범위 제외).
 * Tx 상관은 RE별 H_cache[d]에 개별 적용 후 H_avg 누적(8-port/CL_4PORT TDL과
 * 동일 순서).
 * 채널 추정 : CHAN_EST_METHOD=NONE(genie, 기본값)/LS/MMSE/DFT — 4/8-port TDL과
 *             동일한 채널추정 체인. NONE이 아니면 wideband PMI 설계용 H_avg만
 *             추정치로 바뀌고 실제 하향링크는 항상 RE별 진짜 H_cache로 통과.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_32port_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int num_pilots = 6 * num_rb;
    int num_active = 12 * num_rb;
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    dmrs_data_indices(num_rb, data_pos);
    dmrs_pilot_indices(num_rb, pilot_pos);

    int est_none = (strcmp(cfg->chanEstMethod, "NONE") == 0);
    int est_ls   = (strcmp(cfg->chanEstMethod, "LS")   == 0);
    int est_mmse = (strcmp(cfg->chanEstMethod, "MMSE") == 0);
    int est_dft  = (strcmp(cfg->chanEstMethod, "DFT")  == 0);

    double scs_hz = (double)cfg->scsKHz * 1000.0;
    int dft_num_taps = 0;
    if (est_dft) {
        double t_sample = 1.0 / ((double)num_active * scs_hz);
        dft_num_taps = (int)ceil(8.0 * cfg->tdlDelaySpreadNs * 1e-9 / t_sample);
        if (dft_num_taps < 2) dft_num_taps = 2;
        if (dft_num_taps > num_active) dft_num_taps = num_active;
    }

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by all up-to-4 CWs (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                     length G per CW */
    int nd   = num_data;         /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by all CWs (same E) */

    printf("=== PDSCH Closed-Loop 32-port Codebook (RI+PMI Adaptive, rank 1~4), TDL Fading ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 32 / 4  (64 물리 소자 = 32포트 × 2편파)\n");
    printf("Codebook     : TS 38.214 Type I SP (N1=4, N2=4, O1=4, O2=4, Ng=2)\n");
    printf("Rank Range   : 1~4  (RI+PMI 자동 선택, Wideband H_avg 기준, 5120 후보)\n");
    printf("Tx Corr      : rho_h=%.2f, rho_v=%.2f, rho_xpol=%.2f (%s, Kronecker 2D)\n",
           cfg->spatialCorrTx, cfg->spatialCorrTxVert, cfg->spatialCorrXpol,
           (cfg->spatialCorrTx > 0.0 || cfg->spatialCorrTxVert > 0.0 || cfg->spatialCorrXpol > 0.0)
               ? "공간상관" : "i.i.d.");
    printf("Channel      : TDL per Tx-Rx pair (128 tap-sets, DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Chan Est     : %s\n", cfg->chanEstMethod);
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Data RE/CW   : %d\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[4], *tb_crc[4], *cb_bits[4], *coded[4], **rm[4], *selbits[4];
    int   *decoded_cb[4], *decoded_tb[4];
    cx_t  *sym[4];
    double *allllr[4], *soft_buf[4];
    cx_t  *rx_hat[4];
    for (int l = 0; l < 4; l++) {
        tb[l]      = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[l]  = (int    *)malloc(B     * sizeof(int));
        cb_bits[l] = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[l]   = (int    *)malloc(acsz  * sizeof(int));
        rm[l]      = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[l] = (int    *)malloc(E     * sizeof(int));
        sym[l]     = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[l]  = (double *)malloc(E     * sizeof(double));
        soft_buf[l]= (double *)malloc(acsz  * sizeof(double));
        decoded_cb[l] = (int *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int *)malloc(B * sizeof(int));
        rx_hat[l]  = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc(nd * 4 * sizeof(cx_t));

    cx_t (*H_cache)[32] = (cx_t (*)[32])malloc((size_t)nd * 4 * sizeof(*H_cache));

    cx_t *taps[4][32];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 32; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    /* 채널추정용 버퍼: 파일럿 위치별 4×32 상관채널 + (r,t) 쌍별 파일럿 시퀀스 */
    cx_t (*Hp_full)[32] = est_none ? NULL : malloc((size_t)num_pilots * 4 * sizeof(*Hp_full));
    cx_t *h_ls_pilot  = est_none ? NULL : (cx_t *)malloc(num_pilots * sizeof(cx_t));

    cx_t *w_dft = NULL;
    if (est_dft) {
        w_dft = (cx_t *)malloc(num_pilots * sizeof(cx_t));
        cx_t *basis    = (cx_t *)malloc(num_pilots * sizeof(cx_t));
        cx_t *full_tmp = (cx_t *)malloc(num_active * sizeof(cx_t));
        cx_t *dft_tmp  = (cx_t *)malloc(num_active * sizeof(cx_t));
        for (int p = 0; p < num_pilots; p++) {
            for (int q = 0; q < num_pilots; q++) basis[q] = (p == q) ? 1.0 : 0.0;
            interpolate_channel(basis, num_pilots, pilot_pos, num_active, full_tmp);
            dft_channel_estimate(full_tmp, num_active, dft_num_taps, dft_tmp);
            cx_t s = CX_ZERO;
            for (int d = 0; d < num_data; d++) s += dft_tmp[data_pos[d]];
            w_dft[p] = s / (double)num_data;
        }
        free(basis); free(full_tmp); free(dft_tmp);
    }

    printf("%-9s  %-12s %-11s  %-8s  %-6s %-6s %-6s %-6s\n",
           "SNR(dB)", "BER", "BLER", "AvgRank", "R1%", "R2%", "R3%", "R4%");
    for (int i = 0; i < 76; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        cx_t *mmse_W = NULL;
        if (est_mmse) {
            mmse_W = (cx_t *)malloc((size_t)num_pilots * sizeof(cx_t));
            mmse_build_avg_filter(pilot_pos, num_pilots, data_pos, nd,
                                   cfg->tdlDelaySpreadNs, scs_hz, N0, mmse_W);
        }

        long t_bits = 0, b_err = 0;
        int  e_blk = 0;
        long rank_cnt[5] = {0, 0, 0, 0, 0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① TDL 드로우 + H_cache(RE별 진짜 채널) 계산 */
            for (int r = 0; r < 4; r++)
                for (int t = 0; t < 32; t++)
                    tdl_draw(&tdl_ch, taps[r][t]);

            for (int d = 0; d < nd; d++) {
                int k = data_pos[d];
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 32; t++)
                        H_cache[d * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                mimo_apply_tx_correlation_4x32(&H_cache[d * 4], cfg->spatialCorrTx,
                                                cfg->spatialCorrTxVert, cfg->spatialCorrXpol);
            }

            /* ② 프리코더 설계용 wideband 채널 (H_avg) — genie 또는 파일럿 기반 추정 */
            cx_t H_avg[4][32];
            if (est_none) {
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 32; t++) {
                        cx_t s = CX_ZERO;
                        for (int d = 0; d < nd; d++) s += H_cache[d * 4 + r][t];
                        H_avg[r][t] = s / (double)nd;
                    }
            } else {
                for (int p = 0; p < num_pilots; p++) {
                    int k = pilot_pos[p];
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 32; t++)
                            Hp_full[p * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                    mimo_apply_tx_correlation_4x32(&Hp_full[p * 4], cfg->spatialCorrTx,
                                                    cfg->spatialCorrTxVert, cfg->spatialCorrXpol);
                }
                for (int r = 0; r < 4; r++) {
                    for (int t = 0; t < 32; t++) {
                        for (int p = 0; p < num_pilots; p++)
                            h_ls_pilot[p] = Hp_full[p * 4 + r][t] + CX_MAKE(randn() * sigma, randn() * sigma);

                        if (est_ls) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += h_ls_pilot[p];
                            H_avg[r][t] = s / (double)num_pilots;
                        } else if (est_mmse) {
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += mmse_W[p] * h_ls_pilot[p];
                            H_avg[r][t] = s;
                        } else {   /* est_dft */
                            cx_t s = CX_ZERO;
                            for (int p = 0; p < num_pilots; p++) s += w_dft[p] * h_ls_pilot[p];
                            H_avg[r][t] = s;
                        }
                    }
                }
            }

            /* ③ Wideband RI+PMI 선택 (rank 1~4, 5120 후보) */
            int rank, l1, l2, i13, i2;
            codebook_type1_sp_32port_ri_pmi_select(H_avg, N0, &rank, &l1, &l2, &i13, &i2);
            rank_cnt[rank]++;
            rank_sum += rank;
            int nCW = rank;

            /* ③ nCW개 CW 인코딩 */
            for (int c = 0; c < nCW; c++) {
                gen_random_bits(tb[c], tbsz);
                attach_crc(tb[c], tbsz, CRC24A, tb_crc[c]);
                nr_seg_split(tb_crc[c], &seg, cb_bits[c]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[c] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[c]);
                    nr_ldpc_rate_match_select(coded[c], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[c][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[c], Er, selbits[c]);
                qam_modulate(selbits[c], E, mcs.modulation, sym[c]);
            }

            /* ④ 노이즈 드로우 */
            for (int d = 0; d < nd; d++)
                for (int r = 0; r < 4; r++)
                    nbuf[d * 4 + r] = CX_MAKE(randn() * sigma, randn() * sigma);

            /* ⑤ 프리코딩(진짜 H_cache 사용, wideband PMI 대 주파수선택적 채널
             *    불일치 반영) + 검출 (rank별 분기) */
            double nv_sum[4] = {0.0, 0.0, 0.0, 0.0};

            if (rank == 1) {
                cx_t W[32];
                codebook_type1_sp_32port_rank1(l1, l2, i2, W);
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4];
                    for (int r = 0; r < 4; r++) {
                        h_eff[r] = CX_ZERO;
                        for (int t = 0; t < 32; t++) h_eff[r] += H_cache[d * 4 + r][t] * W[t];
                    }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r] * sym[0][d] + nbuf[d * 4 + r];
                    cx_t xh; double nv;
                    mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                    rx_hat[0][d] = xh;
                    nv_sum[0] += nv;
                }
            } else if (rank == 2) {
                cx_t W[32][2];
                codebook_type1_sp_32port_rank2(l1, l2, i13, i2, W);
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4][2];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 2; c++) {
                            h_eff[r][c] = CX_ZERO;
                            for (int t = 0; t < 32; t++) h_eff[r][c] += H_cache[d * 4 + r][t] * W[t][c];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d] + nbuf[d * 4 + r];
                    cx_t xh[2]; double nv[2];
                    mimo_mmse_detect_4rx2(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1];
                }
            } else if (rank == 3) {
                cx_t W[32][3];
                codebook_type1_sp_32port_rank3(l1, l2, i13, i2, W);
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4][3];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 3; c++) {
                            h_eff[r][c] = CX_ZERO;
                            for (int t = 0; t < 32; t++) h_eff[r][c] += H_cache[d * 4 + r][t] * W[t][c];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + nbuf[d * 4 + r];
                    cx_t xh[3]; double nv[3];
                    mimo_mmse_detect_4rx3(h_eff, y, N0, xh, nv);
                    rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1]; rx_hat[2][d] = xh[2];
                    nv_sum[0] += nv[0]; nv_sum[1] += nv[1]; nv_sum[2] += nv[2];
                }
            } else {   /* rank == 4 */
                cx_t W[32][4];
                codebook_type1_sp_32port_rank4(l1, l2, i13, i2, W);
                for (int d = 0; d < nd; d++) {
                    cx_t h_eff[4][4];
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 4; c++) {
                            h_eff[r][c] = CX_ZERO;
                            for (int t = 0; t < 32; t++) h_eff[r][c] += H_cache[d * 4 + r][t] * W[t][c];
                        }
                    cx_t y[4];
                    for (int r = 0; r < 4; r++)
                        y[r] = h_eff[r][0] * sym[0][d] + h_eff[r][1] * sym[1][d]
                               + h_eff[r][2] * sym[2][d] + h_eff[r][3] * sym[3][d]
                               + nbuf[d * 4 + r];
                    cx_t xh[4]; double nv[4];
                    mimo_mmse_detect_4x4(h_eff, y, N0, xh, nv);
                    for (int c = 0; c < 4; c++) { rx_hat[c][d] = xh[c]; nv_sum[c] += nv[c]; }
                }
            }

            /* ⑥ 복조/복호 */
            int blk_err = 0;
            for (int c = 0; c < nCW; c++) {
                double env = nv_sum[c] / nd;
                qam_demap_llr(rx_hat[c], nd, mcs.modulation, env, allllr[c]);
                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[c], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[c], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[c] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[c], 25, decoded_cb[c]);
                    if (seg.C > 1 && !check_crc(decoded_cb[c], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[c] + (size_t)r * payload, decoded_cb[c], payload * sizeof(int));
                }
                int crc_c = cb_crc_ok && check_crc(decoded_tb[c], B, CRC24A);
                int be_c = 0;
                for (int i = 0; i < tbsz; i++)
                    if (tb[c][i] != decoded_tb[c][i]) be_c++;
                b_err  += be_c;
                t_bits += tbsz;
                if (!crc_c || be_c > 0) blk_err = 1;
            }
            if (blk_err) e_blk++;
        }   /* end trial loop */

        double ber  = t_bits > 0 ? (double)b_err / t_bits : 0.0;
        double bler = e_blk / (double)cfg->numTrials;
        double avg_rank = rank_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-8.2f  %-6.1f %-6.1f %-6.1f %-6.1f\n",
               snr, ber, bler, avg_rank,
               rank_cnt[1] * 100.0 / cfg->numTrials,
               rank_cnt[2] * 100.0 / cfg->numTrials,
               rank_cnt[3] * 100.0 / cfg->numTrials,
               rank_cnt[4] * 100.0 / cfg->numTrials);

        if (mmse_W) free(mmse_W);
    }   /* end SNR loop */

    printf("\nPDSCH CL 32-port + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(data_pos); free(pilot_pos);
    free(H_cache);
    free(nbuf); free(Er);
    if (Hp_full) free(Hp_full);
    if (h_ls_pilot) free(h_ls_pilot);
    if (w_dft) free(w_dft);
    for (int l = 0; l < 4; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
        for (int r = 0; r < seg.C; r++) free(rm[l][r]);
        free(rm[l]); free(selbits[l]);
        free(sym[l]); free(allllr[l]); free(soft_buf[l]);
        free(decoded_cb[l]); free(decoded_tb[l]);
        free(rx_hat[l]);
    }
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 32; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 32포트 Type I SP 코드북 기반 Closed-Loop PDSCH — HARQ Circular Buffer
 * 지원 채널: FLAT_FADING / TDL (모두 genie-aided)
 *
 * 8-port HARQ 버전 구조를 rank 1~4로 확장(최대 4개 CW). RI+PMI는 시도 0에서
 * H_avg(TDL) 또는 H_flat0(FLAT) 기준으로 결정해 이후 재전송까지 고정 —
 * 5G NR HARQ 재전송은 동일 프리코더를 유지하는 실제 동작과 일치. 채널
 * 자체는 시도마다 재추첨(시간 다이버시티). CW 수 = rank(1~4).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_cl_32port_harq_simulation(const L1Config *cfg) {
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

    /* TS 38.212 5.2.2 multi-code-block segmentation (P0-2c HARQ follow-up,
     * 2026-09-03) -- see run_pdsch_harq_simulation() design note. All 4
     * possible CWs share identical tbsz/cr -> one NRSegInfo/LDPCCodec/
     * Er[] covers all of them. */
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;

    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PDSCH CL 32-port (HARQ Circular Buffer %s), %s ===\n",
           is_chase ? "Chase" : "IR",
           is_tdl ? "TDL Frequency-Selective Fading" : "Flat Fading");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d/CB, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)seg.Kprime / acsz, acsz, ldpc.bg, ldpc.Zc);
    printf("Code Blocks  : C=%d/CW%s\n", seg.C, seg.C > 1 ? " (CRC24B/CB)" : "");
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Rx Ants   : 32 / 4  (64 물리 소자 = 32포트 × 2편파)\n");
    printf("Codebook     : TS 38.214 Type I SP (N1=4, N2=4, O1=4, O2=4, Ng=2)\n");
    printf("Rank Range   : 1~4  (시도 0에서 H_avg 기반 선택, 이후 고정)\n");
    printf("Tx Corr      : rho_h=%.2f, rho_v=%.2f, rho_xpol=%.2f (%s, Kronecker 2D)\n",
           cfg->spatialCorrTx, cfg->spatialCorrTxVert, cfg->spatialCorrXpol,
           (cfg->spatialCorrTx > 0.0 || cfg->spatialCorrTxVert > 0.0 || cfg->spatialCorrXpol > 0.0)
               ? "공간상관" : "i.i.d.");
    printf("TB Size/CW   : %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : TDL per Tx-Rx pair (128 tap-sets, DS=%.0fns, %d taps, genie-aided)\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    else
        printf("Channel      : Flat Fading 4x32 (block-flat, redrawn per HARQ attempt, genie-aided)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[4], *tb_crc[4], *cb_bits[4], **coded_cw[4], **rm[4], *selbits[4];
    int   *decoded_cb[4], *decoded_tb[4];
    cx_t  *sym[4], *rx_hat[4];
    double *allllr[4], **soft_buf[4];
    for (int l = 0; l < 4; l++) {
        tb[l]         = (int   *)malloc(tbsz    * sizeof(int));
        tb_crc[l]     = (int   *)malloc(B       * sizeof(int));
        cb_bits[l]    = (int   *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded_cw[l]   = (int  **)malloc(seg.C   * sizeof(int *));
        rm[l]         = (int  **)malloc(seg.C   * sizeof(int *));
        for (int r = 0; r < seg.C; r++) {
            coded_cw[l][r] = (int *)malloc(acsz  * sizeof(int));
            rm[l][r]       = (int *)malloc(Er[r] * sizeof(int));
        }
        selbits[l]    = (int   *)malloc(E       * sizeof(int));
        decoded_cb[l] = (int   *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[l] = (int   *)malloc(B       * sizeof(int));
        sym[l]        = (cx_t  *)malloc(num_data * sizeof(cx_t));
        rx_hat[l]     = (cx_t  *)malloc(num_data * sizeof(cx_t));
        allllr[l]     = (double *)malloc(E      * sizeof(double));
        soft_buf[l]   = (double **)malloc(seg.C * sizeof(double *));
        for (int r = 0; r < seg.C; r++) soft_buf[l][r] = (double *)malloc(acsz * sizeof(double));
    }
    cx_t (*H_cache)[32] = (cx_t (*)[32])malloc((size_t)num_data * 4 * sizeof(*H_cache));
    cx_t *taps[4][32];
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 32; t++)
            taps[r][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%10s%14s%14s%14s%12s%8s\n",
           "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx", "AvgRank");
    for (int i = 0; i < 72; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        int total_err=0, total_bits=0, blk_err_final=0, blk_err_1st=0;
        long long total_attempts = 0;
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            for (int l = 0; l < 4; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    ldpc_encode(&ldpc, cb_bits[l] + (size_t)r * seg.Kprime, coded_cw[l][r]);
                    for (int i = 0; i < acsz; i++) soft_buf[l][r][i] = 0.0;
                }
            }

            /* RI+PMI 선택: 시도 0에서 결정하여 이후 고정 */
            int rank_fix, l1_fix, l2_fix, i13_fix, i2_fix;
            cx_t W1_fix[32];       /* rank-1 */
            cx_t W2_fix[32][2];    /* rank-2 */
            cx_t W3_fix[32][3];    /* rank-3 */
            cx_t W4_fix[32][4];    /* rank-4 */

            cx_t H_flat0[4][32];
            if (!is_tdl) {
                mimo_channel_draw_4x32(H_flat0);
                mimo_apply_tx_correlation_4x32(H_flat0, cfg->spatialCorrTx,
                                                cfg->spatialCorrTxVert, cfg->spatialCorrXpol);
                codebook_type1_sp_32port_ri_pmi_select(H_flat0, N0, &rank_fix, &l1_fix, &l2_fix, &i13_fix, &i2_fix);
            } else {
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 32; t++)
                        tdl_draw(&tdl_ch, taps[r][t]);
                cx_t H_avg[4][32];
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 32; t++) H_avg[r][t] = CX_ZERO;
                for (int d = 0; d < num_data; d++) {
                    int k = data_pos[d];
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 32; t++)
                            H_cache[d * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                    mimo_apply_tx_correlation_4x32(&H_cache[d * 4], cfg->spatialCorrTx,
                                                    cfg->spatialCorrTxVert, cfg->spatialCorrXpol);
                    for (int r = 0; r < 4; r++)
                        for (int t = 0; t < 32; t++)
                            H_avg[r][t] += H_cache[d * 4 + r][t];
                }
                double inv = 1.0 / num_data;
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 32; t++) H_avg[r][t] *= inv;
                codebook_type1_sp_32port_ri_pmi_select(H_avg, N0, &rank_fix, &l1_fix, &l2_fix, &i13_fix, &i2_fix);
            }
            rank_sum += rank_fix;
            switch (rank_fix) {
                case 1: codebook_type1_sp_32port_rank1(l1_fix, l2_fix, i2_fix, W1_fix); break;
                case 2: codebook_type1_sp_32port_rank2(l1_fix, l2_fix, i13_fix, i2_fix, W2_fix); break;
                case 3: codebook_type1_sp_32port_rank3(l1_fix, l2_fix, i13_fix, i2_fix, W3_fix); break;
                default: codebook_type1_sp_32port_rank4(l1_fix, l2_fix, i13_fix, i2_fix, W4_fix); break;
            }
            int nCW = rank_fix;

            int crc_ok[4]={0,0,0,0}, be[4]={0,0,0,0}, be_1st[4]={0,0,0,0}, crc_1st[4]={0,0,0,0};
            int attempts = 0;
            int attempt0_done = 1;

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int l = 0; l < nCW; l++) {
                    for (int r = 0; r < seg.C; r++)
                        nr_ldpc_rate_match_select(coded_cw[l][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], rm[l][r]);
                    nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
                    qam_modulate(selbits[l], E, mcs.modulation, sym[l]);
                }

                if (attempt > 0 || !attempt0_done) {
                    if (!is_tdl) {
                        mimo_channel_draw_4x32(H_flat0);
                        mimo_apply_tx_correlation_4x32(H_flat0, cfg->spatialCorrTx,
                                                        cfg->spatialCorrTxVert, cfg->spatialCorrXpol);
                    } else {
                        for (int r = 0; r < 4; r++)
                            for (int t = 0; t < 32; t++)
                                tdl_draw(&tdl_ch, taps[r][t]);
                        for (int d = 0; d < num_data; d++) {
                            int k = data_pos[d];
                            for (int r = 0; r < 4; r++)
                                for (int t = 0; t < 32; t++)
                                    H_cache[d * 4 + r][t] = tdl_freq_response(&tdl_ch, taps[r][t], k);
                            mimo_apply_tx_correlation_4x32(&H_cache[d * 4], cfg->spatialCorrTx,
                                                            cfg->spatialCorrTxVert, cfg->spatialCorrXpol);
                        }
                    }
                }
                attempt0_done = 0;

                double nv_sum[4] = {0.0, 0.0, 0.0, 0.0};

                for (int d = 0; d < num_data; d++) {
                    cx_t tx[4];
                    for (int l = 0; l < nCW; l++) tx[l] = sym[l][d];
                    cx_t noise[4];
                    for (int r = 0; r < 4; r++) noise[r] = CX_MAKE(randn()*sigma, randn()*sigma);

                    if (rank_fix == 1) {
                        cx_t h_eff[4];
                        for (int r = 0; r < 4; r++) {
                            h_eff[r] = CX_ZERO;
                            const cx_t *Hrow = is_tdl ? H_cache[d * 4 + r] : H_flat0[r];
                            for (int t = 0; t < 32; t++) h_eff[r] += Hrow[t] * W1_fix[t];
                        }
                        cx_t y[4];
                        for (int r = 0; r < 4; r++) y[r] = h_eff[r] * tx[0] + noise[r];
                        cx_t xh; double nv;
                        mrc_combine_4rx(h_eff, y, N0, &xh, &nv);
                        rx_hat[0][d] = xh;
                        nv_sum[0] += nv;
                    } else if (rank_fix == 2) {
                        cx_t h_eff[4][2];
                        for (int r = 0; r < 4; r++) {
                            const cx_t *Hrow = is_tdl ? H_cache[d * 4 + r] : H_flat0[r];
                            for (int c = 0; c < 2; c++) {
                                h_eff[r][c] = CX_ZERO;
                                for (int t = 0; t < 32; t++) h_eff[r][c] += Hrow[t] * W2_fix[t][c];
                            }
                        }
                        cx_t y[4];
                        for (int r = 0; r < 4; r++)
                            y[r] = h_eff[r][0]*tx[0] + h_eff[r][1]*tx[1] + noise[r];
                        cx_t xh[2]; double nv[2];
                        mimo_mmse_detect_4rx2(h_eff, y, N0, xh, nv);
                        rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1];
                        nv_sum[0] += nv[0]; nv_sum[1] += nv[1];
                    } else if (rank_fix == 3) {
                        cx_t h_eff[4][3];
                        for (int r = 0; r < 4; r++) {
                            const cx_t *Hrow = is_tdl ? H_cache[d * 4 + r] : H_flat0[r];
                            for (int c = 0; c < 3; c++) {
                                h_eff[r][c] = CX_ZERO;
                                for (int t = 0; t < 32; t++) h_eff[r][c] += Hrow[t] * W3_fix[t][c];
                            }
                        }
                        cx_t y[4];
                        for (int r = 0; r < 4; r++)
                            y[r] = h_eff[r][0]*tx[0] + h_eff[r][1]*tx[1] + h_eff[r][2]*tx[2] + noise[r];
                        cx_t xh[3]; double nv[3];
                        mimo_mmse_detect_4rx3(h_eff, y, N0, xh, nv);
                        rx_hat[0][d] = xh[0]; rx_hat[1][d] = xh[1]; rx_hat[2][d] = xh[2];
                        nv_sum[0] += nv[0]; nv_sum[1] += nv[1]; nv_sum[2] += nv[2];
                    } else {   /* rank_fix == 4 */
                        cx_t h_eff[4][4];
                        for (int r = 0; r < 4; r++) {
                            const cx_t *Hrow = is_tdl ? H_cache[d * 4 + r] : H_flat0[r];
                            for (int c = 0; c < 4; c++) {
                                h_eff[r][c] = CX_ZERO;
                                for (int t = 0; t < 32; t++) h_eff[r][c] += Hrow[t] * W4_fix[t][c];
                            }
                        }
                        cx_t y[4];
                        for (int r = 0; r < 4; r++)
                            y[r] = h_eff[r][0]*tx[0] + h_eff[r][1]*tx[1]
                                   + h_eff[r][2]*tx[2] + h_eff[r][3]*tx[3] + noise[r];
                        cx_t xh[4]; double nv[4];
                        mimo_mmse_detect_4x4(h_eff, y, N0, xh, nv);
                        for (int c = 0; c < 4; c++) { rx_hat[c][d] = xh[c]; nv_sum[c] += nv[c]; }
                    }
                }

                for (int l = 0; l < nCW; l++) {
                    double env = nv_sum[l] / num_data;
                    qam_demap_llr(rx_hat[l], num_data, mcs.modulation, env, allllr[l]);
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        nr_ldpc_rate_match_combine(soft_buf[l][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], allllr[l] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[l][r], 25, decoded_cb[l]);
                        if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B)) cb_crc_ok = 0;
                        memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                    }
                    crc_ok[l] = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                    int biterr = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[l][i] != decoded_tb[l][i]) biterr++;
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
        double avg_rank  = rank_sum / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f%8.2f\n",
               snr, ber, bler_1st, bler_final, avg_tx, avg_rank);
    }
    printf("\nPDSCH CL 32-port + %s + HARQ simulation complete.\n",
           is_tdl ? "TDL" : "Flat Fading");

    ldpc_free(&ldpc);
    free(data_pos);
    free(H_cache);
    free(Er);
    for (int l = 0; l < 4; l++) {
        free(tb[l]); free(tb_crc[l]); free(cb_bits[l]);
        for (int r = 0; r < seg.C; r++) { free(coded_cw[l][r]); free(rm[l][r]); free(soft_buf[l][r]); }
        free(coded_cw[l]); free(rm[l]); free(soft_buf[l]);
        free(selbits[l]); free(decoded_cb[l]); free(decoded_tb[l]);
        free(sym[l]); free(rx_hat[l]);
        free(allllr[l]);
    }
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 32; t++)
            free(taps[r][t]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * OLLA (Outer Loop Link Adaptation) — SISO, AWGN 고정 SNR 시계열
 *
 * 이 프로젝트의 나머지 PDSCH 함수들과 근본적으로 다른 구조: 다른 함수는
 * "SNR을 쓸어가며 고정 MCS의 BLER"을 보는 반면, 이 함수는 "고정된 실제
 * 채널 SNR(cfg->snrStart, sweep 무시)에서 MCS 자체가 시간에 따라 적응"
 * 하는 시계열 시뮬레이션 — olla.h 문서 참조.
 *
 * 두 pass를 동일한 SNR/트라이얼 수로 비교:
 *   Pass 1 (Open-Loop) : 매 트라이얼 offset=0으로 고정 — olla_select_mcs의
 *     Shannon+구현마진 근사만으로 MCS를 고른다. 이 근사가 실제(LDPC+QAM
 *     체인의 진짜) BLER과 정확히 맞을 이유가 없으므로 목표 BLER에서
 *     벗어날 수 있음 — OLLA가 해결하려는 문제 그 자체를 보여준다.
 *   Pass 2 (OLLA)       : 매 트라이얼 CRC 결과로 offset을 누적 갱신,
 *     MCS 선택에 반영. 실제 채널 SNR은 두 pass 모두 동일(cfg->snrStart로
 *     고정) — 유일한 차이는 MCS 선택에 오프셋 피드백을 쓰는지 여부.
 *
 * 리소스 할당(num_data)은 두 pass 내내 고정, MCS(따라서 TBS/코드율)만
 * 트라이얼마다 재계산 — 이 프로젝트의 다른 함수들이 쓰는 "num_data·Qm·R"
 * TBS 유도 관례와 동일(genie-aided pilot 오버헤드 없음, 채널추정과는
 * 무관한 순수 링크적응 연구이므로 DMRS 구조 자체를 모델링하지 않음).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_olla_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 12 * num_rb;   /* DMRS 분리 없음 — 순수 링크적응 연구 */
    int crc_bits = 24;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    double snr   = cfg->snrStart;   /* OLLA는 시계열 — SNR sweep 무시, 시작값 1개만 사용 */
    double N0    = 1.0 / pow(10.0, snr / 10.0);
    double sigma = sqrt(N0 / 2.0);

    printf("=== PDSCH OLLA (Outer Loop Link Adaptation), AWGN 고정 SNR 시계열 ===\n");
    printf("MCS Table    : %s\n", cfg->mcsTableType);
    printf("Fixed SNR    : %.1f dB (SNR sweep 무시, 시계열 시뮬레이션)\n", snr);
    printf("Num RB       : %d  (Data RE: %d, DMRS 미모델링)\n", num_rb, num_data);
    printf("BLER Target  : %.2f\n", cfg->ollaBlerTarget);
    printf("Step Down    : %.2f dB (Step Up = %.4f dB, 목표 BLER 수렴 조건)\n",
           cfg->ollaStepDownDb, cfg->ollaStepDownDb * cfg->ollaBlerTarget / (1.0 - cfg->ollaBlerTarget));
    printf("SNR Gap      : %.1f dB (Shannon 대비 구현 마진, 구현 정의)\n", cfg->ollaSnrGapDb);
    printf("Trials       : %d (per pass)\n\n", cfg->numTrials);

    int print_interval = cfg->numTrials / 20;
    if (print_interval < 1) print_interval = 1;

    for (int pass = 0; pass < 2; pass++) {
        int is_olla = (pass == 1);
        OLLAState olla;
        olla_init(&olla, cfg->ollaBlerTarget, cfg->ollaStepDownDb);

        printf("--- Pass %d: %s ---\n", pass + 1, is_olla ? "OLLA (폐루프 오프셋 적응)" : "Open-Loop (오프셋 고정 0)");
        printf("%-8s  %-6s  %-9s  %-11s  %-10s\n", "Trial", "MCS", "Offset(dB)", "WindowBLER", "CumBLER");
        for (int i = 0; i < 52; i++) printf("-");
        printf("\n");

        long total_bits = 0, total_err = 0;
        int  blk_err_cum = 0, blk_err_win = 0;
        long mcs_sum = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {
            double eff_snr = snr + (is_olla ? olla.offset_db : 0.0);
            int mcs_idx = olla_select_mcs(eff_snr, cfg->ollaSnrGapDb, tbl);
            MCSEntry mcs = get_mcs_entry(mcs_idx, tbl);
            double cr  = get_code_rate(&mcs);
            int    bps = mcs.modulationOrder;
            mcs_sum += mcs_idx;

            int max_dbits = num_data * bps;
            int tbsz = (int)(max_dbits * cr);
            if (tbsz < 1)    tbsz = 1;
            int B = tbsz + crc_bits;

            NRSegInfo seg; nr_seg_compute(B, cr, &seg);
            int payload = seg.Kprime - seg.L;
            LDPCCodec ldpc;
            ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                                seg.base_rows, seg.base_info_cols, seg.base_cols,
                                seg.filler_size);
            int acsz = ldpc.coded_size;
            int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                             length G for the whole TB */
            int *Er = (int *)malloc(seg.C * sizeof(int));
            nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

            int   *tb      = (int *)malloc(tbsz  * sizeof(int));
            int   *tb_crc  = (int *)malloc(B     * sizeof(int));
            int   *cb_bits = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
            int   *coded   = (int *)malloc(acsz  * sizeof(int));
            int  **rm      = (int **)malloc(seg.C * sizeof(int *));
            for (int r = 0; r < seg.C; r++) rm[r] = (int *)malloc(Er[r] * sizeof(int));
            int   *selbits = (int *)malloc(E     * sizeof(int));
            cx_t  *sym     = (cx_t *)malloc(num_data * sizeof(cx_t));
            cx_t  *rxsym   = (cx_t *)malloc(num_data * sizeof(cx_t));
            double *allllr = (double *)malloc(E     * sizeof(double));
            double *soft_buf = (double *)malloc(acsz * sizeof(double));
            int   *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));
            int   *decoded_tb = (int *)malloc(B * sizeof(int));

            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            nr_seg_split(tb_crc, &seg, cb_bits);
            for (int r = 0; r < seg.C; r++) {
                const int *info = cb_bits + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, info, coded);
                nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rm[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
            qam_modulate(selbits, E, mcs.modulation, sym);

            for (int d = 0; d < num_data; d++)
                rxsym[d] = sym[d] + CX_MAKE(randn() * sigma, randn() * sigma);

            qam_demap_llr(rxsym, num_data, mcs.modulation, N0, allllr);

            int roff = 0;
            int cb_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_buf, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllr + roff);
                roff += Er[r];
                ldpc_decode(&ldpc, soft_buf, 25, decoded_cb);
                if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B))
                    cb_crc_ok = 0;
                memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
            }

            int crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);
            int be = 0;
            for (int i = 0; i < tbsz; i++) if (tb[i] != decoded_tb[i]) be++;
            int ack = (crc_ok && be == 0);

            total_bits += tbsz;
            total_err  += be;
            if (!ack) { blk_err_cum++; blk_err_win++; }

            if (is_olla) olla_update(&olla, ack);

            if ((trial + 1) % print_interval == 0 || trial == cfg->numTrials - 1) {
                double win_bler = (double)blk_err_win / print_interval;
                double cum_bler = (double)blk_err_cum / (trial + 1);
                printf("%-8d  %-6d  %-9.2f  %-11.4f  %-10.4f\n",
                       trial + 1, mcs_idx, is_olla ? olla.offset_db : 0.0, win_bler, cum_bler);
                blk_err_win = 0;
            }

            ldpc_free(&ldpc);
            free(tb); free(tb_crc); free(cb_bits); free(coded);
            for (int r = 0; r < seg.C; r++) free(rm[r]);
            free(rm); free(Er); free(selbits);
            free(sym); free(rxsym); free(allllr); free(soft_buf);
            free(decoded_cb); free(decoded_tb);
        }   /* end trial loop */

        double final_ber  = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double final_bler = (double)blk_err_cum / cfg->numTrials;
        double avg_mcs    = (double)mcs_sum / cfg->numTrials;
        printf("\n%s 최종: BER=%.4e  BLER=%.4f (목표 %.2f)  평균 MCS=%.2f\n\n",
               is_olla ? "OLLA" : "Open-Loop", final_ber, final_bler, cfg->ollaBlerTarget, avg_mcs);
    }   /* end pass loop */

    printf("PDSCH OLLA simulation complete.\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * OLLA + SIMO_MRC(1x2 수신 다이버시티) — SISO OLLA를 첫 MIMO 모드로 확장
 *
 * run_pdsch_olla_simulation()과 구조는 동일(고정 SNR 시계열, Open-Loop vs
 * OLLA 2-pass, MCS가 트라이얼마다 바뀌므로 LDPC 코덱도 매 트라이얼
 * 재초기화)하되, AWGN 대신 매 트라이얼 독립적으로 뽑는 2-branch i.i.d.
 * Rayleigh 채널(genie-aided, DMRS 미모델링 — SISO OLLA와 동일 단순화)에
 * MRC 결합을 적용한다.
 *
 * **핵심 설계 결정(구현 정의)**: MCS 선택에 쓰는 `olla_select_mcs()`의
 * effective_snr_db는 SISO 버전과 완전히 동일한 공식(nominal_snr + offset)을
 * 그대로 쓴다 — MRC의 평균 다이버시티 이득(2-branch 기준 약 +3dB)을 개루프
 * 예측식에 반영하지 않는다. 의도적 선택: OLLA의 강점은 물리계층의 구체적
 * 이득 요인(다이버시티, 코드/블록길이별 SNR gap 등)을 몰라도 ACK/NACK
 * 피드백만으로 수렴한다는 데 있다 — 모드마다 다른 보정식을 손으로 넣는
 * 대신 동일한 단순 예측식을 여러 물리계층에 그대로 적용해, 폐루프가
 * 얼마나 다른 편향(여기서는 다이버시티 이득을 과소예측 → Open-Loop가
 * SISO 때와 반대로 목표보다 낮은 BLER을 낼 가능성)까지 스스로 흡수하는지
 * 보여주는 쪽을 택함.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_olla_simo_mrc_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 12 * num_rb;   /* DMRS 분리 없음 — 순수 링크적응 연구 */
    int crc_bits = 24;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    double snr   = cfg->snrStart;
    double N0    = 1.0 / pow(10.0, snr / 10.0);
    double sigma = sqrt(N0 / 2.0);

    printf("=== PDSCH OLLA + SIMO_MRC (1x2 수신 다이버시티), 고정 SNR 시계열 ===\n");
    printf("MCS Table    : %s\n", cfg->mcsTableType);
    printf("Fixed SNR    : %.1f dB (branch당, SNR sweep 무시, 시계열 시뮬레이션)\n", snr);
    printf("Rx Antennas  : 2 (MRC, genie-aided 채널, DMRS 미모델링)\n");
    printf("Num RB       : %d  (Data RE: %d)\n", num_rb, num_data);
    printf("BLER Target  : %.2f\n", cfg->ollaBlerTarget);
    printf("Step Down    : %.2f dB (Step Up = %.4f dB, 목표 BLER 수렴 조건)\n",
           cfg->ollaStepDownDb, cfg->ollaStepDownDb * cfg->ollaBlerTarget / (1.0 - cfg->ollaBlerTarget));
    printf("SNR Gap      : %.1f dB (Shannon 대비 구현 마진, MRC 이득은 예측식에 미반영)\n", cfg->ollaSnrGapDb);
    printf("Trials       : %d (per pass)\n\n", cfg->numTrials);

    int print_interval = cfg->numTrials / 20;
    if (print_interval < 1) print_interval = 1;

    for (int pass = 0; pass < 2; pass++) {
        int is_olla = (pass == 1);
        OLLAState olla;
        olla_init(&olla, cfg->ollaBlerTarget, cfg->ollaStepDownDb);

        printf("--- Pass %d: %s ---\n", pass + 1, is_olla ? "OLLA (폐루프 오프셋 적응)" : "Open-Loop (오프셋 고정 0)");
        printf("%-8s  %-6s  %-9s  %-11s  %-10s\n", "Trial", "MCS", "Offset(dB)", "WindowBLER", "CumBLER");
        for (int i = 0; i < 52; i++) printf("-");
        printf("\n");

        long total_bits = 0, total_err = 0;
        int  blk_err_cum = 0, blk_err_win = 0;
        long mcs_sum = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {
            double eff_snr = snr + (is_olla ? olla.offset_db : 0.0);
            int mcs_idx = olla_select_mcs(eff_snr, cfg->ollaSnrGapDb, tbl);
            MCSEntry mcs = get_mcs_entry(mcs_idx, tbl);
            double cr  = get_code_rate(&mcs);
            int    bps = mcs.modulationOrder;
            mcs_sum += mcs_idx;

            int max_dbits = num_data * bps;
            int tbsz = (int)(max_dbits * cr);
            if (tbsz < 1)    tbsz = 1;
            int B = tbsz + crc_bits;

            NRSegInfo seg; nr_seg_compute(B, cr, &seg);
            int payload = seg.Kprime - seg.L;
            LDPCCodec ldpc;
            ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                                seg.base_rows, seg.base_info_cols, seg.base_cols,
                                seg.filler_size);
            int acsz = ldpc.coded_size;
            int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                             length G for the whole TB */
            int *Er = (int *)malloc(seg.C * sizeof(int));
            nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

            int   *tb      = (int *)malloc(tbsz  * sizeof(int));
            int   *tb_crc  = (int *)malloc(B     * sizeof(int));
            int   *cb_bits = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
            int   *coded   = (int *)malloc(acsz  * sizeof(int));
            int  **rm      = (int **)malloc(seg.C * sizeof(int *));
            for (int r = 0; r < seg.C; r++) rm[r] = (int *)malloc(Er[r] * sizeof(int));
            int   *selbits = (int *)malloc(E     * sizeof(int));
            cx_t  *sym     = (cx_t *)malloc(num_data * sizeof(cx_t));
            cx_t  *eq      = (cx_t *)malloc(num_data * sizeof(cx_t));
            double *allllr = (double *)malloc(E     * sizeof(double));
            double *soft_buf = (double *)malloc(acsz * sizeof(double));
            int   *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));
            int   *decoded_tb = (int *)malloc(B * sizeof(int));

            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            nr_seg_split(tb_crc, &seg, cb_bits);
            for (int r = 0; r < seg.C; r++) {
                const int *info = cb_bits + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, info, coded);
                nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rm[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
            qam_modulate(selbits, E, mcs.modulation, sym);

            /* 트라이얼당 1회 2-branch i.i.d. Rayleigh 채널 드로우(genie-aided,
             * 블록-플랫), 심볼마다 독립 노이즈 */
            cx_t h[2];
            mimo_channel_draw_1x2(h);
            double nv_sum = 0.0;
            for (int d = 0; d < num_data; d++) {
                cx_t y[2] = {
                    h[0] * sym[d] + CX_MAKE(randn() * sigma, randn() * sigma),
                    h[1] * sym[d] + CX_MAKE(randn() * sigma, randn() * sigma)
                };
                double nv;
                mrc_combine(h, y, N0, &eq[d], &nv);
                nv_sum += nv;
            }
            double env = nv_sum / num_data;

            qam_demap_llr(eq, num_data, mcs.modulation, env, allllr);

            int roff = 0;
            int cb_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_buf, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllr + roff);
                roff += Er[r];
                ldpc_decode(&ldpc, soft_buf, 25, decoded_cb);
                if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B))
                    cb_crc_ok = 0;
                memcpy(decoded_tb + (size_t)r * payload, decoded_cb, payload * sizeof(int));
            }

            int crc_ok = cb_crc_ok && check_crc(decoded_tb, B, CRC24A);
            int be = 0;
            for (int i = 0; i < tbsz; i++) if (tb[i] != decoded_tb[i]) be++;
            int ack = (crc_ok && be == 0);

            total_bits += tbsz;
            total_err  += be;
            if (!ack) { blk_err_cum++; blk_err_win++; }

            if (is_olla) olla_update(&olla, ack);

            if ((trial + 1) % print_interval == 0 || trial == cfg->numTrials - 1) {
                double win_bler = (double)blk_err_win / print_interval;
                double cum_bler = (double)blk_err_cum / (trial + 1);
                printf("%-8d  %-6d  %-9.2f  %-11.4f  %-10.4f\n",
                       trial + 1, mcs_idx, is_olla ? olla.offset_db : 0.0, win_bler, cum_bler);
                blk_err_win = 0;
            }

            ldpc_free(&ldpc);
            free(tb); free(tb_crc); free(cb_bits); free(coded);
            for (int r = 0; r < seg.C; r++) free(rm[r]);
            free(rm); free(Er); free(selbits);
            free(sym); free(eq); free(allllr); free(soft_buf);
            free(decoded_cb); free(decoded_tb);
        }   /* end trial loop */

        double final_ber  = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double final_bler = (double)blk_err_cum / cfg->numTrials;
        double avg_mcs    = (double)mcs_sum / cfg->numTrials;
        printf("\n%s 최종: BER=%.4e  BLER=%.4f (목표 %.2f)  평균 MCS=%.2f\n\n",
               is_olla ? "OLLA" : "Open-Loop", final_ber, final_bler, cfg->ollaBlerTarget, avg_mcs);
    }   /* end pass loop */

    printf("PDSCH OLLA + SIMO_MRC simulation complete.\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * OLLA + SM_2X2(2계층 공간다중화) — SISO/SIMO_MRC에 이은 세 번째 OLLA 모드
 *
 * 구조는 위 SIMO_MRC 버전과 동일하되, 매 트라이얼 2x2 i.i.d. Rayleigh
 * 채널(genie-aided)을 드로우해 ZF/MMSE(EQUALIZER 설정)로 검출하고, 두
 * 레이어가 서로 다른 독립 코드워드를 "같은 OLLA가 고른 MCS"로 공유
 * 전송한다(이 프로젝트의 다른 SM_2X2 함수들과 동일한 관례). ACK는 두
 * 레이어 CRC가 모두 통과해야 하고(둘 중 하나라도 실패하면 NACK), OLLA
 * 폐루프 피드백은 이 결합 ACK/NACK 하나만 본다 — TB 단위 재전송 결정과
 * 동일한 관점(레이어별 독립 HARQ 프로세스가 아니라 "이 전송이 성공했나"만
 * 봄). MCS 예측식은 SIMO_MRC 버전과 마찬가지로 SISO와 동일한 단순식을
 * 그대로 사용(위 SIMO_MRC 헤더 주석의 설계 논거 참조) — 공간다중화의
 * 검출 손실(ZF/MMSE array/diversity 손실)까지 OLLA 폐루프가 스스로
 * 흡수하는지 보는 것이 이번 확장의 목적.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_olla_sm2x2_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 12 * num_rb;
    int crc_bits = 24;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    double snr   = cfg->snrStart;
    double N0    = 1.0 / pow(10.0, snr / 10.0);
    double sigma = sqrt(N0 / 2.0);
    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);

    printf("=== PDSCH OLLA + SM_2X2 (2계층 공간다중화), 고정 SNR 시계열 ===\n");
    printf("MCS Table    : %s\n", cfg->mcsTableType);
    printf("Fixed SNR    : %.1f dB (SNR sweep 무시, 시계열 시뮬레이션)\n", snr);
    printf("Layers       : 2 (독립 코드워드, OLLA가 고른 동일 MCS 공유)\n");
    printf("Rx Antennas  : 2 (genie-aided 채널, DMRS 미모델링)\n");
    printf("Detector     : %s\n", use_mmse ? "MMSE" : "ZF");
    printf("Num RB       : %d  (Data RE/layer: %d)\n", num_rb, num_data);
    printf("BLER Target  : %.2f (결합 ACK — 두 레이어 모두 성공해야 ACK)\n", cfg->ollaBlerTarget);
    printf("Step Down    : %.2f dB (Step Up = %.4f dB, 목표 BLER 수렴 조건)\n",
           cfg->ollaStepDownDb, cfg->ollaStepDownDb * cfg->ollaBlerTarget / (1.0 - cfg->ollaBlerTarget));
    printf("SNR Gap      : %.1f dB (Shannon 대비 구현 마진, MIMO 검출손실은 예측식에 미반영)\n", cfg->ollaSnrGapDb);
    printf("Trials       : %d (per pass)\n\n", cfg->numTrials);

    int print_interval = cfg->numTrials / 20;
    if (print_interval < 1) print_interval = 1;

    for (int pass = 0; pass < 2; pass++) {
        int is_olla = (pass == 1);
        OLLAState olla;
        olla_init(&olla, cfg->ollaBlerTarget, cfg->ollaStepDownDb);

        printf("--- Pass %d: %s ---\n", pass + 1, is_olla ? "OLLA (폐루프 오프셋 적응)" : "Open-Loop (오프셋 고정 0)");
        printf("%-8s  %-6s  %-9s  %-11s  %-10s\n", "Trial", "MCS", "Offset(dB)", "WindowBLER", "CumBLER");
        for (int i = 0; i < 52; i++) printf("-");
        printf("\n");

        long total_bits = 0, total_err = 0;
        int  blk_err_cum = 0, blk_err_win = 0;
        long mcs_sum = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {
            double eff_snr = snr + (is_olla ? olla.offset_db : 0.0);
            int mcs_idx = olla_select_mcs(eff_snr, cfg->ollaSnrGapDb, tbl);
            MCSEntry mcs = get_mcs_entry(mcs_idx, tbl);
            double cr  = get_code_rate(&mcs);
            int    bps = mcs.modulationOrder;
            mcs_sum += mcs_idx;

            int max_dbits = num_data * bps;
            int tbsz = (int)(max_dbits * cr);
            if (tbsz < 1)    tbsz = 1;
            int B = tbsz + crc_bits;

            NRSegInfo seg; nr_seg_compute(B, cr, &seg);
            int payload = seg.Kprime - seg.L;
            LDPCCodec ldpc;
            ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                                seg.base_rows, seg.base_info_cols, seg.base_cols,
                                seg.filler_size);
            int acsz = ldpc.coded_size;
            int E    = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                             length G per layer */
            int *Er = (int *)malloc(seg.C * sizeof(int));
            nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

            int *tbA=(int*)malloc(tbsz*sizeof(int)), *tbB=(int*)malloc(tbsz*sizeof(int));
            int *tb_crcA=(int*)malloc(B*sizeof(int)), *tb_crcB=(int*)malloc(B*sizeof(int));
            int *cb_bitsA=(int*)malloc((size_t)seg.C*seg.Kprime*sizeof(int));
            int *cb_bitsB=(int*)malloc((size_t)seg.C*seg.Kprime*sizeof(int));
            int *codedA=(int*)malloc(acsz*sizeof(int)), *codedB=(int*)malloc(acsz*sizeof(int));
            int **rmA=(int**)malloc(seg.C*sizeof(int*)), **rmB=(int**)malloc(seg.C*sizeof(int*));
            for (int r = 0; r < seg.C; r++) { rmA[r]=(int*)malloc(Er[r]*sizeof(int)); rmB[r]=(int*)malloc(Er[r]*sizeof(int)); }
            int *selbitsA=(int*)malloc(E*sizeof(int)), *selbitsB=(int*)malloc(E*sizeof(int));
            cx_t *symsA=(cx_t*)malloc(num_data*sizeof(cx_t)), *symsB=(cx_t*)malloc(num_data*sizeof(cx_t));
            cx_t *x_hatA=(cx_t*)malloc(num_data*sizeof(cx_t)), *x_hatB=(cx_t*)malloc(num_data*sizeof(cx_t));
            double *allllrA=(double*)malloc(E*sizeof(double)), *allllrB=(double*)malloc(E*sizeof(double));
            double *soft_bufA=(double*)malloc(acsz*sizeof(double)), *soft_bufB=(double*)malloc(acsz*sizeof(double));
            int *decodedA_cb=(int*)malloc(ldpc.info_size*sizeof(int)), *decodedB_cb=(int*)malloc(ldpc.info_size*sizeof(int));
            int *decodedA_tb=(int*)malloc(B*sizeof(int)), *decodedB_tb=(int*)malloc(B*sizeof(int));

            gen_random_bits(tbA, tbsz); gen_random_bits(tbB, tbsz);
            attach_crc(tbA, tbsz, CRC24A, tb_crcA);
            attach_crc(tbB, tbsz, CRC24A, tb_crcB);
            nr_seg_split(tb_crcA, &seg, cb_bitsA);
            nr_seg_split(tb_crcB, &seg, cb_bitsB);
            for (int r = 0; r < seg.C; r++) {
                const int *infoA = cb_bitsA + (size_t)r * seg.Kprime;
                const int *infoB = cb_bitsB + (size_t)r * seg.Kprime;
                ldpc_encode(&ldpc, infoA, codedA);
                ldpc_encode(&ldpc, infoB, codedB);
                nr_ldpc_rate_match_select(codedA, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rmA[r]);
                nr_ldpc_rate_match_select(codedB, acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, Er[r], rmB[r]);
            }
            nr_seg_concat(&seg, (const int *const *)rmA, Er, selbitsA);
            nr_seg_concat(&seg, (const int *const *)rmB, Er, selbitsB);
            qam_modulate(selbitsA, E, mcs.modulation, symsA);
            qam_modulate(selbitsB, E, mcs.modulation, symsB);

            /* 트라이얼당 1회 2x2 i.i.d. Rayleigh 채널 드로우(genie-aided,
             * 블록-플랫), 심볼마다 독립 노이즈 */
            cx_t h[2][2];
            mimo_channel_draw_2x2(h);
            double nvA_sum=0.0, nvB_sum=0.0;
            for (int d = 0; d < num_data; d++) {
                cx_t y[2] = {
                    h[0][0]*symsA[d] + h[0][1]*symsB[d] + CX_MAKE(randn()*sigma, randn()*sigma),
                    h[1][0]*symsA[d] + h[1][1]*symsB[d] + CX_MAKE(randn()*sigma, randn()*sigma)
                };
                cx_t xh[2]; double nv[2];
                if (use_mmse) mimo_mmse_detect(h, y, N0, xh, nv);
                else          mimo_zf_detect  (h, y, N0, xh, nv);
                x_hatA[d] = xh[0]; x_hatB[d] = xh[1];
                nvA_sum += nv[0]; nvB_sum += nv[1];
            }
            double envA = nvA_sum / num_data, envB = nvB_sum / num_data;

            qam_demap_llr(x_hatA, num_data, mcs.modulation, envA, allllrA);
            qam_demap_llr(x_hatB, num_data, mcs.modulation, envB, allllrB);

            int roffA = 0, roffB = 0;
            int cbA_crc_ok = 1, cbB_crc_ok = 1;
            for (int r = 0; r < seg.C; r++) {
                memset(soft_bufA, 0, acsz*sizeof(double));
                memset(soft_bufB, 0, acsz*sizeof(double));
                nr_ldpc_rate_match_combine(soft_bufA, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllrA + roffA);
                nr_ldpc_rate_match_combine(soft_bufB, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, Er[r], allllrB + roffB);
                roffA += Er[r]; roffB += Er[r];

                ldpc_decode(&ldpc, soft_bufA, 25, decodedA_cb);
                ldpc_decode(&ldpc, soft_bufB, 25, decodedB_cb);
                if (seg.C > 1 && !check_crc(decodedA_cb, seg.Kprime, CRC24B)) cbA_crc_ok = 0;
                if (seg.C > 1 && !check_crc(decodedB_cb, seg.Kprime, CRC24B)) cbB_crc_ok = 0;
                memcpy(decodedA_tb + (size_t)r * payload, decodedA_cb, payload * sizeof(int));
                memcpy(decodedB_tb + (size_t)r * payload, decodedB_cb, payload * sizeof(int));
            }
            int crcA_ok = cbA_crc_ok && check_crc(decodedA_tb, B, CRC24A);
            int crcB_ok = cbB_crc_ok && check_crc(decodedB_tb, B, CRC24A);
            int beA=0, beB=0;
            for (int i=0;i<tbsz;i++) {
                if (tbA[i]!=decodedA_tb[i]) beA++;
                if (tbB[i]!=decodedB_tb[i]) beB++;
            }
            int ack = crcA_ok && (beA==0) && crcB_ok && (beB==0);

            total_bits += 2*tbsz;
            total_err  += beA + beB;
            if (!ack) { blk_err_cum++; blk_err_win++; }

            if (is_olla) olla_update(&olla, ack);

            if ((trial + 1) % print_interval == 0 || trial == cfg->numTrials - 1) {
                double win_bler = (double)blk_err_win / print_interval;
                double cum_bler = (double)blk_err_cum / (trial + 1);
                printf("%-8d  %-6d  %-9.2f  %-11.4f  %-10.4f\n",
                       trial + 1, mcs_idx, is_olla ? olla.offset_db : 0.0, win_bler, cum_bler);
                blk_err_win = 0;
            }

            ldpc_free(&ldpc);
            free(tbA); free(tbB); free(tb_crcA); free(tb_crcB);
            free(cb_bitsA); free(cb_bitsB); free(codedA); free(codedB);
            for (int r = 0; r < seg.C; r++) { free(rmA[r]); free(rmB[r]); }
            free(rmA); free(rmB); free(Er); free(selbitsA); free(selbitsB);
            free(symsA); free(symsB); free(x_hatA); free(x_hatB);
            free(allllrA); free(allllrB); free(soft_bufA); free(soft_bufB);
            free(decodedA_cb); free(decodedB_cb); free(decodedA_tb); free(decodedB_tb);
        }   /* end trial loop */

        double final_ber  = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double final_bler = (double)blk_err_cum / cfg->numTrials;
        double avg_mcs    = (double)mcs_sum / cfg->numTrials;
        printf("\n%s 최종: BER=%.4e  BLER=%.4f (목표 %.2f)  평균 MCS=%.2f\n\n",
               is_olla ? "OLLA" : "Open-Loop", final_ber, final_bler, cfg->ollaBlerTarget, avg_mcs);
    }   /* end pass loop */

    printf("PDSCH OLLA + SM_2X2 simulation complete.\n");
}

/* OLLA + SM_4X4 (4계층 공간다중화) — `run_pdsch_olla_sm2x2_simulation()`의
 * "레이어 수 무관 동일 MCS 공유 + 결합 ACK(모든 레이어 성공해야 ACK)"
 * 패턴을 그대로 4레이어로 확장, 4x4 채널/검출은
 * `run_pdsch_sm4x4_simulation()`의 배열 기반(NL=4) 구조를 그대로 사용.
 * MCS 예측식은 SM_2X2와 동일하게 OLLA offset+SNR gap만으로 결정
 * (MIMO 검출 손실은 예측식에 미반영 — 기존 OLLA 함수들과 같은 의도적
 * 단순화). SM_2X2와 마찬가지로 트라이얼마다 MCS가 바뀔 수 있어 LDPC를
 * 트라이얼 안에서 매번 재초기화. */
void run_pdsch_olla_sm4x4_simulation(const L1Config *cfg) {
    const int NL = 4;

    int num_rb   = cfg->numRB;
    int num_data = 12 * num_rb;
    int crc_bits = 24;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    double snr   = cfg->snrStart;
    double N0    = 1.0 / pow(10.0, snr / 10.0);
    double sigma = sqrt(N0 / 2.0);
    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);

    printf("=== PDSCH OLLA + SM_4X4 (4계층 공간다중화), 고정 SNR 시계열 ===\n");
    printf("MCS Table    : %s\n", cfg->mcsTableType);
    printf("Fixed SNR    : %.1f dB (SNR sweep 무시, 시계열 시뮬레이션)\n", snr);
    printf("Layers       : 4 (독립 코드워드, OLLA가 고른 동일 MCS 공유)\n");
    printf("Rx Antennas  : 4 (genie-aided 채널, DMRS 미모델링)\n");
    printf("Detector     : %s\n", use_mmse ? "MMSE" : "ZF");
    printf("Num RB       : %d  (Data RE/layer: %d)\n", num_rb, num_data);
    printf("BLER Target  : %.2f (결합 ACK — 4개 레이어 모두 성공해야 ACK)\n", cfg->ollaBlerTarget);
    printf("Step Down    : %.2f dB (Step Up = %.4f dB, 목표 BLER 수렴 조건)\n",
           cfg->ollaStepDownDb, cfg->ollaStepDownDb * cfg->ollaBlerTarget / (1.0 - cfg->ollaBlerTarget));
    printf("SNR Gap      : %.1f dB (Shannon 대비 구현 마진, MIMO 검출손실은 예측식에 미반영)\n", cfg->ollaSnrGapDb);
    printf("Trials       : %d (per pass)\n\n", cfg->numTrials);

    int print_interval = cfg->numTrials / 20;
    if (print_interval < 1) print_interval = 1;

    for (int pass = 0; pass < 2; pass++) {
        int is_olla = (pass == 1);
        OLLAState olla;
        olla_init(&olla, cfg->ollaBlerTarget, cfg->ollaStepDownDb);

        printf("--- Pass %d: %s ---\n", pass + 1, is_olla ? "OLLA (폐루프 오프셋 적응)" : "Open-Loop (오프셋 고정 0)");
        printf("%-8s  %-6s  %-9s  %-11s  %-10s\n", "Trial", "MCS", "Offset(dB)", "WindowBLER", "CumBLER");
        for (int i = 0; i < 52; i++) printf("-");
        printf("\n");

        long total_bits = 0, total_err = 0;
        int  blk_err_cum = 0, blk_err_win = 0;
        long mcs_sum = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {
            double eff_snr = snr + (is_olla ? olla.offset_db : 0.0);
            int mcs_idx = olla_select_mcs(eff_snr, cfg->ollaSnrGapDb, tbl);
            MCSEntry mcs = get_mcs_entry(mcs_idx, tbl);
            double cr  = get_code_rate(&mcs);
            int    bps = mcs.modulationOrder;
            mcs_sum += mcs_idx;

            int max_dbits = num_data * bps;
            int tbsz = (int)(max_dbits * cr);
            if (tbsz < 1) tbsz = 1;
            int B = tbsz + crc_bits;

            NRSegInfo seg; nr_seg_compute(B, cr, &seg);
            int payload = seg.Kprime - seg.L;
            LDPCCodec ldpc;
            ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                                seg.base_rows, seg.base_info_cols, seg.base_cols,
                                seg.filler_size);
            int acsz = ldpc.coded_size;
            int E    = num_data * bps;
            int *Er = (int *)malloc(seg.C * sizeof(int));
            nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

            int   *tb[NL], *tb_crc[NL], *cb_bits[NL], *coded[NL], **rm[NL], *selbits[NL];
            int   *decoded_cb[NL], *decoded_tb[NL];
            cx_t  *syms[NL];
            double *allllr[NL], *soft_buf[NL];
            double nv_sum[NL];
            cx_t  x_hat_re[NL];
            double nv_re[NL];

            for (int l = 0; l < NL; l++) {
                tb[l]         = (int   *)malloc(tbsz * sizeof(int));
                tb_crc[l]     = (int   *)malloc(B    * sizeof(int));
                cb_bits[l]    = (int   *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
                coded[l]      = (int   *)malloc(acsz * sizeof(int));
                rm[l]         = (int  **)malloc(seg.C * sizeof(int *));
                for (int r = 0; r < seg.C; r++) rm[l][r] = (int *)malloc(Er[r] * sizeof(int));
                selbits[l]    = (int   *)malloc(E    * sizeof(int));
                syms[l]       = (cx_t *)malloc(num_data * sizeof(cx_t));
                allllr[l]     = (double *)malloc(E     * sizeof(double));
                soft_buf[l]   = (double *)malloc(acsz  * sizeof(double));
                decoded_cb[l] = (int   *)malloc(ldpc.info_size * sizeof(int));
                decoded_tb[l] = (int   *)malloc(B * sizeof(int));
            }

            for (int l = 0; l < NL; l++) {
                gen_random_bits(tb[l], tbsz);
                attach_crc(tb[l], tbsz, CRC24A, tb_crc[l]);
                nr_seg_split(tb_crc[l], &seg, cb_bits[l]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[l] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[l]);
                    nr_ldpc_rate_match_select(coded[l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[l][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[l], Er, selbits[l]);
                qam_modulate(selbits[l], E, mcs.modulation, syms[l]);
            }

            /* 트라이얼당 1회 4x4 i.i.d. Rayleigh 채널 드로우(genie-aided,
             * 블록-플랫), 심볼마다 독립 노이즈 -- SM_2X2 OLLA와 동일 관례 */
            cx_t h[NL][NL];
            mimo_channel_draw_4x4(h);
            for (int l = 0; l < NL; l++) nv_sum[l] = 0.0;
            for (int d = 0; d < num_data; d++) {
                cx_t y[NL];
                for (int rIdx = 0; rIdx < NL; rIdx++) {
                    cx_t s = 0;
                    for (int t = 0; t < NL; t++) s += h[rIdx][t] * syms[t][d];
                    y[rIdx] = s + CX_MAKE(randn()*sigma, randn()*sigma);
                }
                if (use_mmse) mimo_mmse_detect_4x4(h, y, N0, x_hat_re, nv_re);
                else          mimo_zf_detect_4x4  (h, y, N0, x_hat_re, nv_re);
                for (int l = 0; l < NL; l++) {
                    /* syms[l][d]를 추정 심볼로 덮어써 재사용(sm4x4_simulation과 동일) */
                    syms[l][d] = x_hat_re[l];
                    nv_sum[l] += nv_re[l];
                }
            }
            for (int l = 0; l < NL; l++) {
                double env = nv_sum[l] / num_data;
                qam_demap_llr(syms[l], num_data, mcs.modulation, env, allllr[l]);
            }

            int trial_err = 0;
            int beL[NL];
            for (int l = 0; l < NL; l++) {
                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[l], 0, acsz*sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[l], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[l] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[l], 25, decoded_cb[l]);
                    if (seg.C > 1 && !check_crc(decoded_cb[l], seg.Kprime, CRC24B)) cb_crc_ok = 0;
                    memcpy(decoded_tb[l] + (size_t)r * payload, decoded_cb[l], payload * sizeof(int));
                }
                int crc_ok = cb_crc_ok && check_crc(decoded_tb[l], B, CRC24A);
                beL[l] = 0;
                for (int i = 0; i < tbsz; i++) if (tb[l][i] != decoded_tb[l][i]) beL[l]++;
                if (!crc_ok || beL[l] > 0) trial_err++;
            }
            int ack = (trial_err == 0);

            for (int l = 0; l < NL; l++) { total_bits += tbsz; total_err += beL[l]; }
            if (!ack) { blk_err_cum++; blk_err_win++; }

            if (is_olla) olla_update(&olla, ack);

            if ((trial + 1) % print_interval == 0 || trial == cfg->numTrials - 1) {
                double win_bler = (double)blk_err_win / print_interval;
                double cum_bler = (double)blk_err_cum / (trial + 1);
                printf("%-8d  %-6d  %-9.2f  %-11.4f  %-10.4f\n",
                       trial + 1, mcs_idx, is_olla ? olla.offset_db : 0.0, win_bler, cum_bler);
                blk_err_win = 0;
            }

            ldpc_free(&ldpc);
            for (int l = 0; l < NL; l++) {
                free(tb[l]); free(tb_crc[l]); free(cb_bits[l]); free(coded[l]);
                for (int r = 0; r < seg.C; r++) free(rm[l][r]);
                free(rm[l]); free(selbits[l]);
                free(syms[l]); free(allllr[l]); free(soft_buf[l]);
                free(decoded_cb[l]); free(decoded_tb[l]);
            }
            free(Er);
        }   /* end trial loop */

        double final_ber  = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double final_bler = (double)blk_err_cum / cfg->numTrials;
        double avg_mcs    = (double)mcs_sum / cfg->numTrials;
        printf("\n%s 최종: BER=%.4e  BLER=%.4f (목표 %.2f)  평균 MCS=%.2f\n\n",
               is_olla ? "OLLA" : "Open-Loop", final_ber, final_bler, cfg->ollaBlerTarget, avg_mcs);
    }   /* end pass loop */

    printf("PDSCH OLLA + SM_4X4 simulation complete.\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * MU-MIMO 하향링크 — Zero-Forcing Beamforming, 평탄 페이딩 (i.i.d.)
 *
 * mumimo.h 문서 참조. Nt=4(gNB), K=2 사용자(각 1 Rx 안테나, MU-MISO).
 * ZF-BF 프리코더가 사용자 간 간섭을 설계상 정확히 제거하므로(잡음 없는
 * 경우 H[k]·W[:,j]=0, j≠k — mumimo_zf_precode() 검증에서 |오차|~1e-15
 * 확인), 검출은 사용자별로 완전히 독립적인 스칼라 채널 y_k=h_eff_k·x_k+n_k
 * (h_eff_k=H[k]·W[:,k], 실수 양수)로 단순화된다 — MIMO 검출기가 전혀
 * 필요 없음. 두 사용자는 서로 다른(독립적인) 데이터를 받는다는 점이
 * SU-MIMO 다중 레이어(같은 사용자의 서로 다른 스트림)와의 근본적 차이 —
 * BLER은 사용자별로 각각, 그리고 "둘 중 하나라도 실패" 결합 지표로 보고.
 * 두 사용자 모두 같은 고정 MCS(이 프로젝트의 다른 함수들과 동일한 관례).
 * TDL/HARQ/사용자별 다중 스트림은 미지원 — tasks/todo.md 후속 과제.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_mumimo_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by all MUMIMO_K users (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz  = ldpc.coded_size;
    int E     = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                       length G per user */
    int nd    = num_data;   /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by all MUMIMO_K users (same E) */

    printf("=== PDSCH MU-MIMO (Zero-Forcing Beamforming, K=%d users) ===\n", MUMIMO_K);
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Users     : %d gNB antennas / %d users (각 1 Rx 안테나, i.i.d.)\n", MUMIMO_NT, MUMIMO_K);
    printf("Precoding    : Zero-Forcing Beamforming (사용자 간 간섭 설계상 제거)\n");
    printf("TB Size/User : %d bits (두 사용자 동일 고정 MCS)\n", tbsz);
    printf("Data RE/User : %d  (Genie-aided CSI, pilot 오버헤드 없음)\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[MUMIMO_K], *tb_crc[MUMIMO_K], *cb_bits[MUMIMO_K], *coded[MUMIMO_K], **rm[MUMIMO_K], *selbits[MUMIMO_K];
    int   *decoded_cb[MUMIMO_K], *decoded_tb[MUMIMO_K];
    cx_t  *sym[MUMIMO_K];
    double *allllr[MUMIMO_K], *soft_buf[MUMIMO_K];
    cx_t  *rx_hat[MUMIMO_K];
    for (int u = 0; u < MUMIMO_K; u++) {
        tb[u]       = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[u]   = (int    *)malloc(B     * sizeof(int));
        cb_bits[u]  = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[u]    = (int    *)malloc(acsz  * sizeof(int));
        rm[u]       = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[u][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[u]  = (int    *)malloc(E     * sizeof(int));
        sym[u]      = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[u]   = (double *)malloc(E     * sizeof(double));
        soft_buf[u] = (double *)malloc(acsz  * sizeof(double));
        decoded_cb[u] = (int  *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[u] = (int  *)malloc(B * sizeof(int));
        rx_hat[u]   = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *nbuf = (cx_t *)malloc((size_t)nd * MUMIMO_K * sizeof(cx_t));

    printf("%-9s  %-12s %-11s  %-12s %-11s  %s\n",
           "SNR(dB)", "BER_U0", "BLER_U0", "BER_U1", "BLER_U1", "BLER_Comb");
    for (int i = 0; i < 78; i++) printf("-");
    printf("\n");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[MUMIMO_K] = {0}, b_err[MUMIMO_K] = {0};
        int  e_blk[MUMIMO_K] = {0};
        int  e_comb = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 채널 드로우 + ZF-BF 프리코더 설계 */
            cx_t H[MUMIMO_K][MUMIMO_NT];
            mumimo_channel_draw(H);
            cx_t W[MUMIMO_NT][MUMIMO_K];
            mumimo_zf_precode(H, W);

            /* ② 유효 채널(사용자별 실수 양수 스칼라, ZF 간섭제거로 자명) */
            double h_eff[MUMIMO_K];
            for (int u = 0; u < MUMIMO_K; u++) {
                cx_t s = CX_ZERO;
                for (int t = 0; t < MUMIMO_NT; t++) s += H[u][t] * W[t][u];
                h_eff[u] = creal(s);   /* 이론상 항상 실수(허수부는 부동소수점 잡음) */
            }

            /* ③ 사용자별(서로 다른 데이터) CW 인코딩 */
            for (int u = 0; u < MUMIMO_K; u++) {
                gen_random_bits(tb[u], tbsz);
                attach_crc(tb[u], tbsz, CRC24A, tb_crc[u]);
                nr_seg_split(tb_crc[u], &seg, cb_bits[u]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[u] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[u]);
                    nr_ldpc_rate_match_select(coded[u], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[u][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[u], Er, selbits[u]);
                qam_modulate(selbits[u], E, mcs.modulation, sym[u]);
            }

            /* ④ 노이즈 드로우 + 사용자별 독립 스칼라 채널 검출
             *    (ZF 간섭제거로 y_u = h_eff[u]·x_u + n_u, MIMO 검출기 불필요) */
            for (int d = 0; d < nd; d++)
                for (int u = 0; u < MUMIMO_K; u++)
                    nbuf[d * MUMIMO_K + u] = CX_MAKE(randn() * sigma, randn() * sigma);

            int cb_crc_ok[MUMIMO_K];
            for (int u = 0; u < MUMIMO_K; u++) {
                double he = h_eff[u];
                double nv = (fabs(he) > 1e-9) ? N0 / (he * he) : N0 * 1e6;
                for (int d = 0; d < nd; d++) {
                    cx_t y = he * sym[u][d] + nbuf[d * MUMIMO_K + u];
                    rx_hat[u][d] = (fabs(he) > 1e-9) ? y / he : CX_ZERO;
                }
                qam_demap_llr(rx_hat[u], nd, mcs.modulation, nv, allllr[u]);

                int roff = 0;
                cb_crc_ok[u] = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[u], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[u], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[u] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[u], 25, decoded_cb[u]);
                    if (seg.C > 1 && !check_crc(decoded_cb[u], seg.Kprime, CRC24B))
                        cb_crc_ok[u] = 0;
                    memcpy(decoded_tb[u] + (size_t)r * payload, decoded_cb[u], payload * sizeof(int));
                }
            }

            /* ⑤ 사용자별 결과 집계 */
            int any_err = 0;
            for (int u = 0; u < MUMIMO_K; u++) {
                int crc_ok = cb_crc_ok[u] && check_crc(decoded_tb[u], B, CRC24A);
                int be = 0;
                for (int i = 0; i < tbsz; i++) if (tb[u][i] != decoded_tb[u][i]) be++;
                b_err[u]  += be;
                t_bits[u] += tbsz;
                if (!crc_ok || be > 0) { e_blk[u]++; any_err = 1; }
            }
            if (any_err) e_comb++;
        }   /* end trial loop */

        double ber0  = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler0 = e_blk[0] / (double)cfg->numTrials;
        double ber1  = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler1 = e_blk[1] / (double)cfg->numTrials;
        double bler_comb = e_comb / (double)cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f  %.4f\n",
               snr, ber0, bler0, ber1, bler1, bler_comb);
    }   /* end SNR loop */

    printf("\nPDSCH MU-MIMO simulation complete.\n");

    ldpc_free(&ldpc);
    free(nbuf); free(Er);
    for (int u = 0; u < MUMIMO_K; u++) {
        free(tb[u]); free(tb_crc[u]); free(cb_bits[u]); free(coded[u]);
        for (int r = 0; r < seg.C; r++) free(rm[u][r]);
        free(rm[u]); free(selbits[u]);
        free(sym[u]); free(allllr[u]); free(soft_buf[u]);
        free(decoded_cb[u]); free(decoded_tb[u]);
        free(rx_hat[u]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * MU-MIMO 하향링크 — Zero-Forcing Beamforming, TDL 주파수선택적 페이딩
 *
 * run_pdsch_mumimo_simulation()(평탄)과 구조는 동일하되, 사용자-Tx 안테나
 * 쌍마다(K×Nt=8개) 독립 TDL tap-set을 드로우해 RE(=심볼 인덱스를 그대로
 * RE로 취급 — genie-aided라 실제 DMRS 그리드가 없음, `pbch.c`의 페이딩
 * 함수와 동일한 기존 단순화 재사용)마다 채널 H[K][Nt]가 달라지므로
 * ZF-BF 프리코더도 RE마다 다시 설계해야 한다. `mumimo_zf_precode()`는
 * K=2 폐형 2×2 역행렬이라 RE당 비용이 사실상 무시할 만큼 작아(다른
 * massive-MIMO 모드의 SVD/고유분해와 달리) PRG 단위 서브밴드 근사 없이
 * 매 RE 정확히 재계산한다.
 *
 * ZF-BF의 H·H^+=I는 채널 값과 무관하게 항상 성립하는 대수적 항등식이므로
 * (평탄 버전 헤더 주석 참조) h_eff[u]가 RE마다 달라도 실수 양수 스칼라
 * 라는 성질은 그대로 유지 — RE별 독립 스칼라 채널로 검출.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_mumimo_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by all MUMIMO_K users (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz  = ldpc.coded_size;
    int E     = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                       length G per user */
    int nd    = num_data;   /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by all MUMIMO_K users (same E) */

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PDSCH MU-MIMO (Zero-Forcing Beamforming, K=%d users), TDL 주파수선택적 페이딩 ===\n", MUMIMO_K);
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Users     : %d gNB antennas / %d users (각 1 Rx 안테나)\n", MUMIMO_NT, MUMIMO_K);
    printf("Precoding    : Zero-Forcing Beamforming, RE별 재설계 (K x Nt = %d개 독립 TDL tap-set, DS=%.0fns, %d taps)\n",
           MUMIMO_K * MUMIMO_NT, cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("TB Size/User : %d bits (두 사용자 동일 고정 MCS)\n", tbsz);
    printf("Data RE/User : %d  (Genie-aided CSI, pilot 오버헤드 없음)\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[MUMIMO_K], *tb_crc[MUMIMO_K], *cb_bits[MUMIMO_K], *coded[MUMIMO_K], **rm[MUMIMO_K], *selbits[MUMIMO_K];
    int   *decoded_cb[MUMIMO_K], *decoded_tb[MUMIMO_K];
    cx_t  *sym[MUMIMO_K];
    double *allllr[MUMIMO_K], *soft_buf[MUMIMO_K];
    cx_t  *rx_hat[MUMIMO_K];
    for (int u = 0; u < MUMIMO_K; u++) {
        tb[u]       = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[u]   = (int    *)malloc(B     * sizeof(int));
        cb_bits[u]  = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[u]    = (int    *)malloc(acsz  * sizeof(int));
        rm[u]       = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[u][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[u]  = (int    *)malloc(E     * sizeof(int));
        sym[u]      = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[u]   = (double *)malloc(E     * sizeof(double));
        soft_buf[u] = (double *)malloc(acsz  * sizeof(double));
        decoded_cb[u] = (int  *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[u] = (int  *)malloc(B * sizeof(int));
        rx_hat[u]   = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *taps[MUMIMO_K][MUMIMO_NT];
    for (int u = 0; u < MUMIMO_K; u++)
        for (int t = 0; t < MUMIMO_NT; t++)
            taps[u][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%-9s  %-12s %-11s  %-12s %-11s  %s\n",
           "SNR(dB)", "BER_U0", "BLER_U0", "BER_U1", "BLER_U1", "BLER_Comb");
    for (int i = 0; i < 78; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[MUMIMO_K] = {0}, b_err[MUMIMO_K] = {0};
        int  e_blk[MUMIMO_K] = {0};
        int  e_comb = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            for (int u = 0; u < MUMIMO_K; u++)
                for (int t = 0; t < MUMIMO_NT; t++)
                    tdl_draw(&tdl_ch, taps[u][t]);

            for (int u = 0; u < MUMIMO_K; u++) {
                gen_random_bits(tb[u], tbsz);
                attach_crc(tb[u], tbsz, CRC24A, tb_crc[u]);
                nr_seg_split(tb_crc[u], &seg, cb_bits[u]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[u] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[u]);
                    nr_ldpc_rate_match_select(coded[u], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[u][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[u], Er, selbits[u]);
                qam_modulate(selbits[u], E, mcs.modulation, sym[u]);
            }

            double nv_sum[MUMIMO_K] = {0.0};
            for (int d = 0; d < nd; d++) {
                cx_t H[MUMIMO_K][MUMIMO_NT];
                for (int u = 0; u < MUMIMO_K; u++)
                    for (int t = 0; t < MUMIMO_NT; t++)
                        H[u][t] = tdl_freq_response(&tdl_ch, taps[u][t], d);
                cx_t W[MUMIMO_NT][MUMIMO_K];
                mumimo_zf_precode(H, W);

                for (int u = 0; u < MUMIMO_K; u++) {
                    cx_t s = CX_ZERO;
                    for (int t = 0; t < MUMIMO_NT; t++) s += H[u][t] * W[t][u];
                    double he = creal(s);
                    double nv = (fabs(he) > 1e-9) ? N0 / (he * he) : N0 * 1e6;
                    nv_sum[u] += nv;
                    cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                    cx_t y = he * sym[u][d] + noise;
                    rx_hat[u][d] = (fabs(he) > 1e-9) ? y / he : CX_ZERO;
                }
            }

            int cb_crc_ok[MUMIMO_K];
            for (int u = 0; u < MUMIMO_K; u++) {
                double env = nv_sum[u] / nd;
                qam_demap_llr(rx_hat[u], nd, mcs.modulation, env, allllr[u]);

                int roff = 0;
                cb_crc_ok[u] = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[u], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[u], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[u] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[u], 25, decoded_cb[u]);
                    if (seg.C > 1 && !check_crc(decoded_cb[u], seg.Kprime, CRC24B))
                        cb_crc_ok[u] = 0;
                    memcpy(decoded_tb[u] + (size_t)r * payload, decoded_cb[u], payload * sizeof(int));
                }
            }

            int any_err = 0;
            for (int u = 0; u < MUMIMO_K; u++) {
                int crc_ok = cb_crc_ok[u] && check_crc(decoded_tb[u], B, CRC24A);
                int be = 0;
                for (int i = 0; i < tbsz; i++) if (tb[u][i] != decoded_tb[u][i]) be++;
                b_err[u]  += be;
                t_bits[u] += tbsz;
                if (!crc_ok || be > 0) { e_blk[u]++; any_err = 1; }
            }
            if (any_err) e_comb++;
        }   /* end trial loop */

        double ber0  = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler0 = e_blk[0] / (double)cfg->numTrials;
        double ber1  = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler1 = e_blk[1] / (double)cfg->numTrials;
        double bler_comb = e_comb / (double)cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f  %.4f\n",
               snr, ber0, bler0, ber1, bler1, bler_comb);
    }   /* end SNR loop */

    printf("\nPDSCH MU-MIMO + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(Er);
    for (int u = 0; u < MUMIMO_K; u++) {
        free(tb[u]); free(tb_crc[u]); free(cb_bits[u]); free(coded[u]);
        for (int r = 0; r < seg.C; r++) free(rm[u][r]);
        free(rm[u]); free(selbits[u]);
        free(sym[u]); free(allllr[u]); free(soft_buf[u]);
        free(decoded_cb[u]); free(decoded_tb[u]);
        free(rx_hat[u]);
        for (int t = 0; t < MUMIMO_NT; t++) free(taps[u][t]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * MU-MIMO 하향링크 — Zero-Forcing Beamforming, HARQ 순환버퍼 IR/Chase
 * (평탄 페이딩/TDL 모두 지원)
 *
 * 두 사용자 모두 mother LDPC 코드워드 + 영구 soft-combining 버퍼를
 * 독립적으로 가지며(이 프로젝트 다른 다중스트림 HARQ 함수들과 동일한
 * "같은 재전송 occasion을 공유"단순화 — 매 attempt마다 두 사용자 모두
 * 재전송, 둘 다 CRC 통과해야 종료), ZF-BF 프리코더는 매 attempt(채널
 * 재드로우 시점)마다, TDL이면 매 RE마다 다시 설계한다. `nr_rate_matching.c`의
 * TS 38.212 5.4.2.1 표준 BG/Zc 인지 circular buffer(`nr_ldpc_rate_match_select`/
 * `nr_ldpc_rate_match_combine`, 2026-09-02)를 그대로 재사용 — CL_32PORT HARQ와
 * 동일한 mother-code/RV 순환 패턴.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_mumimo_harq_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;

    int is_tdl = (strcmp(cfg->channelModel, "TDL") == 0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr   = get_code_rate(&mcs);
    int    bps  = mcs.modulationOrder;
    int    crc_bits = 24;

    int E    = num_data * bps;
    int tbsz = (int)(E * cr);
    if (tbsz < 1)    tbsz = 1;

    /* TS 38.212 5.2.2 multi-code-block segmentation (P0-2c HARQ follow-up,
     * 2026-09-03) -- see run_pdsch_harq_simulation() design note. All
     * MUMIMO_K users share identical tbsz/cr -> one NRSegInfo/LDPCCodec/
     * Er[] covers all of them. */
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;

    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PDSCH MU-MIMO (HARQ Circular Buffer %s, K=%d users), %s ===\n",
           is_chase ? "Chase" : "IR", MUMIMO_K,
           is_tdl ? "TDL Frequency-Selective Fading" : "Flat Fading");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d/CB, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)seg.Kprime / acsz, acsz, ldpc.bg, ldpc.Zc);
    printf("Code Blocks  : C=%d/user%s\n", seg.C, seg.C > 1 ? " (CRC24B/CB)" : "");
    printf("Num RB       : %d\n", num_rb);
    printf("Tx/Users     : %d gNB antennas / %d users (각 1 Rx 안테나)\n", MUMIMO_NT, MUMIMO_K);
    printf("Precoding    : Zero-Forcing Beamforming%s\n", is_tdl ? ", RE별 재설계" : "");
    printf("TB Size/User : %d bits (두 사용자 동일 고정 MCS)\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : TDL per user-antenna pair (%d tap-sets, DS=%.0fns, %d taps, genie-aided)\n",
               MUMIMO_K * MUMIMO_NT, cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    else
        printf("Channel      : Flat Fading %dx%d (block-flat, redrawn per HARQ attempt, genie-aided)\n",
               MUMIMO_K, MUMIMO_NT);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[MUMIMO_K], *tb_crc[MUMIMO_K], *cb_bits[MUMIMO_K], **coded_cw[MUMIMO_K], **rm[MUMIMO_K], *selbits[MUMIMO_K];
    int   *decoded_cb[MUMIMO_K], *decoded_tb[MUMIMO_K];
    cx_t  *sym[MUMIMO_K], *rx_hat[MUMIMO_K];
    double *allllr[MUMIMO_K], **soft_buf[MUMIMO_K];
    for (int u = 0; u < MUMIMO_K; u++) {
        tb[u]         = (int    *)malloc(tbsz     * sizeof(int));
        tb_crc[u]     = (int    *)malloc(B        * sizeof(int));
        cb_bits[u]    = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded_cw[u]   = (int   **)malloc(seg.C    * sizeof(int *));
        rm[u]         = (int   **)malloc(seg.C    * sizeof(int *));
        for (int r = 0; r < seg.C; r++) {
            coded_cw[u][r] = (int *)malloc(acsz  * sizeof(int));
            rm[u][r]       = (int *)malloc(Er[r] * sizeof(int));
        }
        selbits[u]    = (int    *)malloc(E        * sizeof(int));
        decoded_cb[u] = (int    *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[u] = (int    *)malloc(B        * sizeof(int));
        sym[u]        = (cx_t  *)malloc(num_data  * sizeof(cx_t));
        rx_hat[u]     = (cx_t  *)malloc(num_data  * sizeof(cx_t));
        allllr[u]     = (double *)malloc(E        * sizeof(double));
        soft_buf[u]   = (double **)malloc(seg.C   * sizeof(double *));
        for (int r = 0; r < seg.C; r++) soft_buf[u][r] = (double *)malloc(acsz * sizeof(double));
    }
    cx_t *taps[MUMIMO_K][MUMIMO_NT];
    for (int u = 0; u < MUMIMO_K; u++)
        for (int t = 0; t < MUMIMO_NT; t++)
            taps[u][t] = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));
    cx_t H_flat[MUMIMO_K][MUMIMO_NT];

    printf("%10s%14s%14s%14s%12s\n",
           "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i = 0; i < 62; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        int total_err=0, total_bits=0, blk_err_final=0, blk_err_1st=0;
        long long total_attempts = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            for (int u = 0; u < MUMIMO_K; u++) {
                gen_random_bits(tb[u], tbsz);
                attach_crc(tb[u], tbsz, CRC24A, tb_crc[u]);
                nr_seg_split(tb_crc[u], &seg, cb_bits[u]);
                for (int r = 0; r < seg.C; r++) {
                    ldpc_encode(&ldpc, cb_bits[u] + (size_t)r * seg.Kprime, coded_cw[u][r]);
                    for (int i = 0; i < acsz; i++) soft_buf[u][r][i] = 0.0;
                }
            }

            int crc_ok[MUMIMO_K]={0}, be[MUMIMO_K]={0}, be_1st[MUMIMO_K]={0}, crc_1st[MUMIMO_K]={0};
            int attempts = 0;

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int u = 0; u < MUMIMO_K; u++) {
                    for (int r = 0; r < seg.C; r++)
                        nr_ldpc_rate_match_select(coded_cw[u][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], rm[u][r]);
                    nr_seg_concat(&seg, (const int *const *)rm[u], Er, selbits[u]);
                    qam_modulate(selbits[u], E, mcs.modulation, sym[u]);
                }

                /* 채널: 매 attempt(시간 다이버시티) 재드로우 — CL_32PORT HARQ와
                 * 동일한 관례. Flat은 한 번만, TDL은 K*Nt개 tap-set 재드로우 */
                if (!is_tdl) mumimo_channel_draw(H_flat);
                else {
                    for (int u = 0; u < MUMIMO_K; u++)
                        for (int t = 0; t < MUMIMO_NT; t++)
                            tdl_draw(&tdl_ch, taps[u][t]);
                }

                double nv_sum[MUMIMO_K] = {0.0};
                for (int d = 0; d < num_data; d++) {
                    cx_t H[MUMIMO_K][MUMIMO_NT];
                    if (!is_tdl) {
                        for (int u = 0; u < MUMIMO_K; u++)
                            for (int t = 0; t < MUMIMO_NT; t++)
                                H[u][t] = H_flat[u][t];
                    } else {
                        for (int u = 0; u < MUMIMO_K; u++)
                            for (int t = 0; t < MUMIMO_NT; t++)
                                H[u][t] = tdl_freq_response(&tdl_ch, taps[u][t], d);
                    }
                    cx_t W[MUMIMO_NT][MUMIMO_K];
                    mumimo_zf_precode(H, W);

                    for (int u = 0; u < MUMIMO_K; u++) {
                        cx_t s = CX_ZERO;
                        for (int t = 0; t < MUMIMO_NT; t++) s += H[u][t] * W[t][u];
                        double he = creal(s);
                        double nv = (fabs(he) > 1e-9) ? N0 / (he * he) : N0 * 1e6;
                        nv_sum[u] += nv;
                        cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                        cx_t y = he * sym[u][d] + noise;
                        rx_hat[u][d] = (fabs(he) > 1e-9) ? y / he : CX_ZERO;
                    }
                }

                for (int u = 0; u < MUMIMO_K; u++) {
                    double env = nv_sum[u] / num_data;
                    qam_demap_llr(rx_hat[u], num_data, mcs.modulation, env, allllr[u]);
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        nr_ldpc_rate_match_combine(soft_buf[u][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], allllr[u] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[u][r], 25, decoded_cb[u]);
                        if (seg.C > 1 && !check_crc(decoded_cb[u], seg.Kprime, CRC24B)) cb_crc_ok = 0;
                        memcpy(decoded_tb[u] + (size_t)r * payload, decoded_cb[u], payload * sizeof(int));
                    }
                    crc_ok[u] = cb_crc_ok && check_crc(decoded_tb[u], B, CRC24A);
                    int biterr = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[u][i] != decoded_tb[u][i]) biterr++;
                    be[u] = biterr;
                    if (attempt == 0) { be_1st[u] = biterr; crc_1st[u] = crc_ok[u]; }
                }

                attempts = attempt + 1;
                int all_ok = 1;
                for (int u = 0; u < MUMIMO_K; u++) if (!crc_ok[u]) { all_ok = 0; break; }
                if (all_ok) break;
            }   /* end attempt loop */

            int any_err_1st = 0, any_err_final = 0;
            for (int u = 0; u < MUMIMO_K; u++) {
                total_err  += be[u];
                total_bits += tbsz;
                if (!crc_1st[u] || be_1st[u] > 0) any_err_1st   = 1;
                if (!crc_ok[u]  || be[u]    > 0) any_err_final = 1;
            }
            if (any_err_1st)   blk_err_1st++;
            if (any_err_final) blk_err_final++;
            total_attempts += attempts;
        }   /* end trial loop */

        double ber        = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double bler_1st   = (double)blk_err_1st   / cfg->numTrials;
        double bler_final = (double)blk_err_final / cfg->numTrials;
        double avg_tx     = (double)total_attempts / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr, ber, bler_1st, bler_final, avg_tx);
    }   /* end SNR loop */

    printf("\nPDSCH MU-MIMO + %s + HARQ simulation complete.\n", is_tdl ? "TDL" : "Flat Fading");

    ldpc_free(&ldpc);
    free(Er);
    for (int u = 0; u < MUMIMO_K; u++) {
        free(tb[u]); free(tb_crc[u]); free(cb_bits[u]);
        for (int r = 0; r < seg.C; r++) { free(coded_cw[u][r]); free(rm[u][r]); free(soft_buf[u][r]); }
        free(coded_cw[u]); free(rm[u]); free(soft_buf[u]);
        free(selbits[u]); free(decoded_cb[u]); free(decoded_tb[u]);
        free(sym[u]); free(rx_hat[u]);
        free(allllr[u]);
        for (int t = 0; t < MUMIMO_NT; t++) free(taps[u][t]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 빔 관리(Beam Management) — SSB/CSI-RS 기반 P1 절차, 32-port Tx 빔 스위핑
 *
 * beam_mgmt.h 문서 참조. 매 트라이얼마다 코드북 격자와 무관한 임의의
 * 연속 방향(l_true,m_true,n_true)에서 단일 지배경로(LOS) 참 채널을 뽑고,
 * (1) Genie(양자화 손실만) — 잡음 없이 1024개 후보 전수탐색으로 격자
 * 위에서의 진짜 최적 빔, (2) P1(양자화+측정잡음 손실) — 빔마다 num_rep회
 * 반복 관측한 RSRP 평균으로 선택한 빔, 두 경우를 같은 참 채널·같은 SNR로
 * 병렬 비교한다. 두 경우 모두 선택된 빔을 실제 Tx 프리코더로 써서
 * rank-1 SISO 등가 스칼라 채널(y=h_eff·x+n)로 PDSCH 데이터를 전송·복호
 * (두 사용자/CW가 아니라 같은 트라이얼 내 "같은 채널, 다른 빔 선택
 * 방식" 2-way 비교라는 점이 MU-MIMO와 다름). 단일 Rx 안테나, 평탄
 * 페이딩(트라이얼 내 채널 고정) 전용 — UE Rx 빔 스위핑, TDL, HARQ는
 * 미포함(구현 정의 스코프 축소, tasks/todo.md 후속 과제).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_beam_mgmt_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int num_rep  = cfg->beamMgmtNumRep;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by both scenarios (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz  = ldpc.coded_size;
    int E     = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                       length G per scenario */
    int nd    = num_data;   /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by both scenarios (same E) */

    printf("=== PDSCH Beam Management (SSB/CSI-RS P1, 32-port Tx 빔 스위핑) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Candidate Beams: %d (i1_1=16 x i1_2=16 x i2=4, rank-1 코드북 전체)\n",
           BM_CAND_L * BM_CAND_M * BM_CAND_N2);
    printf("P1 Num Rep   : %d (빔당 RSRP 반복 관측 횟수)\n", num_rep);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Data RE      : %d  (Genie는 잡음없는 전수탐색, P1은 반복관측 RSRP로 선택)\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int    *tb[2], *tb_crc[2], *cb_bits[2], *coded[2], **rm[2], *selbits[2];
    int    *decoded_cb[2], *decoded_tb[2];
    cx_t   *sym[2];
    double *allllr[2], *soft_buf[2];
    cx_t   *rx_hat[2];
    for (int s = 0; s < 2; s++) {
        tb[s]       = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[s]   = (int    *)malloc(B     * sizeof(int));
        cb_bits[s]  = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[s]    = (int    *)malloc(acsz  * sizeof(int));
        rm[s]       = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[s][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[s]  = (int    *)malloc(E     * sizeof(int));
        sym[s]      = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[s]   = (double *)malloc(E     * sizeof(double));
        soft_buf[s] = (double *)malloc(acsz  * sizeof(double));
        decoded_cb[s] = (int  *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[s] = (int  *)malloc(B * sizeof(int));
        rx_hat[s]   = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }

    printf("%-9s  %-11s %-12s %-11s  %-12s %-11s\n",
           "SNR(dB)", "P1==Genie", "BER_Genie", "BLER_Genie", "BER_P1", "BLER_P1");
    for (int i = 0; i < 78; i++) printf("-");
    printf("\n");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[2] = {0}, b_err[2] = {0};
        int  e_blk[2] = {0};
        int  match_cnt = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 임의의 연속 방향(격자와 무관)에서 참 채널 드로우 */
            double l_true = (double)rand() / RAND_MAX * BM_CAND_L;
            double m_true = (double)rand() / RAND_MAX * BM_CAND_M;
            double n_true = (double)rand() / RAND_MAX * BM_CAND_N2;
            cx_t H_true[32];
            beam_mgmt_true_channel(l_true, m_true, n_true, H_true);

            /* ② Genie(양자화 손실만) vs P1(양자화+측정잡음) 빔 선택 */
            int gl, gm, gn; double gg;
            beam_mgmt_genie_best(H_true, &gl, &gm, &gn, &gg);
            int sl, sm, sn; double sg;
            beam_mgmt_p1_sweep(H_true, N0, num_rep, &sl, &sm, &sn, &sg);
            if (sl == gl && sm == gm && sn == gn) match_cnt++;

            /* ③ 선택된 빔의 복소 유효 채널(스칼라, rank-1) 계산 */
            cx_t Wg[32], Wp[32];
            codebook_type1_sp_32port_rank1(gl, gm, gn, Wg);
            codebook_type1_sp_32port_rank1(sl, sm, sn, Wp);
            cx_t heff[2] = {CX_ZERO, CX_ZERO};
            for (int t = 0; t < 32; t++) {
                heff[0] += conj(H_true[t]) * Wg[t];
                heff[1] += conj(H_true[t]) * Wp[t];
            }

            /* ④ 두 경로(Genie/P1) 각각 독립 CW 인코딩 + 검출 + 복호 */
            for (int s = 0; s < 2; s++) {
                gen_random_bits(tb[s], tbsz);
                attach_crc(tb[s], tbsz, CRC24A, tb_crc[s]);
                nr_seg_split(tb_crc[s], &seg, cb_bits[s]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[s] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[s]);
                    nr_ldpc_rate_match_select(coded[s], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[s][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[s], Er, selbits[s]);
                qam_modulate(selbits[s], E, mcs.modulation, sym[s]);

                double he_abs2 = CX_NORM(heff[s]);
                double nv = (he_abs2 > 1e-12) ? N0 / he_abs2 : N0 * 1e6;
                for (int d = 0; d < nd; d++) {
                    cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                    cx_t y = heff[s] * sym[s][d] + noise;
                    rx_hat[s][d] = (he_abs2 > 1e-12) ? y / heff[s] : CX_ZERO;
                }
                qam_demap_llr(rx_hat[s], nd, mcs.modulation, nv, allllr[s]);

                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[s], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[s], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[s] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[s], 25, decoded_cb[s]);
                    if (seg.C > 1 && !check_crc(decoded_cb[s], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[s] + (size_t)r * payload, decoded_cb[s], payload * sizeof(int));
                }

                int crc_ok = cb_crc_ok && check_crc(decoded_tb[s], B, CRC24A);
                int be = 0;
                for (int i = 0; i < tbsz; i++) if (tb[s][i] != decoded_tb[s][i]) be++;
                b_err[s]  += be;
                t_bits[s] += tbsz;
                if (!crc_ok || be > 0) e_blk[s]++;
            }
        }   /* end trial loop */

        double match_rate  = match_cnt / (double)cfg->numTrials;
        double ber_genie   = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler_genie  = e_blk[0] / (double)cfg->numTrials;
        double ber_p1      = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler_p1     = e_blk[1] / (double)cfg->numTrials;

        printf("%-9.1f  %-11.4f %-12.4e %-11.4f  %-12.4e %-11.4f\n",
               snr, match_rate, ber_genie, bler_genie, ber_p1, bler_p1);
    }   /* end SNR loop */

    printf("\nPDSCH Beam Management simulation complete.\n");

    ldpc_free(&ldpc);
    free(Er);
    for (int s = 0; s < 2; s++) {
        free(tb[s]); free(tb_crc[s]); free(cb_bits[s]); free(coded[s]);
        for (int r = 0; r < seg.C; r++) free(rm[s][r]);
        free(rm[s]); free(selbits[s]);
        free(sym[s]); free(allllr[s]); free(soft_buf[s]);
        free(decoded_cb[s]); free(decoded_tb[s]);
        free(rx_hat[s]);
    }
}

/* 빔 관리 P1 -> P3(UE Rx 빔 정제) -> P2(gNB Tx 빔 재정제) — 2026-09-03.
 * UE를 더 이상 단일/광각 안테나로 취급하지 않고 실제 BM_UE_N(=4)소자
 * ULA + 자체 DFT 빔 코드북(BM_UE_CAND=8후보)을 갖는다고 모델링(위
 * beam_mgmt.h 문서 참조). 순서:
 *   P1(기존 beam_mgmt_p1_sweep, H_true 위에서 그대로) -> gNB Tx 빔 W1
 *   P3(신규): W1 고정, UE가 자기 Rx 빔 BM_UE_CAND개 스위핑 -> Rx 빔 w_ue
 *   P2(신규): w_ue 고정, 유효채널 h_eff로 beam_mgmt_p1_sweep 재사용
 *             -> 재정제된 gNB Tx 빔 W2
 * "정제 안 함" 기준선(No-Rx-Sweep)은 UE가 항상 후보 idx=0 고정 빔만
 * 쓴다고 가정(실측 없이 임의 고정 — 빔 스위핑 자체를 안 하는 상황을
 * 대표) — Genie(이상적 상한)/No-Rx-Sweep(정제 전)/P3+P2 Refined(정제 후)
 * 세 기준으로 비교. */
void run_pdsch_beam_mgmt_p123_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int num_rep  = cfg->beamMgmtNumRep;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz  = ldpc.coded_size;
    int E     = num_data * bps;
    int nd    = num_data;
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

    printf("=== PDSCH Beam Management P1->P3->P2 (UE Rx 빔 정제 + gNB Tx 빔 재정제) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("gNB Tx Cand  : %d (i1_1=16 x i1_2=16 x i2=4, rank-1 코드북 전체)\n",
           BM_CAND_L * BM_CAND_M * BM_CAND_N2);
    printf("UE Rx Cand   : %d (%d소자 ULA, 오버샘플링 x%d)\n", BM_UE_CAND, BM_UE_N, BM_UE_O);
    printf("Num Rep      : %d (빔당 RSRP 반복 관측 횟수, P1/P3 공용)\n", num_rep);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int    *tb[2], *tb_crc[2], *cb_bits[2], *coded[2], **rm[2], *selbits[2];
    int    *decoded_cb[2], *decoded_tb[2];
    cx_t   *sym[2];
    double *allllr[2], *soft_buf[2];
    cx_t   *rx_hat[2];
    for (int s = 0; s < 2; s++) {
        tb[s]       = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[s]   = (int    *)malloc(B     * sizeof(int));
        cb_bits[s]  = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[s]    = (int    *)malloc(acsz  * sizeof(int));
        rm[s]       = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[s][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[s]  = (int    *)malloc(E     * sizeof(int));
        sym[s]      = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[s]   = (double *)malloc(E     * sizeof(double));
        soft_buf[s] = (double *)malloc(acsz  * sizeof(double));
        decoded_cb[s] = (int  *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[s] = (int  *)malloc(B * sizeof(int));
        rx_hat[s]   = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }

    printf("%-9s  %-11s  %-14s  %-14s  %-12s %-11s  %-12s %-11s\n",
           "SNR(dB)", "P2==P1", "AvgGain_NoSweep", "AvgGain_P3P2", "BER_Genie", "BLER_Genie", "BER_Refined", "BLER_Refined");
    for (int i = 0; i < 100; i++) printf("-");
    printf("\n");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[2] = {0}, b_err[2] = {0};
        int  e_blk[2] = {0};
        int  p2_eq_p1_cnt = 0;
        double gain_noswp_sum = 0.0, gain_refined_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 참 방향 드로우: gNB 3개(l,m,n) + UE 1개(r) */
            double l_true = (double)rand() / RAND_MAX * BM_CAND_L;
            double m_true = (double)rand() / RAND_MAX * BM_CAND_M;
            double n_true = (double)rand() / RAND_MAX * BM_CAND_N2;
            double r_true = (double)rand() / RAND_MAX * BM_UE_CAND;
            cx_t H_true[32];
            beam_mgmt_true_channel(l_true, m_true, n_true, H_true);
            cx_t H_full[BM_UE_N][32];
            beam_mgmt_true_channel_mimo(H_true, r_true, H_full);

            /* ② Genie: 이상적 단일안테나 상한(기존과 동일, 정제 절차와 무관) */
            int gl, gm, gn; double gg;
            beam_mgmt_genie_best(H_true, &gl, &gm, &gn, &gg);
            cx_t Wg[32];
            codebook_type1_sp_32port_rank1(gl, gm, gn, Wg);
            cx_t heff_genie = CX_ZERO;
            for (int t = 0; t < 32; t++) heff_genie += conj(H_true[t]) * Wg[t];

            /* ③ P1 -> P3 -> P2 */
            int sl1, sm1, sn1; double sg1;
            beam_mgmt_p1_sweep(H_true, N0, num_rep, &sl1, &sm1, &sn1, &sg1);
            cx_t W1[32];
            codebook_type1_sp_32port_rank1(sl1, sm1, sn1, W1);

            int ue_idx; double p3_gain;
            beam_mgmt_p3_sweep(H_full, W1, N0, num_rep, &ue_idx, &p3_gain);
            cx_t w_ue[BM_UE_N];
            beam_mgmt_ue_steer((double)ue_idx, w_ue);

            cx_t h_eff2[32];
            beam_mgmt_p2_effective_channel(H_full, w_ue, h_eff2);
            int sl2, sm2, sn2; double sg2;
            beam_mgmt_p1_sweep(h_eff2, N0, num_rep, &sl2, &sm2, &sn2, &sg2);
            if (sl2 == sl1 && sm2 == sm1 && sn2 == sn1) p2_eq_p1_cnt++;

            cx_t W2[32];
            codebook_type1_sp_32port_rank1(sl2, sm2, sn2, W2);
            cx_t heff_refined = CX_ZERO;
            for (int u = 0; u < BM_UE_N; u++) {
                cx_t hg = CX_ZERO;
                for (int g = 0; g < 32; g++) hg += conj(H_full[u][g]) * W2[g];
                heff_refined += conj(w_ue[u]) * hg;
            }

            /* ④ "정제 안 함" 기준선: UE가 항상 후보 idx=0 고정 빔으로
             * P1이 고른 W1을 그대로 수신(실측 없이 임의 고정) */
            cx_t w_fixed[BM_UE_N];
            beam_mgmt_ue_steer(0.0, w_fixed);
            cx_t heff_noswp = CX_ZERO;
            for (int u = 0; u < BM_UE_N; u++) {
                cx_t hg = CX_ZERO;
                for (int g = 0; g < 32; g++) hg += conj(H_full[u][g]) * W1[g];
                heff_noswp += conj(w_fixed[u]) * hg;
            }
            gain_noswp_sum   += CX_NORM(heff_noswp);
            gain_refined_sum += CX_NORM(heff_refined);

            /* ⑤ 두 경로(Genie/Refined) 각각 독립 CW 인코딩+검출+복호 */
            cx_t heff[2] = { heff_genie, heff_refined };
            for (int s = 0; s < 2; s++) {
                gen_random_bits(tb[s], tbsz);
                attach_crc(tb[s], tbsz, CRC24A, tb_crc[s]);
                nr_seg_split(tb_crc[s], &seg, cb_bits[s]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[s] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[s]);
                    nr_ldpc_rate_match_select(coded[s], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[s][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[s], Er, selbits[s]);
                qam_modulate(selbits[s], E, mcs.modulation, sym[s]);

                double he_abs2 = CX_NORM(heff[s]);
                double nv = (he_abs2 > 1e-12) ? N0 / he_abs2 : N0 * 1e6;
                for (int d = 0; d < nd; d++) {
                    cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                    cx_t y = heff[s] * sym[s][d] + noise;
                    rx_hat[s][d] = (he_abs2 > 1e-12) ? y / heff[s] : CX_ZERO;
                }
                qam_demap_llr(rx_hat[s], nd, mcs.modulation, nv, allllr[s]);

                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[s], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[s], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[s] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[s], 25, decoded_cb[s]);
                    if (seg.C > 1 && !check_crc(decoded_cb[s], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[s] + (size_t)r * payload, decoded_cb[s], payload * sizeof(int));
                }

                int crc_ok = cb_crc_ok && check_crc(decoded_tb[s], B, CRC24A);
                int be = 0;
                for (int i = 0; i < tbsz; i++) if (tb[s][i] != decoded_tb[s][i]) be++;
                b_err[s]  += be;
                t_bits[s] += tbsz;
                if (!crc_ok || be > 0) e_blk[s]++;
            }
        }   /* end trial loop */

        double p2_eq_p1_rate = p2_eq_p1_cnt / (double)cfg->numTrials;
        double avg_gain_noswp   = gain_noswp_sum   / cfg->numTrials;
        double avg_gain_refined = gain_refined_sum / cfg->numTrials;
        double ber_genie   = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler_genie  = e_blk[0] / (double)cfg->numTrials;
        double ber_ref     = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler_ref    = e_blk[1] / (double)cfg->numTrials;

        printf("%-9.1f  %-11.4f  %-14.4f  %-14.4f  %-12.4e %-11.4f  %-12.4e %-11.4f\n",
               snr, p2_eq_p1_rate, avg_gain_noswp, avg_gain_refined,
               ber_genie, bler_genie, ber_ref, bler_ref);
    }   /* end SNR loop */

    printf("\nPDSCH Beam Management P1->P3->P2 simulation complete.\n");

    ldpc_free(&ldpc);
    free(Er);
    for (int s = 0; s < 2; s++) {
        free(tb[s]); free(tb_crc[s]); free(cb_bits[s]); free(coded[s]);
        for (int r = 0; r < seg.C; r++) free(rm[s][r]);
        free(rm[s]); free(selbits[s]);
        free(sym[s]); free(allllr[s]); free(soft_buf[s]);
        free(decoded_cb[s]); free(decoded_tb[s]); free(rx_hat[s]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 빔 관리(Beam Management) — SSB/CSI-RS 기반 P1 절차, TDL 주파수선택적 페이딩
 *
 * 평탄 버전 헤더 주석 참조. P1/Genie 빔 선택 자체는 wideband로 유지
 * (구현 정의 — 빔 관리는 실무에서도 데이터 스케줄링보다 훨씬 느린
 * 주기로 갱신되는 wideband 절차이므로, 매 RE 재선택하는 건 오히려
 * 비현실적). TDL은 선택된 빔의 실제 하향링크 데이터 전송에만 적용:
 * 단일 지배경로(LOS) 클러스터 자체의 다중경로/지연확산을 모사하는
 * SISO TDL tap-set 1개를 모든 32개 안테나에 공통으로 곱하는 형태로
 * 확장(같은 산란 클러스터는 배열 전체에 동일한 주파수응답을 준다는
 * 근사 — 클러스터 내부의 각도확산까지 모델링하려면 안테나별 독립
 * tap-set이 필요하지만 그러면 스티어링 벡터의 코히런트 구조 자체가
 * 깨져 "빔 관리"라는 시나리오 자체가 성립하지 않으므로 이번 확장의
 * 범위 밖).
 *
 * **분석적으로 확인한 사실**: 이 공유-스칼라 모델에서는 매 RE의 채널이
 * H_true[t]·g(RE) 형태(g(RE)가 모든 후보 빔에 동일하게 곱해짐)이므로,
 * 후보 빔 간 순위(|H_true^H·W_c|² 대소 비교)는 g(RE)의 값과 무관하게
 * 항상 평탄 버전과 정확히 동일하다 — 즉 TDL이 "어느 빔이 선택되는가"에는
 * 영향을 주지 않고, "선택된 빔이 데이터 평면에서 얼마나 잘 동작하는가"
 * (주파수선택적 페이딩에 따른 BLER)에만 영향을 준다. 이 프로젝트의
 * "CL_XPORT: wideband PMI 설계 + per-RE 실제 전송" 패턴과 정확히 같은
 * 철학.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_beam_mgmt_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int num_rep  = cfg->beamMgmtNumRep;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;

    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);   /* shared by both scenarios (same B/cr) */
    int payload = seg.Kprime - seg.L;
    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz  = ldpc.coded_size;
    int E     = num_data * bps;   /* TS 38.212 5.4.2.1 total rate-matched output
                                       length G per scenario */
    int nd    = num_data;   /* always the full RE budget now that E=num_data*bps */
    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);   /* shared by both scenarios (same E) */

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PDSCH Beam Management (SSB/CSI-RS P1, 32-port Tx 빔 스위핑), TDL 주파수선택적 페이딩 ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Candidate Beams: %d (i1_1=16 x i1_2=16 x i2=4, rank-1 코드북 전체)\n",
           BM_CAND_L * BM_CAND_M * BM_CAND_N2);
    printf("P1 Num Rep   : %d (빔당 RSRP 반복 관측 횟수, wideband — TDL 무관)\n", num_rep);
    printf("Channel      : 단일 클러스터 공유 SISO TDL tap-set(32안테나 공통), DS=%.0fns, %d taps\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Data RE      : %d\n", nd);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int    *tb[2], *tb_crc[2], *cb_bits[2], *coded[2], **rm[2], *selbits[2];
    int    *decoded_cb[2], *decoded_tb[2];
    cx_t   *sym[2];
    double *allllr[2], *soft_buf[2];
    cx_t   *rx_hat[2];
    for (int s = 0; s < 2; s++) {
        tb[s]       = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[s]   = (int    *)malloc(B     * sizeof(int));
        cb_bits[s]  = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded[s]    = (int    *)malloc(acsz  * sizeof(int));
        rm[s]       = (int   **)malloc(seg.C * sizeof(int *));
        for (int r = 0; r < seg.C; r++) rm[s][r] = (int *)malloc(Er[r] * sizeof(int));
        selbits[s]  = (int    *)malloc(E     * sizeof(int));
        sym[s]      = (cx_t  *)malloc(nd     * sizeof(cx_t));
        allllr[s]   = (double *)malloc(E     * sizeof(double));
        soft_buf[s] = (double *)malloc(acsz  * sizeof(double));
        decoded_cb[s] = (int  *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[s] = (int  *)malloc(B * sizeof(int));
        rx_hat[s]   = (cx_t  *)malloc(nd     * sizeof(cx_t));
    }
    cx_t *cluster_taps = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%-9s  %-11s %-12s %-11s  %-12s %-11s\n",
           "SNR(dB)", "P1==Genie", "BER_Genie", "BLER_Genie", "BER_P1", "BLER_P1");
    for (int i = 0; i < 78; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[2] = {0}, b_err[2] = {0};
        int  e_blk[2] = {0};
        int  match_cnt = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            double l_true = (double)rand() / RAND_MAX * BM_CAND_L;
            double m_true = (double)rand() / RAND_MAX * BM_CAND_M;
            double n_true = (double)rand() / RAND_MAX * BM_CAND_N2;
            cx_t H_true[32];
            beam_mgmt_true_channel(l_true, m_true, n_true, H_true);

            int gl, gm, gn; double gg;
            beam_mgmt_genie_best(H_true, &gl, &gm, &gn, &gg);
            int sl, sm, sn; double sg;
            beam_mgmt_p1_sweep(H_true, N0, num_rep, &sl, &sm, &sn, &sg);
            if (sl == gl && sm == gm && sn == gn) match_cnt++;

            cx_t Wg[32], Wp[32];
            codebook_type1_sp_32port_rank1(gl, gm, gn, Wg);
            codebook_type1_sp_32port_rank1(sl, sm, sn, Wp);
            cx_t hbeam[2] = {CX_ZERO, CX_ZERO};
            for (int t = 0; t < 32; t++) {
                hbeam[0] += conj(H_true[t]) * Wg[t];
                hbeam[1] += conj(H_true[t]) * Wp[t];
            }

            /* 지배경로 클러스터의 공유 주파수선택적 게인(모든 안테나 공통,
             * 빔 순위엔 영향 없음 — 위 헤더 주석 참조) */
            tdl_draw(&tdl_ch, cluster_taps);

            for (int s = 0; s < 2; s++) {
                gen_random_bits(tb[s], tbsz);
                attach_crc(tb[s], tbsz, CRC24A, tb_crc[s]);
                nr_seg_split(tb_crc[s], &seg, cb_bits[s]);
                for (int r = 0; r < seg.C; r++) {
                    const int *info = cb_bits[s] + (size_t)r * seg.Kprime;
                    ldpc_encode(&ldpc, info, coded[s]);
                    nr_ldpc_rate_match_select(coded[s], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, Er[r], rm[s][r]);
                }
                nr_seg_concat(&seg, (const int *const *)rm[s], Er, selbits[s]);
                qam_modulate(selbits[s], E, mcs.modulation, sym[s]);

                double nv_sum = 0.0;
                for (int d = 0; d < nd; d++) {
                    cx_t g_d = tdl_freq_response(&tdl_ch, cluster_taps, d);
                    cx_t he  = g_d * hbeam[s];
                    double he_abs2 = CX_NORM(he);
                    double nv = (he_abs2 > 1e-12) ? N0 / he_abs2 : N0 * 1e6;
                    nv_sum += nv;
                    cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                    cx_t y = he * sym[s][d] + noise;
                    rx_hat[s][d] = (he_abs2 > 1e-12) ? y / he : CX_ZERO;
                }
                double env = nv_sum / nd;
                qam_demap_llr(rx_hat[s], nd, mcs.modulation, env, allllr[s]);

                int roff = 0;
                int cb_crc_ok = 1;
                for (int r = 0; r < seg.C; r++) {
                    memset(soft_buf[s], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[s], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, Er[r], allllr[s] + roff);
                    roff += Er[r];
                    ldpc_decode(&ldpc, soft_buf[s], 25, decoded_cb[s]);
                    if (seg.C > 1 && !check_crc(decoded_cb[s], seg.Kprime, CRC24B))
                        cb_crc_ok = 0;
                    memcpy(decoded_tb[s] + (size_t)r * payload, decoded_cb[s], payload * sizeof(int));
                }

                int crc_ok = cb_crc_ok && check_crc(decoded_tb[s], B, CRC24A);
                int be = 0;
                for (int i = 0; i < tbsz; i++) if (tb[s][i] != decoded_tb[s][i]) be++;
                b_err[s]  += be;
                t_bits[s] += tbsz;
                if (!crc_ok || be > 0) e_blk[s]++;
            }
        }   /* end trial loop */

        double match_rate  = match_cnt / (double)cfg->numTrials;
        double ber_genie   = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler_genie  = e_blk[0] / (double)cfg->numTrials;
        double ber_p1      = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler_p1     = e_blk[1] / (double)cfg->numTrials;

        printf("%-9.1f  %-11.4f %-12.4e %-11.4f  %-12.4e %-11.4f\n",
               snr, match_rate, ber_genie, bler_genie, ber_p1, bler_p1);
    }   /* end SNR loop */

    printf("\nPDSCH Beam Management + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(cluster_taps); free(Er);
    for (int s = 0; s < 2; s++) {
        free(tb[s]); free(tb_crc[s]); free(cb_bits[s]); free(coded[s]);
        for (int r = 0; r < seg.C; r++) free(rm[s][r]);
        free(rm[s]); free(selbits[s]);
        free(sym[s]); free(allllr[s]); free(soft_buf[s]);
        free(decoded_cb[s]); free(decoded_tb[s]);
        free(rx_hat[s]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 빔 관리(Beam Management) — SSB/CSI-RS 기반 P1 절차, HARQ 순환버퍼 IR/Chase
 * (평탄 페이딩/TDL 모두 지원)
 *
 * 빔 선택(P1/Genie)은 트라이얼당 1회만 수행하고 모든 HARQ attempt에서
 * 재사용한다 — 빔 관리 갱신 주기가 HARQ 재전송 주기보다 훨씬 느리다는
 * 가정(구현 정의, CL_32PORT HARQ의 "RI/PMI는 attempt 0에서 고정" 패턴과
 * 동일 철학). 평탄 모드는 빔도 채널도 트라이얼 내내 고정이라 매 attempt
 * 잡음만 새로 뽑히고(순수 코딩이득), TDL 모드는 attempt마다 클러스터
 * tap-set을 재드로우해 시간 다이버시티를 모사(CL_32PORT/MU-MIMO HARQ와
 * 동일 관례). Genie 경로와 P1 경로는 이 프로젝트 다른 다중스트림 HARQ
 * 함수들과 동일하게 "같은 재전송 occasion 공유"(둘 다 CRC 통과해야
 * 종료) 단순화를 그대로 재사용 — 사실 두 경로는 물리적으로 공존하는
 * 자원을 나눠쓰는 게 아니라 같은 채널에 대한 두 가지 비교 시나리오라
 * 독립 재시도 루프가 더 정확하겠지만, 이 프로젝트의 기존 다중스트림
 * HARQ 관례와의 일관성을 위해 동일 패턴을 유지(구현 정의).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pdsch_beam_mgmt_harq_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int num_rep  = cfg->beamMgmtNumRep;

    int is_tdl = (strcmp(cfg->channelModel, "TDL") == 0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr   = get_code_rate(&mcs);
    int    bps  = mcs.modulationOrder;
    int    crc_bits = 24;

    int E    = num_data * bps;
    int tbsz = (int)(E * cr);
    if (tbsz < 1)    tbsz = 1;

    /* TS 38.212 5.2.2 multi-code-block segmentation (P0-2c HARQ follow-up,
     * 2026-09-03) -- see run_pdsch_harq_simulation() design note. Both
     * scenarios (Genie/P1) share identical tbsz/cr -> one NRSegInfo/
     * LDPCCodec/Er[] for both. */
    int B = tbsz + crc_bits;
    NRSegInfo seg; nr_seg_compute(B, cr, &seg);
    int payload = seg.Kprime - seg.L;

    LDPCCodec ldpc;
    ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                        seg.base_rows, seg.base_info_cols, seg.base_cols,
                        seg.filler_size);
    int acsz = ldpc.coded_size;

    int *Er = (int *)malloc(seg.C * sizeof(int));
    nr_ldpc_er_alloc(E, /*Nl=*/1, bps, seg.C, Er);

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PDSCH Beam Management (HARQ Circular Buffer %s), %s ===\n",
           is_chase ? "Chase" : "IR",
           is_tdl ? "TDL Frequency-Selective Fading" : "Flat Fading");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d/CB, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)seg.Kprime / acsz, acsz, ldpc.bg, ldpc.Zc);
    printf("Code Blocks  : C=%d/scenario%s\n", seg.C, seg.C > 1 ? " (CRC24B/CB)" : "");
    printf("Num RB       : %d\n", num_rb);
    printf("Candidate Beams: %d (i1_1=16 x i1_2=16 x i2=4, rank-1 코드북 전체)\n",
           BM_CAND_L * BM_CAND_M * BM_CAND_N2);
    printf("P1 Num Rep   : %d (빔 선택은 트라이얼당 1회, 모든 attempt에서 재사용)\n", num_rep);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : 단일 클러스터 공유 SISO TDL(attempt마다 재드로우), DS=%.0fns, %d taps\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    else
        printf("Channel      : 고정(빔도 채널도 트라이얼 내내 불변, attempt마다 잡음만 재드로우)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2], *tb_crc[2], *cb_bits[2], **coded_cw[2], **rm[2], *selbits[2];
    int   *decoded_cb[2], *decoded_tb[2];
    cx_t  *sym[2], *rx_hat[2];
    double *allllr[2], **soft_buf[2];
    for (int s = 0; s < 2; s++) {
        tb[s]         = (int    *)malloc(tbsz     * sizeof(int));
        tb_crc[s]     = (int    *)malloc(B        * sizeof(int));
        cb_bits[s]    = (int    *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
        coded_cw[s]   = (int   **)malloc(seg.C    * sizeof(int *));
        rm[s]         = (int   **)malloc(seg.C    * sizeof(int *));
        for (int r = 0; r < seg.C; r++) {
            coded_cw[s][r] = (int *)malloc(acsz  * sizeof(int));
            rm[s][r]       = (int *)malloc(Er[r] * sizeof(int));
        }
        selbits[s]    = (int    *)malloc(E        * sizeof(int));
        decoded_cb[s] = (int    *)malloc(ldpc.info_size * sizeof(int));
        decoded_tb[s] = (int    *)malloc(B        * sizeof(int));
        sym[s]        = (cx_t  *)malloc(num_data  * sizeof(cx_t));
        rx_hat[s]     = (cx_t  *)malloc(num_data  * sizeof(cx_t));
        allllr[s]     = (double *)malloc(E        * sizeof(double));
        soft_buf[s]   = (double **)malloc(seg.C   * sizeof(double *));
        for (int r = 0; r < seg.C; r++) soft_buf[s][r] = (double *)malloc(acsz * sizeof(double));
    }
    cx_t *cluster_taps = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%-9s  %-11s %10s%14s%14s%12s\n",
           "SNR(dB)", "P1==Genie", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i = 0; i < 78; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        int total_err=0, total_bits=0, blk_err_final=0, blk_err_1st=0;
        long long total_attempts = 0;
        int match_cnt = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* 빔 선택: 트라이얼당 1회, wideband, TDL 무관(위 TDL 함수 헤더의
             * 분석적 증명 참조) */
            double l_true = (double)rand() / RAND_MAX * BM_CAND_L;
            double m_true = (double)rand() / RAND_MAX * BM_CAND_M;
            double n_true = (double)rand() / RAND_MAX * BM_CAND_N2;
            cx_t H_true[32];
            beam_mgmt_true_channel(l_true, m_true, n_true, H_true);

            int gl, gm, gn; double gg;
            beam_mgmt_genie_best(H_true, &gl, &gm, &gn, &gg);
            int sl, sm, sn; double sg;
            beam_mgmt_p1_sweep(H_true, N0, num_rep, &sl, &sm, &sn, &sg);
            if (sl == gl && sm == gm && sn == gn) match_cnt++;

            cx_t Wg[32], Wp[32];
            codebook_type1_sp_32port_rank1(gl, gm, gn, Wg);
            codebook_type1_sp_32port_rank1(sl, sm, sn, Wp);
            cx_t hbeam[2] = {CX_ZERO, CX_ZERO};
            for (int t = 0; t < 32; t++) {
                hbeam[0] += conj(H_true[t]) * Wg[t];
                hbeam[1] += conj(H_true[t]) * Wp[t];
            }

            for (int s = 0; s < 2; s++) {
                gen_random_bits(tb[s], tbsz);
                attach_crc(tb[s], tbsz, CRC24A, tb_crc[s]);
                nr_seg_split(tb_crc[s], &seg, cb_bits[s]);
                for (int r = 0; r < seg.C; r++) {
                    ldpc_encode(&ldpc, cb_bits[s] + (size_t)r * seg.Kprime, coded_cw[s][r]);
                    for (int i = 0; i < acsz; i++) soft_buf[s][r][i] = 0.0;
                }
            }

            int crc_ok[2]={0,0}, be[2]={0,0}, be_1st[2]={0,0}, crc_1st[2]={0,0};
            int attempts = 0;

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int s = 0; s < 2; s++) {
                    for (int r = 0; r < seg.C; r++)
                        nr_ldpc_rate_match_select(coded_cw[s][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], rm[s][r]);
                    nr_seg_concat(&seg, (const int *const *)rm[s], Er, selbits[s]);
                    qam_modulate(selbits[s], E, mcs.modulation, sym[s]);
                }

                /* TDL이면 클러스터 게인을 attempt마다 재드로우(시간
                 * 다이버시티) — flat이면 빔·채널 모두 고정, 잡음만 재드로우 */
                if (is_tdl) tdl_draw(&tdl_ch, cluster_taps);

                double nv_sum[2] = {0.0, 0.0};
                for (int d = 0; d < num_data; d++) {
                    cx_t g_d = is_tdl ? tdl_freq_response(&tdl_ch, cluster_taps, d)
                                       : CX_MAKE(1.0, 0.0);
                    for (int s = 0; s < 2; s++) {
                        cx_t he = g_d * hbeam[s];
                        double he_abs2 = CX_NORM(he);
                        double nv = (he_abs2 > 1e-12) ? N0 / he_abs2 : N0 * 1e6;
                        nv_sum[s] += nv;
                        cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                        cx_t y = he * sym[s][d] + noise;
                        rx_hat[s][d] = (he_abs2 > 1e-12) ? y / he : CX_ZERO;
                    }
                }

                for (int s = 0; s < 2; s++) {
                    double env = nv_sum[s] / num_data;
                    qam_demap_llr(rx_hat[s], num_data, mcs.modulation, env, allllr[s]);
                    int roff = 0;
                    int cb_crc_ok = 1;
                    for (int r = 0; r < seg.C; r++) {
                        nr_ldpc_rate_match_combine(soft_buf[s][r], acsz, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, Er[r], allllr[s] + roff);
                        roff += Er[r];
                        ldpc_decode(&ldpc, soft_buf[s][r], 25, decoded_cb[s]);
                        if (seg.C > 1 && !check_crc(decoded_cb[s], seg.Kprime, CRC24B)) cb_crc_ok = 0;
                        memcpy(decoded_tb[s] + (size_t)r * payload, decoded_cb[s], payload * sizeof(int));
                    }
                    crc_ok[s] = cb_crc_ok && check_crc(decoded_tb[s], B, CRC24A);
                    int biterr = 0;
                    for (int i = 0; i < tbsz; i++)
                        if (tb[s][i] != decoded_tb[s][i]) biterr++;
                    be[s] = biterr;
                    if (attempt == 0) { be_1st[s] = biterr; crc_1st[s] = crc_ok[s]; }
                }

                attempts = attempt + 1;
                if (crc_ok[0] && crc_ok[1]) break;
            }   /* end attempt loop */

            int any_err_1st = 0, any_err_final = 0;
            for (int s = 0; s < 2; s++) {
                total_err  += be[s];
                total_bits += tbsz;
                if (!crc_1st[s] || be_1st[s] > 0) any_err_1st   = 1;
                if (!crc_ok[s]  || be[s]    > 0) any_err_final = 1;
            }
            if (any_err_1st)   blk_err_1st++;
            if (any_err_final) blk_err_final++;
            total_attempts += attempts;
        }   /* end trial loop */

        double match_rate = match_cnt / (double)cfg->numTrials;
        double ber        = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double bler_1st   = (double)blk_err_1st   / cfg->numTrials;
        double bler_final = (double)blk_err_final / cfg->numTrials;
        double avg_tx     = (double)total_attempts / cfg->numTrials;
        printf("%-9.1f  %-11.4f %10.4e%14.4f%14.4f%12.2f\n",
               snr, match_rate, ber, bler_1st, bler_final, avg_tx);
    }   /* end SNR loop */

    printf("\nPDSCH Beam Management + %s + HARQ simulation complete.\n", is_tdl ? "TDL" : "Flat Fading");

    ldpc_free(&ldpc);
    free(cluster_taps); free(Er);
    for (int s = 0; s < 2; s++) {
        free(tb[s]); free(tb_crc[s]); free(cb_bits[s]);
        for (int r = 0; r < seg.C; r++) { free(coded_cw[s][r]); free(rm[s][r]); free(soft_buf[s][r]); }
        free(coded_cw[s]); free(rm[s]); free(soft_buf[s]);
        free(selbits[s]); free(decoded_cb[s]); free(decoded_tb[s]);
        free(sym[s]); free(rx_hat[s]);
        free(allllr[s]);
    }
}
