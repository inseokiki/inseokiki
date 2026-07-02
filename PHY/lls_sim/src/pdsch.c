#include "pdsch.h"
#include "crc.h"
#include "mcs_table.h"
#include "ldpc.h"
#include "modulation.h"
#include "channel.h"
#include "dmrs.h"
#include "channel_estimation.h"
#include "tdl_channel.h"
#include "harq.h"
#include "tbs.h"
#include "mimo.h"
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

/* =========================================================================
 * run_pdsch_tdl_harq_simulation  (v3 — Phase 3)
 *
 * PDSCH + DMRS + TDL 다중경로 채널 + HARQ IR + MIMO MRC
 *
 * - 채널 모델    : TDL-A / TDL-C / TDL-D (TS 38.901)
 * - HARQ         : 최대 harqMaxRounds회, RV=0/1/2/3, LLR soft combining
 * - TBS          : TS 38.214 Section 5.1.3.2 정규 계산
 * - MIMO         : 1~4 Rx 안테나, MRC combining (IID per Rx)
 * - 채널 추정    : DMRS LS + 선형 보간, 안테나별 독립 추정
 * - 등화         : EQUALIZER=MMSE → per-SC LLR (기본)
 *                  EQUALIZER=ZF   → 평균 채널 파워 기반 flat LLR
 * - 조기 종료    : MIN_BLOCK_ERRORS 도달 시 해당 SNR 포인트 중단
 * - AWGN 기준선  : 동일 MCS, 단일 HARQ 라운드, flat 채널과 비교
 * - Throughput   : TBS * (1 - BLER) / num_data_RE [bits/RE]
 *
 * CSV 출력:
 *   SNR_dB, BLER_Tx1.., BLER_AWGN, Tput_Tx1.., Tput_AWGN, N_trials
 * ========================================================================= */
void run_pdsch_tdl_harq_simulation(const L1Config *cfg)
{
    /* ── MCS / 코딩 ─────────────────────────────────────────────────────── */
    MCSTableType tbl = (strcmp(cfg->mcsTableType, "TABLE2") == 0)
                       ? MCS_TABLE2 : MCS_TABLE1;
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr    = get_code_rate(&mcs);
    int    bps   = mcs.modulationOrder;
    int    crc_bits = 24;

    int num_rb   = (cfg->numRB > 0) ? cfg->numRB : 25;
    int active   = num_rb * 12;
    int num_pilots = 6 * num_rb;   /* DMRS Type-1: 6 RE/RB */
    int num_data   = 6 * num_rb;   /* 데이터 RE: 6 RE/RB   */

    /* ── TBS (TS 38.214 Section 5.1.3.2) ─────────────────────────────────
     * n_symb=1, n_dmrs_prb=0: 리소스 그리드에서 이미 파일럿 제외됨      */
    int tbsz = (cfg->tbSize > 0)
               ? cfg->tbSize
               : calc_tbs_from_nre(num_data, cr, bps, 1 /*layers*/);
    if (tbsz < 24)   tbsz = 24;
    if (tbsz > 8424) tbsz = 8424;

    int K    = tbsz + crc_bits;
    LDPCCodec ldpc;
    ldpc_init(&ldpc, K, cr);
    int N_cb = ldpc.coded_size;

    /* Rate-matched bits per HARQ round */
    int E = num_data * bps;
    if (E > N_cb) E = N_cb;

    /* ── HARQ ──────────────────────────────────────────────────────────── */
    int max_rounds = (cfg->harqMaxRounds >= 1 && cfg->harqMaxRounds <= 4)
                     ? cfg->harqMaxRounds : 4;

    /* ── MIMO ──────────────────────────────────────────────────────────── */
    int Nrx = (cfg->numRxAnt >= 1 && cfg->numRxAnt <= MIMO_MAX_RX)
              ? cfg->numRxAnt : 1;

    /* ── 조기 종료 임계값 ─────────────────────────────────────────────── */
    int min_block_errors = (cfg->minBlockErrors > 0) ? cfg->minBlockErrors : 200;

    /* ── 등화기 모드 ──────────────────────────────────────────────────── */
    /* MMSE(기본): per-SC 노이즈 분산 LLR  /  ZF: 평균 채널 파워 기반 flat LLR */
    int use_persc = (strcmp(cfg->equalizer, "ZF") != 0);

    /* ── TDL 채널 ──────────────────────────────────────────────────────── */
    TDLModel tdl_model = TDL_A;
    if      (strcmp(cfg->tdlModel, "TDL_C") == 0) tdl_model = TDL_C;
    else if (strcmp(cfg->tdlModel, "TDL_D") == 0) tdl_model = TDL_D;

    double scs_hz = cfg->scsKHz * 1e3;
    int    nfft   = (cfg->nfft > 0) ? cfg->nfft : 1024;
    int    cp     = (cfg->cpLengthNormal > 0)
                    ? cfg->cpLengthNormal
                    : (int)(144.0 * nfft / 2048.0 + 0.5);
    double Tsym_s = (double)(nfft + cp) / (scs_hz * nfft);

    /* ── DMRS 인덱스 / 시퀀스 ─────────────────────────────────────────── */
    const uint32_t c_init = 0x12345678u;
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices (num_rb, data_pos);
    cx_t *dmrs_sym = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    dmrs_sequence(c_init, num_pilots, dmrs_sym);

    /* ── 헤더 출력 ─────────────────────────────────────────────────────── */
    printf("=== PDSCH + TDL + HARQ IR + MIMO MRC Simulation ===\n");
    printf("Channel      : %s  (DS_rms=%.0f ns, fd=%.1f Hz)\n",
           cfg->tdlModel, cfg->tdlDsRmsNs, cfg->dopplerHz);
    printf("MCS Index    : %d (Table %s)  |  %s  Qm=%d  R=%.4f\n",
           cfg->mcsIndex, cfg->mcsTableType, mcs.modulation, bps, cr);
    printf("Num RB       : %d  |  Data RE: %d  |  E: %d bits\n",
           num_rb, num_data, E);
    printf("TBS (38.214) : %d bits  |  N_cb: %d bits\n", tbsz, N_cb);
    printf("MIMO         : 1x%d MRC  |  EQ: %s\n", Nrx,
           use_persc ? "MMSE(per-SC LLR)" : "ZF(flat LLR)");
    printf("Max HARQ     : %d rounds  |  Trials: %d  |  Min errors: %d\n\n",
           max_rounds, cfg->numTrials, min_block_errors);

    /* 콘솔 컬럼 헤더 */
    printf("%10s", "SNR(dB)");
    for (int r = 0; r < max_rounds; r++) printf("  BLER_Tx%d", r+1);
    printf("  BLER_AWGN");
    for (int r = 0; r < max_rounds; r++) printf("  Tput_Tx%d", r+1);
    printf("  Tput_AWGN  N_trials\n");
    for (int i = 0; i < 12 + 10 * (max_rounds * 2 + 1) + 10; i++) printf("-");
    printf("\n");

    /* ── 메모리 할당 ───────────────────────────────────────────────────── */
    int   *tb       = (int *)malloc(tbsz   * sizeof(int));
    int   *tb_crc   = (int *)malloc(K      * sizeof(int));
    int   *coded    = (int *)malloc(N_cb   * sizeof(int));
    int   *rm_bits  = (int *)malloc(E      * sizeof(int));
    cx_t  *data_syms= (cx_t *)malloc((E/bps + 1) * sizeof(cx_t));
    int   *decoded  = (int *)malloc(K * sizeof(int));
    double *llr_rx  = (double *)malloc(E * sizeof(double));

    /* 안테나별 리소스 그리드 / 채널 추정 버퍼 */
    cx_t  *tx_grid              = (cx_t *)malloc(active     * sizeof(cx_t));
    cx_t  *rx_grid_all[MIMO_MAX_RX];
    cx_t  *h_full_all [MIMO_MAX_RX];
    cx_t  *rx_data_all[MIMO_MAX_RX];
    cx_t  *h_data_all [MIMO_MAX_RX];
    for (int m = 0; m < Nrx; m++) {
        rx_grid_all[m] = (cx_t *)malloc(active     * sizeof(cx_t));
        h_full_all [m] = (cx_t *)malloc(active     * sizeof(cx_t));
        rx_data_all[m] = (cx_t *)malloc(num_data   * sizeof(cx_t));
        h_data_all [m] = (cx_t *)malloc(num_data   * sizeof(cx_t));
    }
    cx_t  *rx_pilots= (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t  *h_pilots = (cx_t *)malloc(num_pilots * sizeof(cx_t));

    /* MRC 출력 버퍼 (AWGN 기준선 수신 버퍼로도 재사용) */
    cx_t   *y_mrc  = (cx_t   *)malloc(num_data * sizeof(cx_t));
    double *nv_eff = (double  *)malloc(num_data * sizeof(double));

    HARQBuffer hbuf;
    harq_init(&hbuf, N_cb);

    /* CSV */
    FILE *csv = NULL;
    if (cfg->outputCsv) {
        csv = fopen(cfg->outputFile, "w");
        if (csv) {
            fprintf(csv, "SNR_dB");
            for (int r = 0; r < max_rounds; r++) fprintf(csv, ",BLER_Tx%d", r+1);
            fprintf(csv, ",BLER_AWGN");
            for (int r = 0; r < max_rounds; r++) fprintf(csv, ",Tput_Tx%d_bits_RE", r+1);
            fprintf(csv, ",Tput_AWGN_bits_RE,N_trials\n");
        }
    }

    /* ── SNR 루프 ──────────────────────────────────────────────────────── */
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-9; snr += cfg->snrStep) {

        double N0 = 1.0 / pow(10.0, snr / 10.0);

        /* 안테나별 TDL 채널 초기화 */
        TDLChannel ch[MIMO_MAX_RX];
        for (int m = 0; m < Nrx; m++)
            tdl_init(&ch[m], tdl_model, cfg->tdlDsRmsNs, cfg->dopplerHz,
                     snr, scs_hz, Tsym_s);

        /* ── TDL + HARQ 시뮬 ─────────────────────────────────────────── */
        int blk_err[4] = {0, 0, 0, 0};
        int total_trials = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {
            total_trials++;

            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded);

            harq_reset(&hbuf);
            int still_err[4] = {1, 1, 1, 1};

            for (int rv = 0; rv < max_rounds; rv++) {

                harq_rate_match(coded, N_cb, E, rv, rm_bits);
                int nsym_tx = E / bps;
                qam_modulate(rm_bits, E, mcs.modulation, data_syms);

                for (int k = 0; k < active; k++) tx_grid[k] = CX_ZERO;
                for (int p = 0; p < num_pilots; p++)
                    tx_grid[pilot_pos[p]] = dmrs_sym[p];
                int nd = (nsym_tx < num_data) ? nsym_tx : num_data;
                for (int d = 0; d < nd; d++)
                    tx_grid[data_pos[d]] = data_syms[d];

                for (int m = 0; m < Nrx; m++) {
                    tdl_new_realization(&ch[m]);
                    tdl_apply_cfr(&ch[m], tx_grid, active, rx_grid_all[m]);

                    for (int p = 0; p < num_pilots; p++)
                        rx_pilots[p] = rx_grid_all[m][pilot_pos[p]];
                    ls_estimate(rx_pilots, dmrs_sym, num_pilots, h_pilots);
                    interpolate_channel(h_pilots, num_pilots, pilot_pos,
                                        active, h_full_all[m]);

                    for (int d = 0; d < nd; d++) {
                        rx_data_all[m][d] = rx_grid_all[m][data_pos[d]];
                        h_data_all [m][d] = h_full_all [m][data_pos[d]];
                    }
                }

                mrc_combine(
                    (const cx_t * const *)rx_data_all,
                    (const cx_t * const *)h_data_all,
                    Nrx, nd, N0, y_mrc, nv_eff);

                /* 등화기 모드에 따른 LLR 계산 */
                if (use_persc) {
                    qam_demap_llr_persc(y_mrc, nd, mcs.modulation, nv_eff, llr_rx);
                } else {
                    /* ZF flat: 데이터 SC 평균 채널 파워로 단일 노이즈 분산 */
                    double h2_sum = 0.0;
                    for (int d = 0; d < nd; d++) {
                        double hp = 0.0;
                        for (int m = 0; m < Nrx; m++)
                            hp += CX_NORM(h_data_all[m][d]);
                        h2_sum += hp;
                    }
                    double nv_flat = (h2_sum > 1e-10) ? N0 * nd / h2_sum : N0;
                    qam_demap_llr(y_mrc, nd, mcs.modulation, nv_flat, llr_rx);
                }

                for (int i = nd * bps; i < E; i++) llr_rx[i] = 0.0;

                harq_combine(&hbuf, llr_rx, E, rv);
                ldpc_decode(&ldpc, harq_get_buf(&hbuf), 25, decoded);

                int crc_ok    = check_crc(decoded, K, CRC24A);
                still_err[rv] = crc_ok ? 0 : 1;
                if (crc_ok) {
                    for (int r2 = rv + 1; r2 < max_rounds; r2++) still_err[r2] = 0;
                    break;
                }
            } /* end HARQ */

            for (int r = 0; r < max_rounds; r++) blk_err[r] += still_err[r];

            /* 조기 종료: 1라운드 에러가 임계값에 도달하면 이 SNR 포인트 중단 */
            if (blk_err[0] >= min_block_errors) break;

        } /* end TDL trial */

        /* ── AWGN 기준선 (단일 HARQ 라운드, RV=0) ───────────────────── */
        int awgn_err = 0, awgn_trials = 0;
        AWGNChannel awgn_ch;
        awgn_init(&awgn_ch, snr);

        for (int trial = 0; trial < cfg->numTrials; trial++) {
            awgn_trials++;

            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded);
            harq_rate_match(coded, N_cb, E, 0, rm_bits);

            int nsym_awgn = E / bps;
            qam_modulate(rm_bits, E, mcs.modulation, data_syms);

            /* AWGN 채널 (y_mrc를 수신 버퍼로 재사용) */
            awgn_add_noise(&awgn_ch, data_syms, nsym_awgn, 0, y_mrc);

            /* Flat noise LLR (AWGN에서 per-SC 보정 불필요) */
            qam_demap_llr(y_mrc, nsym_awgn, mcs.modulation, N0, llr_rx);
            for (int i = nsym_awgn * bps; i < E; i++) llr_rx[i] = 0.0;

            harq_reset(&hbuf);
            harq_combine(&hbuf, llr_rx, E, 0);
            ldpc_decode(&ldpc, harq_get_buf(&hbuf), 25, decoded);

            if (!check_crc(decoded, K, CRC24A)) awgn_err++;
            if (awgn_err >= min_block_errors) break;
        } /* end AWGN trial */

        /* ── 결과 출력 ───────────────────────────────────────────────── */
        printf("%10.2f", snr);
        if (csv) fprintf(csv, "%.2f", snr);

        double bler[4];
        for (int r = 0; r < max_rounds; r++) {
            bler[r] = (double)blk_err[r] / total_trials;
            printf("  %8.4f", bler[r]);
            if (csv) fprintf(csv, ",%.6f", bler[r]);
        }

        double awgn_bler = awgn_trials > 0 ? (double)awgn_err / awgn_trials : 0.0;
        printf("  %8.4f", awgn_bler);
        if (csv) fprintf(csv, ",%.6f", awgn_bler);

        /* Throughput = TBS * (1 - BLER) / num_data_RE  [bits/RE] */
        for (int r = 0; r < max_rounds; r++) {
            double tput = (double)tbsz * (1.0 - bler[r]) / num_data;
            printf("  %8.4f", tput);
            if (csv) fprintf(csv, ",%.6f", tput);
        }
        double tput_awgn = (double)tbsz * (1.0 - awgn_bler) / num_data;
        printf("  %8.4f  %6d\n", tput_awgn, total_trials);
        if (csv) fprintf(csv, ",%.6f,%d\n", tput_awgn, total_trials);

    } /* end SNR */

    printf("\nPDSCH TDL+HARQ+MIMO simulation complete.\n");
    if (csv) { fclose(csv); printf("CSV saved: %s\n", cfg->outputFile); }

    /* ── 메모리 해제 ───────────────────────────────────────────────────── */
    ldpc_free(&ldpc);
    harq_free(&hbuf);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(rm_bits);
    free(data_syms); free(decoded); free(llr_rx);
    free(tx_grid); free(rx_pilots); free(h_pilots);
    free(y_mrc); free(nv_eff);
    for (int m = 0; m < Nrx; m++) {
        free(rx_grid_all[m]); free(h_full_all[m]);
        free(rx_data_all[m]); free(h_data_all[m]);
    }
}
