#include "pusch.h"
#include "pusch_codebook_4port.h"
#include "crc.h"
#include "mcs_table.h"
#include "ldpc.h"
#include "nr_rate_matching.h"
#include "nr_sch.h"
#include "tbs.h"
#include "modulation.h"
#include "dmrs.h"
#include "channel_estimation.h"
#include "tdl.h"
#include "tdl_time.h"
#include "tdl_mimo.h"
#include "mimo.h"
#include "utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Effective precoded channel, indexed as [RE][Rx][layer]. */
#define HE(grid,k,r,l) ((grid)[((size_t)(k)*4+(r))*4+(l)])

/* The first draw determines RI/TPMI. HARQ keeps both fixed across attempts. */
static void selection_channel(const TDLChannel *ch, int is_tdl, int center,
                              const cx_t flat[4][4],
                              const cx_t taps[4][4][TDL_MAX_TAPS], cx_t H[4][4]) {
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++)
            H[r][t] = is_tdl ? tdl_freq_response(ch, taps[r][t], center) : flat[r][t];
}

static void detect_re(int rank, cx_t h[4][4], const cx_t y[4], double N0,
                      cx_t xh[4], double nv[4]) {
    if (rank == 1) {
        cx_t h1[4];
        for (int r = 0; r < 4; r++) h1[r] = h[r][0];
        mrc_combine_4rx(h1, y, N0, &xh[0], &nv[0]);
    } else if (rank == 2) {
        cx_t h2[4][2];
        for (int r = 0; r < 4; r++)
            for (int l = 0; l < 2; l++) h2[r][l] = h[r][l];
        mimo_mmse_detect_4rx2(h2, y, N0, xh, nv);
    } else if (rank == 3) {
        cx_t h3[4][3];
        for (int r = 0; r < 4; r++)
            for (int l = 0; l < 3; l++) h3[r][l] = h[r][l];
        mimo_mmse_detect_4rx3(h3, y, N0, xh, nv);
    } else {
        mimo_mmse_detect_4x4(h, y, N0, xh, nv);
    }
}

/* Shared TDL and/or HARQ path. Flat non-HARQ keeps its existing driver. */
void run_pusch_ul_cb_4port_extended_simulation(const L1Config *cfg) {
    const int is_tdl = strcmp(cfg->channelModel, "TDL") == 0;
    const int is_harq = cfg->harqEnable;
    const int correlated = cfg->tdlTimeCorrelation;
    TDLTimeState *time_state = correlated ? malloc(16*sizeof(*time_state)) : NULL;
    if (correlated && !time_state) {
        fprintf(stderr,"PUSCH: cannot allocate TDL time states\n"); exit(1);
    }
    const int active = cfg->numRB * 12;
    const int num_data = cfg->numRB * 6;
    const int num_pilots = cfg->numRB * 6;
    const int max_tx = is_harq ? cfg->harqMaxRetx : 1;
    const int chase = strcmp(cfg->harqRvSeq, "CHASE") == 0;
    const int rvseq[4] = {0, 2, 3, 1};
    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    const double cr = get_code_rate(&mcs);
    const int bps = mcs.modulationOrder;
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos = (int *)malloc(num_data * sizeof(int));
    dmrs_pilot_indices(cfg->numRB, pilot_pos);
    dmrs_data_indices(cfg->numRB, data_pos);
    cx_t *he_grid = (cx_t *)malloc((size_t)active * 16 * sizeof(cx_t));

    printf("=== PUSCH UL 4-port codebook (%s%s) ===\n",
           is_tdl ? "TDL" : "flat", is_harq ? " + HARQ" : "");
    printf("RI/TPMI      : first-draw channel at center RE; fixed per HARQ burst\n");
    printf("MCS/Table    : %d / %s, Rx=4, CP-OFDM/MMSE\n", cfg->mcsIndex, cfg->mcsTableType);
    if (is_tdl) printf("TDL          : %s, DS=%.0f ns, per-pair taps\n", cfg->tdlProfile, cfg->tdlDelaySpreadNs);
    if (cfg->tdlSpatialCorrTx > 0.0 || cfg->tdlSpatialCorrRx > 0.0)
        printf("TDL spatial  : exponential Tx=%.3f Rx=%.3f (research model)\n",
               cfg->tdlSpatialCorrTx, cfg->tdlSpatialCorrRx);
    if (correlated)
        printf("TDL time     : correlated, fD=%.3f Hz, interval=%.3f ms; constant within each attempt\n",
               cfg->tdlMaxDopplerHz, cfg->tdlHarqIntervalMs);
    if (is_harq) printf("HARQ         : %s, max %d attempts\n", chase ? "Chase" : "IR", max_tx);
    printf("%10s %13s %12s %12s %8s %8s %8s %8s %8s\n",
           "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(final)", "AvgTx", "R1%", "R2%", "R3%", "R4%");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        const double N0 = 1.0 / pow(10.0, snr / 10.0);
        const double sigma = sqrt(N0 / 2.0);
        TDLChannel ch;
        if (is_tdl)
            tdl_channel_init(&ch, cfg->tdlProfile[0], cfg->tdlDelaySpreadNs,
                             (double)cfg->scsKHz * 1000.0, snr);
        long total_bits = 0, total_err = 0, total_tx = 0;
        int block_first = 0, block_final = 0, rank_cnt[5] = {0};

        for (int trial = 0; trial < cfg->numTrials; trial++) {
            cx_t flat[4][4] = {{0}}, taps[4][4][TDL_MAX_TAPS], Hsel[4][4];
            if (is_tdl) {
                for (int r = 0; r < 4; r++)
                    for (int t = 0; t < 4; t++) {
                        if (correlated) {
                            if (tdl_time_init(&time_state[r*4+t], &ch, cfg->tdlMaxDopplerHz) ||
                                tdl_time_sample(&time_state[r*4+t], 0.0, taps[r][t])) {
                                fprintf(stderr,"PUSCH: invalid TDL time initialization\n"); exit(1);
                            }
                        } else tdl_draw(&ch, taps[r][t]);
                    }
            } else mimo_channel_draw_4x4(flat);
            if (is_tdl && tdl_mimo_correlate_4x4(taps,ch.profile.num_taps,
                                                cfg->tdlSpatialCorrTx,cfg->tdlSpatialCorrRx)) {
                fprintf(stderr,"PUSCH: invalid TDL spatial correlation\n"); exit(1);
            }
            selection_channel(&ch, is_tdl, active/2, flat, taps, Hsel);
            int rank, tpmi;
            ul_cb4_select((const cx_t (*)[4])Hsel, N0, &rank, &tpmi);
            rank_cnt[rank]++;
            cx_t W[4][4]; ul_cb4_precoder(rank, tpmi, W);
            double beta = ul_cb4_unit_power_beta((const cx_t (*)[4])W, rank);

            int E = num_data * rank * bps;
            int tbsz = nr_determine_tbs(E, cr);
            int B = tbsz + 24;
            NRSegInfo seg; nr_seg_compute(B, cr, &seg);
            int payload = seg.Kprime - seg.L;
            LDPCCodec ldpc;
            ldpc_init_resolved(&ldpc, seg.Kprime, cr, seg.bg, seg.Zc, seg.Kb,
                               seg.base_rows, seg.base_info_cols, seg.base_cols,
                               seg.filler_size);
            int acsz = ldpc.coded_size;
            int *Er = (int *)malloc(seg.C * sizeof(int));
            nr_ldpc_er_alloc(E, rank, bps, seg.C, Er);
            int *tb = (int *)malloc(tbsz * sizeof(int));
            int *tb_crc = (int *)malloc(B * sizeof(int));
            int *cb_bits = (int *)malloc((size_t)seg.C * seg.Kprime * sizeof(int));
            int **coded = (int **)malloc(seg.C * sizeof(int *));
            int **rm = (int **)malloc(seg.C * sizeof(int *));
            double **soft = (double **)malloc(seg.C * sizeof(double *));
            int *selbits = (int *)malloc(E * sizeof(int));
            cx_t *syms = (cx_t *)malloc((size_t)num_data * rank * sizeof(cx_t));
            double *allllr = (double *)malloc(E * sizeof(double));
            int *decoded_cb = (int *)malloc(ldpc.info_size * sizeof(int));
            int *decoded_tb = (int *)malloc(B * sizeof(int));
            cx_t *dmrs[4] = {0}, *rx_hat[4] = {0};
            double *layer_llr[4] = {0};
            double *noise_re[4] = {0};
            int *pilotL[4] = {0}, nppl[4] = {0};
            cx_t *hfull[4][4] = {{0}}, *hp[4][4] = {{0}};
            for (int c = 0; c < seg.C; c++) {
                coded[c] = (int *)malloc(acsz * sizeof(int));
                rm[c] = (int *)malloc(Er[c] * sizeof(int));
                soft[c] = (double *)calloc(acsz, sizeof(double));
            }
            for (int l = 0; l < rank; l++) {
                nppl[l] = (num_pilots + rank - 1 - l) / rank;
                pilotL[l] = (int *)malloc(nppl[l] * sizeof(int));
                dmrs[l] = (cx_t *)malloc(nppl[l] * sizeof(cx_t));
                rx_hat[l] = (cx_t *)malloc(num_data * sizeof(cx_t));
                layer_llr[l] = (double *)malloc((size_t)num_data * bps * sizeof(double));
                noise_re[l] = (double *)malloc(num_data * sizeof(double));
                for (int p = 0; p < nppl[l]; p++) pilotL[l][p] = pilot_pos[p*rank+l];
                dmrs_sequence(0x87654321u+(uint32_t)l*0x10203u, nppl[l], dmrs[l]);
                for (int r = 0; r < 4; r++) {
                    hp[r][l] = (cx_t *)malloc(nppl[l] * sizeof(cx_t));
                    hfull[r][l] = (cx_t *)malloc(active * sizeof(cx_t));
                }
            }
            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            nr_seg_split(tb_crc, &seg, cb_bits);
            for (int c = 0; c < seg.C; c++)
                ldpc_encode(&ldpc, cb_bits + (size_t)c*seg.Kprime, coded[c]);

            int first_failed = 0, final_be = 0, final_ok = 0, attempts = 0;
            for (int attempt = 0; attempt < max_tx; attempt++) {
                if (attempt > 0) {
                    if (is_tdl) {
                        for (int r = 0; r < 4; r++)
                            for (int t = 0; t < 4; t++) {
                                if (correlated) {
                                    double time_s = attempt*(cfg->tdlHarqIntervalMs/1000.0);
                                    if (tdl_time_sample(&time_state[r*4+t], time_s, taps[r][t])) {
                                        fprintf(stderr,"PUSCH: invalid HARQ sample time\n"); exit(1);
                                    }
                                } else tdl_draw(&ch, taps[r][t]);
                            }
                        if (tdl_mimo_correlate_4x4(taps,ch.profile.num_taps,
                                                   cfg->tdlSpatialCorrTx,cfg->tdlSpatialCorrRx)) {
                            fprintf(stderr,"PUSCH: invalid TDL spatial correlation\n"); exit(1);
                        }
                    } else mimo_channel_draw_4x4(flat);
                }
                int rv = chase ? 0 : rvseq[attempt % 4];
                for (int c = 0; c < seg.C; c++)
                    nr_sch_rate_match_select(coded[c], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols*ldpc.Zc,
                                               rv, Er[c], bps, rm[c]);
                nr_seg_concat(&seg, (const int *const *)rm, Er, selbits);
                qam_modulate(selbits, E, mcs.modulation, syms);

                for (int k = 0; k < active; k++)
                    for (int r = 0; r < 4; r++)
                        for (int l = 0; l < rank; l++) {
                            cx_t he = 0.0;
                            for (int t = 0; t < 4; t++) {
                                cx_t h = is_tdl ? tdl_freq_response(&ch, taps[r][t], k) : flat[r][t];
                                he += h * W[t][l] * beta;
                            }
                            HE(he_grid,k,r,l) = he;
                        }
                for (int l = 0; l < rank; l++)
                    for (int p = 0; p < nppl[l]; p++) {
                        int k = pilotL[l][p];
                        for (int r = 0; r < 4; r++) {
                            cx_t y = HE(he_grid,k,r,l) * dmrs[l][p]
                                     + CX_MAKE(randn()*sigma, randn()*sigma);
                            hp[r][l][p] = y / dmrs[l][p];
                        }
                    }
                for (int r = 0; r < 4; r++)
                    for (int l = 0; l < rank; l++) {
                        if (is_tdl)
                            interpolate_channel(hp[r][l], nppl[l], pilotL[l], active, hfull[r][l]);
                        else {
                            cx_t avg = 0.0;
                            for (int p = 0; p < nppl[l]; p++) avg += hp[r][l][p];
                            avg /= nppl[l];
                            for (int k = 0; k < active; k++) hfull[r][l][k] = avg;
                        }
                    }

                for (int d = 0; d < num_data; d++) {
                    int k = data_pos[d];
                    cx_t y[4], hh[4][4] = {{0}}, xh[4] = {0};
                    double nv[4] = {0};
                    for (int r = 0; r < 4; r++) {
                        y[r] = CX_MAKE(randn()*sigma, randn()*sigma);
                        for (int l = 0; l < rank; l++) {
                            y[r] += HE(he_grid,k,r,l) * syms[d*rank+l];
                            hh[r][l] = hfull[r][l][k];
                        }
                    }
                    detect_re(rank, hh, y, N0, xh, nv);
                    for (int l = 0; l < rank; l++) {
                        rx_hat[l][d] = xh[l]; noise_re[l][d] = nv[l];
                    }
                }
                for (int l = 0; l < rank; l++)
                    /* Preserve each RE's detector reliability before HARQ combining. */
                    qam_demap_llr_re(rx_hat[l], num_data, mcs.modulation,
                                     noise_re[l], layer_llr[l]);
                for (int d = 0; d < num_data; d++)
                    for (int l = 0; l < rank; l++)
                        for (int b = 0; b < bps; b++)
                            allllr[(d*rank+l)*bps+b] = layer_llr[l][d*bps+b];

                int cb_ok = 1, roff = 0;
                for (int c = 0; c < seg.C; c++) {
                    nr_sch_rate_match_combine(soft[c], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols*ldpc.Zc,
                                                rv, Er[c], bps, allllr+roff);
                    roff += Er[c];
                    ldpc_decode(&ldpc, soft[c], 25, decoded_cb);
                    if (seg.C > 1 && !check_crc(decoded_cb, seg.Kprime, CRC24B)) cb_ok = 0;
                    memcpy(decoded_tb+(size_t)c*payload, decoded_cb, payload*sizeof(int));
                }
                final_be = 0;
                for (int i = 0; i < tbsz; i++) if (tb[i] != decoded_tb[i]) final_be++;
                final_ok = cb_ok && check_crc(decoded_tb, B, CRC24A) && final_be == 0;
                attempts++;
                if (attempt == 0) first_failed = !final_ok;
                if (final_ok) break;
            }
            block_first += first_failed;
            block_final += !final_ok;
            total_bits += tbsz; total_err += final_be; total_tx += attempts;

            for (int l = 0; l < rank; l++) {
                for (int r = 0; r < 4; r++) { free(hp[r][l]); free(hfull[r][l]); }
                free(pilotL[l]); free(dmrs[l]); free(rx_hat[l]); free(layer_llr[l]);
                free(noise_re[l]);
            }
            for (int c = 0; c < seg.C; c++) { free(coded[c]); free(rm[c]); free(soft[c]); }
            free(coded); free(rm); free(soft); free(Er); free(tb); free(tb_crc);
            free(cb_bits); free(selbits); free(syms); free(allllr);
            free(decoded_cb); free(decoded_tb); ldpc_free(&ldpc);
        }
        printf("%10.1f %13.4e %12.4f %12.4f %8.2f %8.1f %8.1f %8.1f %8.1f\n",
               snr, total_bits ? (double)total_err/total_bits : 0.0,
               (double)block_first/cfg->numTrials, (double)block_final/cfg->numTrials,
               (double)total_tx/cfg->numTrials,
               100.0*rank_cnt[1]/cfg->numTrials, 100.0*rank_cnt[2]/cfg->numTrials,
               100.0*rank_cnt[3]/cfg->numTrials, 100.0*rank_cnt[4]/cfg->numTrials);
    }
    free(pilot_pos); free(data_pos); free(he_grid); free(time_state);
    printf("PUSCH UL 4-port codebook extended simulation complete.\n");
}

#undef HE
