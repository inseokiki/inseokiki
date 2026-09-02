/* ================================================================
 *  pusch.c
 *  PUSCH (uplink data) simulation -- CP-OFDM/DFT-s-OFDM, TDL, DFE/turbo eq.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "pusch.h"
#include "crc.h"
#include "mcs_table.h"
#include "ldpc.h"
#include "nr_rate_matching.h"
#include "modulation.h"
#include "channel.h"
#include "dmrs.h"
#include "channel_estimation.h"
#include "dft_precode.h"
#include "tdl.h"
#include "mimo.h"
#include "ul_eigen_bf.h"
#include "eigen_16port.h"
#include "utils.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* PUSCH (uplink data channel), SISO + DMRS. Structurally identical to
   run_pdsch_dmrs_simulation() (PHY/src/pdsch.c) -- same DMRS/LS/interp
   channel estimation, ZF/MMSE equalization, LDPC/CRC pipeline. The one
   thing that actually makes this "PUSCH" rather than a renamed PDSCH is
   Transform Precoding (DFT-s-OFDM, TS 38.211 Sec 6.3.1.4): the M data
   symbols are DFT-spread before RE mapping and IDFT-despread after
   equalization, which is what gives uplink its low-PAPR waveform.
   TRANSFORM_PRECODING=0 falls back to plain CP-OFDM (should perform
   identically to PDSCH+DMRS under AWGN/flat fading, since a unitary
   transform doesn't change performance there -- only the transmitted
   waveform's PAPR differs). AWGN/FLAT_FADING channel models only in
   this round; MIMO/HARQ/TDL combinations are a follow-up. */
void run_pusch_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr    = get_code_rate(&mcs);
    int    bps   = mcs.modulationOrder;
    int    crc_bits = 24;

    const uint32_t c_init = 0x87654321u;   /* distinct UL DMRS scrambling seed */
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
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    int use_ray  = (strcmp(cfg->channelModel,"AWGN")!=0);
    int precode  = cfg->transformPrecoding;

    printf("=== PUSCH (Uplink), %s ===\n", precode ? "DFT-s-OFDM" : "CP-OFDM");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("Active SC    : %d\n", active);
    printf("Pilot REs    : %d (DMRS Type 1, 50%% overhead)\n", num_pilots);
    printf("Data REs     : %d\n", num_data);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Transform Precoding : %s\n", precode ? "ON (DFT-s-OFDM)" : "OFF (CP-OFDM)");
    printf("Channel      : %s\n", use_ray ? "Flat Rayleigh fading" : "AWGN (H=1)");
    printf("Equalizer    : %s with LS channel estimation\n", use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb      = (int *)malloc(tbsz  * sizeof(int));
    int *tb_crc  = (int *)malloc(K     * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int *selbits = (int *)malloc(E     * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *precoded   = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *tx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_pilots  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots   = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_full     = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *h_data     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *eq_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *descrambled= (cx_t *)malloc(num_data      * sizeof(cx_t));
    double *allllr   = (double *)malloc(E            * sizeof(double));
    double *soft_buf = (double *)malloc(acsz         * sizeof(double));
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
            nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                       ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                       /*rv=*/0, E, selbits);
            qam_modulate(selbits, E, mcs.modulation, data_syms);

            cx_t *tx_data = data_syms;
            if (precode) {
                dft_precode(data_syms, num_data, precoded);
                tx_data = precoded;
            }

            /* build resource grid */
            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<num_data;d++) tx_grid[data_pos[d]] = tx_data[d];

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
            } else {
                zf_equalize(rx_data, h_data, num_data, eq_data);
            }

            /* IDFT-despread before demapping. This is only exact because
               this round's channel models (AWGN / flat fading) give a
               single h value for the whole grid: mmse_equalize()'s bias
               alpha=|h|^2/(|h|^2+N0) and zf_equalize()'s residual noise
               variance N0/|h|^2 are then identical for every RE, so a
               unitary IDFT preserves the same per-symbol bias/noise
               statistics and the existing scalar demap functions can be
               reused unmodified on the descrambled symbols. This breaks
               under a frequency-selective channel (h varying per RE) --
               TDL+PUSCH is out of scope for this round precisely because
               of this. */
            cx_t *demod_in = eq_data;
            if (precode) {
                idft_precode(eq_data, num_data, descrambled);
                demod_in = descrambled;
            }

            if (use_mmse) {
                qam_demap_llr_mmse(demod_in, num_data, mcs.modulation, h_data, N0, allllr);
            } else {
                double mhp = 0.0;
                for (int d=0;d<num_data;d++) mhp += CX_NORM(h_data[d]);
                mhp /= num_data;
                double env = (mhp > 1e-10) ? N0/mhp : N0;
                qam_demap_llr(demod_in, num_data, mcs.modulation, env, allllr);
            }
            memset(soft_buf, 0, acsz * sizeof(double));
            nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllr);

            ldpc_decode(&ldpc, soft_buf, 25, decoded);
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
    printf("\nPUSCH simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(selbits);
    free(data_syms); free(precoded); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data); free(descrambled);
    free(allllr); free(soft_buf); free(decoded);
}

/* PUSCH over a TDL frequency-selective channel, ZF or MMSE.
 *
 * Both equalizers need care here because a unitary IDFT does not simply
 * "pass through" a per-RE-varying bias/noise profile -- it mixes REs
 * together, so the post-IDFT statistics have to be derived, not assumed.
 *
 * ZF: eq_data[d] = x[d] + noise_d/h_d is already unbiased *exactly* per
 * RE (bias = 1 for every d), so the IDFT sum is clean by linearity: the
 * descrambled noise variance is EXACTLY uniform across all M outputs,
 * equal to N0 * (1/M) * sum_d 1/|h_d|^2.
 *
 * MMSE: eq_data[d] = alpha_d*x[d] + noise'_d is biased, and alpha_d =
 * |h_d|^2/(|h_d|^2+N0) now VARIES across REs under a frequency-selective
 * channel (unlike the flat-channel PUSCH/PDSCH paths elsewhere in this
 * LLS, where alpha is the same for every RE and this problem doesn't
 * arise). Writing y[n] = IDFT(eq_data)[n] and expanding via the DFT
 * definition of X[d] gives, for unit-power iid x[m]:
 *
 *   y[n] = alpha_bar*x[n] + (ISI from alpha_d variation) + (noise)
 *
 * where, using ā = mean_d(alpha_d) and Parseval on the (non-unitary,
 * 1/M-normalized) IDFT of alpha_d:
 *   - signal gain              = ā
 *   - residual ISI variance    = Var_d(alpha_d)      (=0 iff alpha flat)
 *   - residual noise variance  = mean_d(alpha_d*(1-alpha_d))
 *     (noise combines through a unitary IDFT of independent, unequal-
 *     variance terms -> output variance at every index is exactly the
 *     average input variance)
 * De-biasing by dividing by ā gives an unbiased estimate with
 * noise_var = (mean(alpha*(1-alpha)) + Var(alpha)) / ā^2, which reduces
 * exactly to the existing flat-channel formula (1-alpha)/alpha when
 * alpha_d is constant (Var(alpha)=0) -- a useful correctness check. */
void run_pusch_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr    = get_code_rate(&mcs);
    int    bps   = mcs.modulationOrder;
    int    crc_bits = 24;

    const uint32_t c_init = 0x87654321u;
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
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    int precode  = cfg->transformPrecoding;
    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PUSCH over TDL (Frequency-Selective), %s ===\n", precode ? "DFT-s-OFDM" : "CP-OFDM");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Transform Precoding : %s\n", precode ? "ON (DFT-s-OFDM)" : "OFF (CP-OFDM)");
    printf("Channel      : TDL (approx NLOS PDP, DS=%.0fns, %d taps -- not exact TS 38.901 table)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Equalizer    : %s with LS channel estimation\n", use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb      = (int *)malloc(tbsz  * sizeof(int));
    int *tb_crc  = (int *)malloc(K     * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int *selbits = (int *)malloc(E     * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *precoded   = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *tx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_pilots  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots   = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_full     = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *h_data     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *eq_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *descrambled= (cx_t *)malloc(num_data      * sizeof(cx_t));
    double *allllr   = (double *)malloc(E            * sizeof(double));
    double *soft_buf = (double *)malloc(acsz         * sizeof(double));
    int    *decoded  = (int *)malloc(K               * sizeof(int));
    cx_t   *taps     = (cx_t *)malloc(TDL_MAX_TAPS    * sizeof(cx_t));
    double *alpha    = (double *)malloc(num_data      * sizeof(double));

    printf("%12s%15s%15s\n", "SNR (dB)", "BER", "BLER");
    for (int i=0;i<42;i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0 = 1.0 / pow(10.0, snr/10.0);
        int total_err=0, total_bits=0, blk_err=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded);
            nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                       ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                       /*rv=*/0, E, selbits);
            qam_modulate(selbits, E, mcs.modulation, data_syms);

            cx_t *tx_data = data_syms;
            if (precode) {
                dft_precode(data_syms, num_data, precoded);
                tx_data = precoded;
            }

            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<num_data;d++) tx_grid[data_pos[d]] = tx_data[d];

            tdl_draw(&tdl_ch, taps);
            tdl_channel_apply(&tdl_ch, taps, tx_grid, active, rx_grid, NULL);

            for (int p=0;p<num_pilots;p++) rx_pilots[p] = rx_grid[pilot_pos[p]];
            ls_estimate(rx_pilots, dmrs_sym, num_pilots, h_pilots);
            interpolate_channel(h_pilots, num_pilots, pilot_pos, active, h_full);

            for (int d=0;d<num_data;d++) {
                rx_data[d] = rx_grid[data_pos[d]];
                h_data[d]  = h_full[data_pos[d]];
            }

            cx_t *demod_in = eq_data;
            double env;

            if (use_mmse) {
                mmse_equalize(rx_data, h_data, num_data, N0, eq_data, alpha);
                if (precode) {
                    idft_precode(eq_data, num_data, descrambled);
                    /* de-bias by the mean gain, then feed the plain
                       (unbiased) demapper -- see function comment */
                    double abar = 0.0, msq = 0.0, mvar = 0.0;
                    for (int d=0;d<num_data;d++) abar += alpha[d];
                    abar /= num_data;
                    for (int d=0;d<num_data;d++) {
                        msq  += alpha[d]*alpha[d];
                        mvar += alpha[d]*(1.0-alpha[d]);
                    }
                    msq  /= num_data;
                    mvar /= num_data;
                    double var_alpha = msq - abar*abar;
                    if (abar < 1e-6) abar = 1e-6;
                    for (int d=0;d<num_data;d++) descrambled[d] /= abar;
                    demod_in = descrambled;
                    env = (mvar + var_alpha) / (abar*abar);
                } else {
                    qam_demap_llr_mmse(eq_data, num_data, mcs.modulation, h_data, N0, allllr);
                    env = -1.0; /* already demapped below */
                }
            } else {
                zf_equalize(rx_data, h_data, num_data, eq_data);
                if (precode) {
                    idft_precode(eq_data, num_data, descrambled);
                    demod_in = descrambled;
                    /* exact post-IDFT noise variance (see function comment) */
                    double inv_sum = 0.0;
                    for (int d=0;d<num_data;d++) {
                        double hp = CX_NORM(h_data[d]);
                        inv_sum += (hp > 1e-10) ? 1.0/hp : 1.0/1e-10;
                    }
                    env = N0 * inv_sum / num_data;
                } else {
                    double mhp = 0.0;
                    for (int d=0;d<num_data;d++) mhp += CX_NORM(h_data[d]);
                    mhp /= num_data;
                    env = (mhp > 1e-10) ? N0/mhp : N0;
                }
            }
            if (env >= 0.0)
                qam_demap_llr(demod_in, num_data, mcs.modulation, env, allllr);

            memset(soft_buf, 0, acsz * sizeof(double));
            nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllr);

            ldpc_decode(&ldpc, soft_buf, 25, decoded);
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
    printf("\nPUSCH+TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(selbits);
    free(data_syms); free(precoded); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data); free(descrambled);
    free(allllr); free(soft_buf); free(decoded); free(taps); free(alpha);
}

/* PUSCH+TDL, MMSE+DFT-precoding, with an added block decision-feedback
 * equalization (DFE) stage -- explores whether nonlinear equalization can
 * do better than run_pusch_tdl_simulation()'s MMSE path, which treats the
 * residual ISI (see that function's big comment) as extra noise variance
 * instead of cancelling it.
 *
 * Post-IDFT, eq_data[d] = alpha_d*X[d] + n'_d (frequency domain, X=DFT(x))
 * means y = IDFT(eq_data) = g \circledast x + noise'' (circular convolution
 * identity: a diagonal-in-frequency operator is circulant in time), where
 * g = IDFT(alpha_d, d=0..M-1) is the "residual-ISI impulse response" --
 * g[0] = abar (main tap), g[k!=0] are self-interference taps that vanish
 * exactly when alpha is flat (Var(alpha)=0), consistent with the parent
 * function's flat-channel reduction check.
 *
 * DFE (block parallel interference cancellation -- a non-causal variant of
 * classical DFE since circular convolution has no causal order; Proakis &
 * Salehi, Digital Communications 5th Ed., Ch.10.3 Decision Feedback
 * Equalization):
 *   1. First pass: hard-slice the existing (no-DFE) demapper output to get
 *      initial decisions x_hat0[m] for every symbol in the block.
 *   2. Cancel: y_dfe[n] = y[n] - sum_{m!=n} g[(n-m) mod M] * x_hat0[m].
 *   3. Re-demap y_dfe[n]/g[0] with the ISI variance term removed (only the
 *      noise term mean(alpha(1-alpha))/abar^2 remains, since Var(alpha) was
 *      just cancelled) and re-decode.
 * Known risk: if the first-pass decisions are wrong (low SNR), step 2
 * injects errors instead of removing them -- classical DFE error
 * propagation (Proakis & Salehi Ch.10.3). Both paths are evaluated on the
 * *same* channel/noise draw per trial and reported side-by-side (same
 * pattern as run_pdsch_harq_simulation()'s BLER(1st) vs BLER(HARQ) columns)
 * for a fair, paired comparison.
 *
 * Only meaningful for MMSE + TRANSFORM_PRECODING=ON (ZF has no residual ISI
 * to cancel -- see run_pusch_tdl_simulation()'s ZF derivation -- so both are
 * forced on here regardless of EQUALIZER/TRANSFORM_PRECODING config). */
void run_pusch_tdl_dfe_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr    = get_code_rate(&mcs);
    int    bps   = mcs.modulationOrder;
    int    crc_bits = 24;

    const uint32_t c_init = 0x87654321u;
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
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PUSCH over TDL, MMSE + DFT-s-OFDM + Decision Feedback Equalization ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Channel      : TDL (approx NLOS PDP, DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Equalizer    : MMSE (forced -- ZF has no residual ISI to cancel)\n");
    printf("DFE          : block parallel interference cancellation (Proakis & Salehi Ch.10.3)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb      = (int *)malloc(tbsz  * sizeof(int));
    int *tb_crc  = (int *)malloc(K     * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int *selbits = (int *)malloc(E     * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *precoded   = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *tx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_pilots  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots   = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_full     = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *h_data     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *eq_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *y_raw      = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *demod_nodfe= (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *x_hat0     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *y_dfe      = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *alpha_cx   = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *g          = (cx_t *)malloc(num_data      * sizeof(cx_t));
    double *alpha    = (double *)malloc(num_data    * sizeof(double));
    double *allllr   = (double *)malloc(E            * sizeof(double));
    double *soft_buf = (double *)malloc(acsz         * sizeof(double));
    int    *decoded  = (int *)malloc(K               * sizeof(int));
    int    *bits_tmp = (int *)malloc(num_data*bps     * sizeof(int));
    cx_t   *taps     = (cx_t *)malloc(TDL_MAX_TAPS    * sizeof(cx_t));

    printf("%10s%16s%12s%16s%12s\n", "SNR(dB)", "BER(noDFE)", "BLER(noDFE)", "BER(DFE)", "BLER(DFE)");
    for (int i=0;i<66;i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0 = 1.0 / pow(10.0, snr/10.0);
        int err_nodfe=0, bits_nodfe=0, blk_nodfe=0;
        int err_dfe=0,   bits_dfe=0,   blk_dfe=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded);
            nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                       ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                       /*rv=*/0, E, selbits);
            qam_modulate(selbits, E, mcs.modulation, data_syms);

            int nd = num_data;   /* rate matching now provides exactly
                                     num_data*bps bits -- no truncation */
            dft_precode(data_syms, nd, precoded);

            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<nd;d++) tx_grid[data_pos[d]] = precoded[d];

            tdl_draw(&tdl_ch, taps);
            tdl_channel_apply(&tdl_ch, taps, tx_grid, active, rx_grid, NULL);

            for (int p=0;p<num_pilots;p++) rx_pilots[p] = rx_grid[pilot_pos[p]];
            ls_estimate(rx_pilots, dmrs_sym, num_pilots, h_pilots);
            interpolate_channel(h_pilots, num_pilots, pilot_pos, active, h_full);

            for (int d=0;d<num_data;d++) {
                rx_data[d] = rx_grid[data_pos[d]];
                h_data[d]  = h_full[data_pos[d]];
            }

            mmse_equalize(rx_data, h_data, num_data, N0, eq_data, alpha);
            idft_precode(eq_data, nd, y_raw);

            double abar=0.0, msq=0.0, mvar=0.0;
            for (int d=0;d<nd;d++) abar += alpha[d];
            abar /= nd;
            for (int d=0;d<nd;d++) {
                msq  += alpha[d]*alpha[d];
                mvar += alpha[d]*(1.0-alpha[d]);
            }
            msq  /= nd;
            mvar /= nd;
            double var_alpha = msq - abar*abar;
            double abar_safe = (abar < 1e-6) ? 1e-6 : abar;

            /* --- path 1: existing approach (residual ISI folded into noise) --- */
            for (int d=0;d<nd;d++) demod_nodfe[d] = y_raw[d] / abar_safe;
            double env_nodfe = (mvar + var_alpha) / (abar_safe*abar_safe);
            qam_demap_llr(demod_nodfe, nd, mcs.modulation, env_nodfe, allllr);
            memset(soft_buf, 0, acsz * sizeof(double));
            nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllr);
            ldpc_decode(&ldpc, soft_buf, 25, decoded);
            int crc_nodfe = check_crc(decoded, K, CRC24A);
            int ml = tbsz < ldpc.info_size-crc_bits ? tbsz : ldpc.info_size-crc_bits;
            if (ml<0) ml=0;
            int be_nodfe=0;
            for (int i=0;i<ml;i++) if (tb[i]!=decoded[i]) be_nodfe++;
            err_nodfe  += be_nodfe;
            bits_nodfe += tbsz;
            if (!crc_nodfe || be_nodfe > 0) blk_nodfe++;

            /* --- path 2: DFE -- cancel the residual ISI, then re-decode --- */
            for (int d=0;d<nd;d++) alpha_cx[d] = CX_MAKE(alpha[d], 0.0);
            idft_precode(alpha_cx, nd, g);
            double sqrt_nd = sqrt((double)nd);
            for (int d=0;d<nd;d++) g[d] /= sqrt_nd;   /* see function comment: g[0]==abar */

            qam_demodulate(demod_nodfe, nd, mcs.modulation, bits_tmp);
            qam_modulate(bits_tmp, nd*bps, mcs.modulation, x_hat0);

            for (int n=0;n<nd;n++) {
                cx_t isi = CX_ZERO;
                for (int m=0;m<nd;m++) {
                    if (m == n) continue;
                    isi += g[(n - m + nd) % nd] * x_hat0[m];
                }
                y_dfe[n] = (y_raw[n] - isi) / abar_safe;
            }
            double env_dfe = mvar / (abar_safe*abar_safe);
            qam_demap_llr(y_dfe, nd, mcs.modulation, env_dfe, allllr);
            memset(soft_buf, 0, acsz * sizeof(double));
            nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllr);
            ldpc_decode(&ldpc, soft_buf, 25, decoded);
            int crc_dfe = check_crc(decoded, K, CRC24A);
            int be_dfe=0;
            for (int i=0;i<ml;i++) if (tb[i]!=decoded[i]) be_dfe++;
            err_dfe  += be_dfe;
            bits_dfe += tbsz;
            if (!crc_dfe || be_dfe > 0) blk_dfe++;
        }
        double ber_nodfe  = bits_nodfe > 0 ? (double)err_nodfe/bits_nodfe : 0.0;
        double bler_nodfe = (double)blk_nodfe / cfg->numTrials;
        double ber_dfe    = bits_dfe   > 0 ? (double)err_dfe/bits_dfe     : 0.0;
        double bler_dfe   = (double)blk_dfe   / cfg->numTrials;
        printf("%10.1f%16.4e%12.4f%16.4e%12.4f\n",
               snr, ber_nodfe, bler_nodfe, ber_dfe, bler_dfe);
    }
    printf("\nPUSCH+TDL+DFE simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(selbits);
    free(data_syms); free(precoded); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(y_raw); free(demod_nodfe); free(x_hat0); free(y_dfe);
    free(alpha_cx); free(g); free(alpha);
    free(allllr); free(soft_buf); free(decoded); free(bits_tmp); free(taps);
}

/* PUSCH+TDL, MMSE+DFT-precoding, turbo equalization: replaces the hard
 * decision-feedback of run_pusch_tdl_dfe_simulation() with an iterative
 * soft equalizer <-> LDPC decoder exchange (Douillard et al. 1995, "Turbo
 * Equalization"; the MMSE soft-PIC equalizer structure used here follows
 * Tuchler, Koetter & Singer, "Turbo Equalization: Principles and New
 * Results," IEEE Trans. Commun., 2002 -- Proakis & Salehi does not cover
 * turbo equalization in depth, hence the separate citation).
 *
 * Each outer iteration:
 *   1. qam_soft_symbol() turns the current a priori bit LLRs (0 on the
 *      first iteration) into per-symbol soft means E_x[m] and variances
 *      Var_x[m] (generalizes the DFE's hard x_hat0[m] to a soft estimate).
 *   2. Soft-PIC: y_soft[n] = (y_raw[n] - sum_{m!=n} g[(n-m)%M]*E_x[m]) / g[0],
 *      same g=IDFT(alpha) residual-ISI model as the DFE function. Because
 *      m=n is always excluded, the equalizer never uses symbol n's own
 *      prior to judge symbol n -- extrinsic-safe by construction, no
 *      separate bookkeeping needed.
 *   3. The leftover uncertainty in the *other* symbols' soft estimates
 *      leaks into symbol n's effective noise: var_n = mvar/abar^2 +
 *      sum_{m!=n} |g[(n-m)%M]|^2 * Var_x[m] / abar^2. qam_demap_llr() is
 *      called per symbol (n=1 batch) since it only takes one noise_var for
 *      the whole call.
 *   4. This per-symbol LLR is already extrinsic (step 2's exclusion), so it
 *      feeds directly into ldpc_decode_soft() as the channel LLR.
 *   5. New a priori for the next iteration = posterior - what was just fed
 *      in (extrinsic only -- never re-inject a bit's own prior, or the
 *      iteration correlates with itself and the loop's estimates become
 *      overconfident).
 *
 * Caveat (documented limitation, not fixed here): this LLS has no bit
 * interleaver between the LDPC coded bits and the QAM mapper (true of
 * every non-turbo function in this file too), so the ISI-induced
 * correlation and the code's own correlation aren't decorrelated the way
 * classical turbo equalization literature assumes -- observed iteration
 * gains may run below what an interleaved system would show.
 *
 * Reports BLER(noDFE) / BLER(hardDFE) / BLER(turbo) side by side, all
 * computed from the *same* per-trial channel/noise draw, for a fair
 * three-way comparison. */
void run_pusch_tdl_turbo_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr    = get_code_rate(&mcs);
    int    bps   = mcs.modulationOrder;
    int    crc_bits = 24;

    const uint32_t c_init = 0x87654321u;
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
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    double scs_hz = (double)cfg->scsKHz * 1000.0;
    int turbo_iters = cfg->puschTurboIters > 0 ? cfg->puschTurboIters : 3;

    printf("=== PUSCH over TDL, MMSE + DFT-s-OFDM + Turbo Equalization ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Channel      : TDL (approx NLOS PDP, DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Equalizer    : MMSE (forced), soft-PIC + LDPC extrinsic exchange\n");
    printf("Turbo iters  : %d (no bit interleaver -- see function comment)\n", turbo_iters);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tb      = (int *)malloc(tbsz  * sizeof(int));
    int *tb_crc  = (int *)malloc(K     * sizeof(int));
    int *coded   = (int *)malloc(acsz  * sizeof(int));
    int *selbits = (int *)malloc(E     * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *precoded   = (cx_t *)malloc(num_data     * sizeof(cx_t));
    cx_t *tx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_grid    = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_pilots  = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_pilots   = (cx_t *)malloc(num_pilots   * sizeof(cx_t));
    cx_t *h_full     = (cx_t *)malloc(active       * sizeof(cx_t));
    cx_t *rx_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *h_data     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *eq_data    = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *y_raw      = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *demod_nodfe= (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *x_hat0     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *y_dfe      = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *y_soft     = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *alpha_cx   = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *g          = (cx_t *)malloc(num_data      * sizeof(cx_t));
    cx_t *E_x        = (cx_t *)malloc(num_data      * sizeof(cx_t));
    double *Var_x    = (double *)malloc(num_data    * sizeof(double));
    double *apriori  = (double *)malloc(E           * sizeof(double));
    double *posterior= (double *)malloc(acsz        * sizeof(double));
    double *extrinsic= (double *)malloc(acsz        * sizeof(double));
    double *alpha    = (double *)malloc(num_data    * sizeof(double));
    double *allllr   = (double *)malloc(E            * sizeof(double));
    double *soft_buf = (double *)malloc(acsz         * sizeof(double));
    int    *decoded  = (int *)malloc(K               * sizeof(int));
    int    *bits_tmp = (int *)malloc(num_data*bps     * sizeof(int));
    cx_t   *taps     = (cx_t *)malloc(TDL_MAX_TAPS    * sizeof(cx_t));

    printf("%10s%14s%12s%14s%12s%14s%12s\n", "SNR(dB)",
           "BER(noDFE)","BLER(noDFE)","BER(hardDFE)","BLER(hDFE)","BER(turbo)","BLER(turbo)");
    for (int i=0;i<86;i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0 = 1.0 / pow(10.0, snr/10.0);
        int err_nodfe=0, bits_nodfe=0, blk_nodfe=0;
        int err_dfe=0,   bits_dfe=0,   blk_dfe=0;
        int err_turbo=0, bits_turbo=0, blk_turbo=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded);
            nr_ldpc_rate_match_select(coded, acsz, ldpc.bg, ldpc.Zc,
                                       ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                       /*rv=*/0, E, selbits);
            qam_modulate(selbits, E, mcs.modulation, data_syms);

            int nd = num_data;
            dft_precode(data_syms, nd, precoded);

            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<nd;d++) tx_grid[data_pos[d]] = precoded[d];

            tdl_draw(&tdl_ch, taps);
            tdl_channel_apply(&tdl_ch, taps, tx_grid, active, rx_grid, NULL);

            for (int p=0;p<num_pilots;p++) rx_pilots[p] = rx_grid[pilot_pos[p]];
            ls_estimate(rx_pilots, dmrs_sym, num_pilots, h_pilots);
            interpolate_channel(h_pilots, num_pilots, pilot_pos, active, h_full);

            for (int d=0;d<num_data;d++) {
                rx_data[d] = rx_grid[data_pos[d]];
                h_data[d]  = h_full[data_pos[d]];
            }

            mmse_equalize(rx_data, h_data, num_data, N0, eq_data, alpha);
            idft_precode(eq_data, nd, y_raw);

            double abar=0.0, msq=0.0, mvar=0.0;
            for (int d=0;d<nd;d++) abar += alpha[d];
            abar /= nd;
            for (int d=0;d<nd;d++) {
                msq  += alpha[d]*alpha[d];
                mvar += alpha[d]*(1.0-alpha[d]);
            }
            msq  /= nd;
            mvar /= nd;
            double var_alpha = msq - abar*abar;
            double abar_safe = (abar < 1e-6) ? 1e-6 : abar;

            /* --- path 1: no DFE (residual ISI folded into noise) --- */
            for (int d=0;d<nd;d++) demod_nodfe[d] = y_raw[d] / abar_safe;
            double env_nodfe = (mvar + var_alpha) / (abar_safe*abar_safe);
            qam_demap_llr(demod_nodfe, nd, mcs.modulation, env_nodfe, allllr);
            memset(soft_buf, 0, acsz * sizeof(double));
            nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllr);
            ldpc_decode(&ldpc, soft_buf, 25, decoded);
            int crc_nodfe = check_crc(decoded, K, CRC24A);
            int ml = tbsz < ldpc.info_size-crc_bits ? tbsz : ldpc.info_size-crc_bits;
            if (ml<0) ml=0;
            int be_nodfe=0;
            for (int i=0;i<ml;i++) if (tb[i]!=decoded[i]) be_nodfe++;
            err_nodfe  += be_nodfe;
            bits_nodfe += tbsz;
            if (!crc_nodfe || be_nodfe > 0) blk_nodfe++;

            /* --- path 2: hard-decision DFE (block PIC) --- */
            for (int d=0;d<nd;d++) alpha_cx[d] = CX_MAKE(alpha[d], 0.0);
            idft_precode(alpha_cx, nd, g);
            double sqrt_nd = sqrt((double)nd);
            for (int d=0;d<nd;d++) g[d] /= sqrt_nd;   /* g[0] == abar */

            qam_demodulate(demod_nodfe, nd, mcs.modulation, bits_tmp);
            qam_modulate(bits_tmp, nd*bps, mcs.modulation, x_hat0);

            for (int n=0;n<nd;n++) {
                cx_t isi = CX_ZERO;
                for (int m=0;m<nd;m++) {
                    if (m == n) continue;
                    isi += g[(n - m + nd) % nd] * x_hat0[m];
                }
                y_dfe[n] = (y_raw[n] - isi) / abar_safe;
            }
            double env_dfe = mvar / (abar_safe*abar_safe);
            qam_demap_llr(y_dfe, nd, mcs.modulation, env_dfe, allllr);
            memset(soft_buf, 0, acsz * sizeof(double));
            nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllr);
            ldpc_decode(&ldpc, soft_buf, 25, decoded);
            int crc_dfe = check_crc(decoded, K, CRC24A);
            int be_dfe=0;
            for (int i=0;i<ml;i++) if (tb[i]!=decoded[i]) be_dfe++;
            err_dfe  += be_dfe;
            bits_dfe += tbsz;
            if (!crc_dfe || be_dfe > 0) blk_dfe++;

            /* --- path 3: turbo equalization (soft PIC + LDPC extrinsic) ---
             * Correctness check (confirmed empirically, PUSCH_TURBO_ITERS=1
             * reproduces path 1's BER/BLER bit-for-bit): with zero a priori,
             * qam_soft_symbol() gives E_x[m]=0 and Var_x[m]=1 (uniform prior
             * over the unit-power constellation) for every m, so isi=0 (no
             * cancellation) and, by Parseval, sum_{k!=0}|g[k]|^2 = msq -
             * abar^2 = var_alpha regardless of which n it's centered on --
             * so leak/abar^2 reduces to exactly var_alpha/abar^2, making
             * var_n identical to path 1's env_nodfe = (mvar+var_alpha)/abar^2.
             * The first turbo iteration is therefore algebraically the same
             * computation as path 1, not just numerically close. */
            for (int i=0;i<E;i++) apriori[i] = 0.0;
            int crc_turbo = 0; int be_turbo = 0;
            for (int iter=0; iter<turbo_iters; iter++) {
                qam_soft_symbol(apriori, nd, mcs.modulation, E_x, Var_x);

                for (int n=0;n<nd;n++) {
                    cx_t isi = CX_ZERO;
                    double leak = 0.0;
                    for (int m=0;m<nd;m++) {
                        if (m == n) continue;
                        cx_t gm = g[(n - m + nd) % nd];
                        isi  += gm * E_x[m];
                        leak += CX_NORM(gm) * Var_x[m];
                    }
                    y_soft[n] = (y_raw[n] - isi) / abar_safe;
                    double var_n = mvar/(abar_safe*abar_safe) + leak/(abar_safe*abar_safe);
                    qam_demap_llr(&y_soft[n], 1, mcs.modulation, var_n, &allllr[n*bps]);
                }
                memset(soft_buf, 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf, acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, E, allllr);

                ldpc_decode_soft(&ldpc, soft_buf, 25, decoded, posterior);
                crc_turbo = check_crc(decoded, K, CRC24A);
                be_turbo = 0;
                for (int i=0;i<ml;i++) if (tb[i]!=decoded[i]) be_turbo++;

                if (iter < turbo_iters-1) {
                    /* extrinsic over the full acsz domain (posterior minus what
                     * the channel/rate-matching actually contributed this
                     * iteration -- soft_buf is exactly that, 0 at positions
                     * never transmitted), then re-select only the E
                     * transmitted positions as next iteration's a priori
                     * (mirrors nr_ldpc_rate_match_select() but for doubles --
                     * see nr_ldpc_rate_match_select_soft()). */
                    for (int i=0;i<acsz;i++) extrinsic[i] = posterior[i] - soft_buf[i];
                    nr_ldpc_rate_match_select_soft(extrinsic, acsz, ldpc.bg, ldpc.Zc,
                                                    ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                    /*rv=*/0, E, apriori);
                }
            }
            err_turbo  += be_turbo;
            bits_turbo += tbsz;
            if (!crc_turbo || be_turbo > 0) blk_turbo++;
        }
        double ber_nodfe  = bits_nodfe > 0 ? (double)err_nodfe/bits_nodfe : 0.0;
        double bler_nodfe = (double)blk_nodfe / cfg->numTrials;
        double ber_dfe    = bits_dfe   > 0 ? (double)err_dfe/bits_dfe     : 0.0;
        double bler_dfe   = (double)blk_dfe   / cfg->numTrials;
        double ber_turbo  = bits_turbo > 0 ? (double)err_turbo/bits_turbo : 0.0;
        double bler_turbo = (double)blk_turbo / cfg->numTrials;
        printf("%10.1f%14.4e%12.4f%14.4e%12.4f%14.4e%12.4f\n",
               snr, ber_nodfe, bler_nodfe, ber_dfe, bler_dfe, ber_turbo, bler_turbo);
    }
    printf("\nPUSCH+TDL+Turbo simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(selbits);
    free(data_syms); free(precoded); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(y_raw); free(demod_nodfe); free(x_hat0); free(y_dfe); free(y_soft);
    free(alpha_cx); free(g); free(E_x); free(Var_x); free(apriori); free(posterior);
    free(extrinsic); free(alpha);
    free(allllr); free(soft_buf); free(decoded); free(bits_tmp); free(taps);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * PUSCH HARQ Circular Buffer (Chase / IR)
 * 지원 채널: TDL / FLAT_FADING / AWGN
 *
 * TDL/Flat : LS 채널 추정 + 선형 보간 (SISO pilot 간격 2 SC → 충분)
 * AWGN     : h=1 가정 (perfect CSI, 추정 생략)
 * HARQ     : rate_match_select / rate_match_combine, 시도마다 채널 재추첨
 * Precode  : cfg->transformPrecoding에 따라 DFT-s-OFDM 지원
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_harq_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int active   = num_rb * 12;

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;

    const uint32_t c_init = 0x87654321u;
    int num_pilots = 6 * num_rb;
    int num_data   = 6 * num_rb;
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);
    cx_t *dmrs_sym = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    dmrs_sequence(c_init, num_pilots, dmrs_sym);

    int E    = num_data * bps;
    int tbsz = (int)(E * cr);
    if (tbsz < 1)    tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;
    int K = tbsz + crc_bits;

    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);
    int ncb = ldpc.coded_size;

    int is_tdl    = (strcmp(cfg->channelModel, "TDL")         == 0);
    int is_flat   = (strcmp(cfg->channelModel, "FLAT_FADING") == 0);
    int precode   = cfg->transformPrecoding;
    int use_mmse  = (strcmp(cfg->equalizer, "MMSE") == 0);
    int is_chase  = (strcmp(cfg->harqRvSeq, "CHASE") == 0);
    int rvseq[4]  = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx  = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PUSCH (HARQ Circular Buffer %s), %s, %s ===\n",
           is_chase ? "Chase" : "IR",
           is_tdl ? "TDL" : (is_flat ? "Flat Fading" : "AWGN"),
           precode ? "DFT-s-OFDM" : "CP-OFDM");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)K / ncb, ncb, ldpc.bg, ldpc.Zc);
    printf("Num RB       : %d\n", num_rb);
    printf("TB Size      : %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : TDL (DS=%.0fns, %d taps), LS est.\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Equalizer    : %s with LS channel estimation\n", use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int  *tb       = (int  *)malloc(tbsz     * sizeof(int));
    int  *tb_crc   = (int  *)malloc(K        * sizeof(int));
    int  *coded_full=(int  *)malloc(ncb      * sizeof(int));
    int  *selbits  = (int  *)malloc(E        * sizeof(int));
    int  *decoded  = (int  *)malloc(K        * sizeof(int));
    cx_t *data_syms= (cx_t *)malloc(num_data * sizeof(cx_t));
    cx_t *precoded = (cx_t *)malloc(num_data * sizeof(cx_t));
    cx_t *tx_grid  = (cx_t *)malloc(active   * sizeof(cx_t));
    cx_t *rx_grid  = (cx_t *)malloc(active   * sizeof(cx_t));
    cx_t *rx_pilots= (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_pilots = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    cx_t *h_full   = (cx_t *)malloc(active    * sizeof(cx_t));
    cx_t *rx_data  = (cx_t *)malloc(num_data  * sizeof(cx_t));
    cx_t *h_data   = (cx_t *)malloc(num_data  * sizeof(cx_t));
    cx_t *eq_data  = (cx_t *)malloc(num_data  * sizeof(cx_t));
    cx_t *descrambled=(cx_t*)malloc(num_data  * sizeof(cx_t));
    double *allllr = (double *)malloc(E   * sizeof(double));
    double *soft_buf=(double *)malloc(ncb * sizeof(double));
    double *alpha  = (double *)malloc(num_data * sizeof(double));
    cx_t   *taps   = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%10s%14s%14s%14s%12s\n", "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i = 0; i < 64; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    FlatFadingChannel flat_ch;
    AWGNChannel awgn_ch;

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        if (is_tdl)  tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        if (is_flat) flat_fading_init(&flat_ch, snr);
        awgn_init(&awgn_ch, snr);
        double N0 = 1.0 / pow(10.0, snr / 10.0);

        int total_err=0, total_bits=0, blk_err_final=0, blk_err_1st=0;
        long long total_attempts = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {
            gen_random_bits(tb, tbsz);
            attach_crc(tb, tbsz, CRC24A, tb_crc);
            ldpc_encode(&ldpc, tb_crc, coded_full);
            for (int i = 0; i < ncb; i++) soft_buf[i] = 0.0;

            int crc_ok=0, be=0, be_1st=0, crc_1st=0;
            int attempts = 0;

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                nr_ldpc_rate_match_select(coded_full, ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, selbits);
                int nd = num_data;  /* E = nd * bps */
                qam_modulate(selbits, E, mcs.modulation, data_syms);

                cx_t *tx_data = data_syms;
                if (precode) { dft_precode(data_syms, nd, precoded); tx_data = precoded; }

                for (int k = 0; k < active; k++) tx_grid[k] = CX_ZERO;
                for (int p = 0; p < num_pilots; p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
                for (int d = 0; d < nd; d++) tx_grid[data_pos[d]] = tx_data[d];

                /* channel apply */
                if (is_tdl) {
                    tdl_draw(&tdl_ch, taps);
                    tdl_channel_apply(&tdl_ch, taps, tx_grid, active, rx_grid, NULL);
                } else if (is_flat) {
                    cx_t h_true;
                    flat_fading_apply(&flat_ch, tx_grid, active, rx_grid, &h_true);
                } else {
                    /* AWGN: h=1 */
                    awgn_add_noise(&awgn_ch, tx_grid, active, 0, rx_grid);
                }

                /* channel estimation */
                if (!is_flat && !is_tdl) {
                    /* AWGN: perfect h=1 */
                    for (int k = 0; k < active; k++) h_full[k] = 1.0;
                } else {
                    for (int p = 0; p < num_pilots; p++) rx_pilots[p] = rx_grid[pilot_pos[p]];
                    ls_estimate(rx_pilots, dmrs_sym, num_pilots, h_pilots);
                    interpolate_channel(h_pilots, num_pilots, pilot_pos, active, h_full);
                }

                for (int d = 0; d < num_data; d++) {
                    rx_data[d] = rx_grid[data_pos[d]];
                    h_data[d]  = h_full[data_pos[d]];
                }

                /* equalize + LLR */
                double env;
                cx_t *demod_in = eq_data;
                if (use_mmse) {
                    mmse_equalize(rx_data, h_data, num_data, N0, eq_data, alpha);
                    if (precode) {
                        idft_precode(eq_data, nd, descrambled);
                        double abar=0.0, msq=0.0, mvar=0.0;
                        for (int d=0;d<num_data;d++) abar += alpha[d];
                        abar /= num_data;
                        for (int d=0;d<num_data;d++) {
                            msq  += alpha[d]*alpha[d];
                            mvar += alpha[d]*(1.0-alpha[d]);
                        }
                        msq  /= num_data;
                        mvar /= num_data;
                        double var_alpha = msq - abar*abar;
                        if (abar < 1e-6) abar = 1e-6;
                        for (int d=0;d<num_data;d++) descrambled[d] /= abar;
                        demod_in = descrambled;
                        env = (mvar + var_alpha) / (abar*abar);
                    } else {
                        qam_demap_llr_mmse(eq_data, nd, mcs.modulation, h_data, N0, allllr);
                        env = -1.0;
                    }
                } else {
                    zf_equalize(rx_data, h_data, num_data, eq_data);
                    if (precode) {
                        idft_precode(eq_data, nd, descrambled);
                        demod_in = descrambled;
                        double inv_sum = 0.0;
                        for (int d=0;d<num_data;d++) {
                            double hp = CX_NORM(h_data[d]);
                            inv_sum += (hp > 1e-10) ? 1.0/hp : 1.0/1e-10;
                        }
                        env = N0 * inv_sum / num_data;
                    } else {
                        double mhp = 0.0;
                        for (int d=0;d<num_data;d++) mhp += CX_NORM(h_data[d]);
                        mhp /= num_data;
                        env = (mhp > 1e-10) ? N0/mhp : N0;
                    }
                }
                if (env >= 0.0)
                    qam_demap_llr(demod_in, nd, mcs.modulation, env, allllr);

                nr_ldpc_rate_match_combine(soft_buf, ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, allllr);
                ldpc_decode(&ldpc, soft_buf, 25, decoded);
                crc_ok = check_crc(decoded, K, CRC24A);

                int ml = tbsz < ldpc.info_size - crc_bits ? tbsz : ldpc.info_size - crc_bits;
                if (ml < 0) ml = 0;
                be = 0;
                for (int i = 0; i < ml; i++) if (tb[i] != decoded[i]) be++;

                if (attempt == 0) { be_1st = be; crc_1st = crc_ok; }
                attempts = attempt + 1;
                if (crc_ok) break;
            }

            total_err  += be;
            total_bits += tbsz;
            if (!crc_1st || be_1st > 0) blk_err_1st++;
            if (!crc_ok  || be    > 0) blk_err_final++;
            total_attempts += attempts;
        }

        double ber       = total_bits > 0 ? (double)total_err / total_bits : 0.0;
        double bler_1st  = (double)blk_err_1st   / cfg->numTrials;
        double bler_final= (double)blk_err_final  / cfg->numTrials;
        double avg_tx    = (double)total_attempts / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr, ber, bler_1st, bler_final, avg_tx);
    }
    printf("\nPUSCH + HARQ simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded_full); free(selbits); free(decoded);
    free(data_syms); free(precoded); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data); free(descrambled);
    free(allllr); free(soft_buf); free(alpha); free(taps);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL SU-MIMO 2x2 공간다중화(SM_2X2), 평탄 페이딩 — PUSCH의 첫 MIMO 지원
 *
 * 이 프로젝트는 DL(PDSCH)에 SIMO/SM_2X2/SM_4X4/CL_4·8·32PORT/EIGEN_16PORT/
 * MU_MIMO까지 완비돼 있었지만 UL(PUSCH)은 지금까지 전부 단일안테나였다
 * (2026-09-01 완성도 점검에서 발견한 구조적 비대칭, `tasks/todo.md` 참조).
 * 구조는 `run_pdsch_sm2x2_simulation()`(pdsch.c)과 완전히 동일 —
 * `mimo.c`의 검출 함수(`mimo_zf_detect`/`mimo_mmse_detect`)와 채널 드로우
 * (`mimo_channel_draw_2x2`)는 RE 단위 순수 함수라 방향(DL/UL) 무관하게
 * 그대로 재사용된다. UE가 2개 레이어(안테나)로 서로 다른 코드워드를
 * 동시 전송, gNB가 2 Rx 안테나로 수신 — 레이어별 FDM 파일럿으로 채널
 * 추정(LS, 평탄이라 파일럿 REs 전체 평균).
 *
 * TS 38.211 §6.3.1.4: Transform Precoding(DFT-s-OFDM)은 1개 레이어를
 * 초과하는 전송에는 사용할 수 없음 — 따라서 SM_2X2는 CP-OFDM
 * (`TRANSFORM_PRECODING=0`)에서만 유효, `config_parser.c`가 이 조합을
 * 검증(TRANSFORM_PRECODING=1과 함께 쓰면 CFG_ERR). HARQ 조합은 미지원
 * (전용 함수 없음 — `config_parser.c`가 PUSCH+MIMO_MODE≠SISO+HARQ_ENABLE=1
 * 조합도 함께 차단, `tasks/todo.md` 후속 과제).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_sm2x2_simulation(const L1Config *cfg) {
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

    /* 레이어별 파일럿 RE 분리(직교 FDM DMRS) */
    int nppl = num_pilots / 2;
    int *pilotA = (int *)malloc(nppl * sizeof(int));
    int *pilotB = (int *)malloc(nppl * sizeof(int));
    for (int i=0;i<nppl;i++) { pilotA[i] = pilot_pos[2*i]; pilotB[i] = pilot_pos[2*i+1]; }
    cx_t *dmrsA = (cx_t *)malloc(nppl * sizeof(cx_t));
    cx_t *dmrsB = (cx_t *)malloc(nppl * sizeof(cx_t));
    dmrs_sequence(0x87654321u, nppl, dmrsA);
    dmrs_sequence(0x87abcdefu, nppl, dmrsB);

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);

    printf("=== PUSCH 2x2 SU-MIMO Spatial Multiplexing (SM_2X2, Uplink) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Layers    : 2 (독립 코드워드, 공유 데이터 RE)\n");
    printf("gNB Rx Ant   : 2\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Waveform     : CP-OFDM (Transform Precoding은 다중 레이어 미지원, TS 38.211 6.3.1.4)\n");
    printf("Detector     : %s with LS channel estimation (averaged, block-flat)\n",
           use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tbA=(int*)malloc(tbsz*sizeof(int)), *tbB=(int*)malloc(tbsz*sizeof(int));
    int *tb_crcA=(int*)malloc(K*sizeof(int)), *tb_crcB=(int*)malloc(K*sizeof(int));
    int *codedA=(int*)malloc(acsz*sizeof(int)), *codedB=(int*)malloc(acsz*sizeof(int));
    int *selbitsA=(int*)malloc(E*sizeof(int)), *selbitsB=(int*)malloc(E*sizeof(int));
    cx_t *symsA=(cx_t*)malloc(num_data*sizeof(cx_t)), *symsB=(cx_t*)malloc(num_data*sizeof(cx_t));
    cx_t *tx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *tx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *rx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *rx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *x_hatA=(cx_t*)malloc(num_data*sizeof(cx_t)), *x_hatB=(cx_t*)malloc(num_data*sizeof(cx_t));
    double *allllrA=(double*)malloc(E*sizeof(double)), *allllrB=(double*)malloc(E*sizeof(double));
    double *soft_bufA=(double*)malloc(acsz*sizeof(double)), *soft_bufB=(double*)malloc(acsz*sizeof(double));
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
            gen_random_bits(tbA, tbsz); gen_random_bits(tbB, tbsz);
            attach_crc(tbA, tbsz, CRC24A, tb_crcA);
            attach_crc(tbB, tbsz, CRC24A, tb_crcB);
            ldpc_encode(&ldpc, tb_crcA, codedA);
            ldpc_encode(&ldpc, tb_crcB, codedB);
            nr_ldpc_rate_match_select(codedA, acsz, ldpc.bg, ldpc.Zc,
                                       ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                       /*rv=*/0, E, selbitsA);
            nr_ldpc_rate_match_select(codedB, acsz, ldpc.bg, ldpc.Zc,
                                       ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                       /*rv=*/0, E, selbitsB);
            qam_modulate(selbitsA, E, mcs.modulation, symsA);
            qam_modulate(selbitsB, E, mcs.modulation, symsB);

            for (int k=0;k<active;k++) { tx_grid0[k]=CX_ZERO; tx_grid1[k]=CX_ZERO; }
            for (int p=0;p<nppl;p++) { tx_grid0[pilotA[p]] = dmrsA[p]; tx_grid1[pilotB[p]] = dmrsB[p]; }
            for (int d=0;d<num_data;d++) { tx_grid0[data_pos[d]] = symsA[d]; tx_grid1[data_pos[d]] = symsB[d]; }

            cx_t h[2][2];
            mimo_channel_draw_2x2(h);
            for (int k=0;k<active;k++) {
                cx_t tx[2] = { tx_grid0[k], tx_grid1[k] };
                cx_t y[2];
                mimo_channel_apply_2x2(&mch, h, tx, y);
                rx_grid0[k] = y[0]; rx_grid1[k] = y[1];
            }

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
            memset(soft_bufA, 0, acsz*sizeof(double));
            memset(soft_bufB, 0, acsz*sizeof(double));
            nr_ldpc_rate_match_combine(soft_bufA, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllrA);
            nr_ldpc_rate_match_combine(soft_bufB, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllrB);

            ldpc_decode(&ldpc, soft_bufA, 25, decodedA);
            ldpc_decode(&ldpc, soft_bufB, 25, decodedB);
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
    printf("\nPUSCH 2x2 SU-MIMO simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(pilotA); free(pilotB);
    free(dmrsA); free(dmrsB);
    free(tbA); free(tbB); free(tb_crcA); free(tb_crcB);
    free(codedA); free(codedB); free(selbitsA); free(selbitsB);
    free(symsA); free(symsB); free(tx_grid0); free(tx_grid1);
    free(rx_grid0); free(rx_grid1); free(x_hatA); free(x_hatB);
    free(allllrA); free(allllrB); free(soft_bufA); free(soft_bufB);
    free(decodedA); free(decodedB);
}

/* UL SM_2X2, 주파수선택적 TDL — `run_pdsch_sm2x2_tdl_simulation()`과 완전히
 * 동일한 구조(4개 독립 Tx-Rx 안테나쌍 TDL tap-set, RE별 2x2 채널, 안테나쌍당
 * LS+보간 채널추정). 위 flat 버전과 마찬가지로 CP-OFDM 전용, HARQ 미지원 —
 * 상세 근거는 위 flat 버전 헤더 주석 참조. */
void run_pusch_sm2x2_tdl_simulation(const L1Config *cfg) {
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
    dmrs_sequence(0x87654321u, nppl, dmrsA);
    dmrs_sequence(0x87abcdefu, nppl, dmrsB);

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    printf("=== PUSCH 2x2 SU-MIMO Spatial Multiplexing (SM_2X2, Uplink), TDL Frequency-Selective Fading ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Layers    : 2 (독립 코드워드, 공유 데이터 RE)\n");
    printf("gNB Rx Ant   : 2\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Channel      : TDL per Tx-Rx antenna pair (4 independent tap-sets, DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Waveform     : CP-OFDM (Transform Precoding은 다중 레이어 미지원, TS 38.211 6.3.1.4)\n");
    printf("Detector     : %s with per-RE LS channel estimation + interpolation\n",
           use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int *tbA=(int*)malloc(tbsz*sizeof(int)), *tbB=(int*)malloc(tbsz*sizeof(int));
    int *tb_crcA=(int*)malloc(K*sizeof(int)), *tb_crcB=(int*)malloc(K*sizeof(int));
    int *codedA=(int*)malloc(acsz*sizeof(int)), *codedB=(int*)malloc(acsz*sizeof(int));
    int *selbitsA=(int*)malloc(E*sizeof(int)), *selbitsB=(int*)malloc(E*sizeof(int));
    cx_t *symsA=(cx_t*)malloc(num_data*sizeof(cx_t)), *symsB=(cx_t*)malloc(num_data*sizeof(cx_t));
    cx_t *tx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *tx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *rx_grid0=(cx_t*)malloc(active*sizeof(cx_t)), *rx_grid1=(cx_t*)malloc(active*sizeof(cx_t));
    cx_t *x_hatA=(cx_t*)malloc(num_data*sizeof(cx_t)), *x_hatB=(cx_t*)malloc(num_data*sizeof(cx_t));
    double *allllrA=(double*)malloc(E*sizeof(double)), *allllrB=(double*)malloc(E*sizeof(double));
    double *soft_bufA=(double*)malloc(acsz*sizeof(double)), *soft_bufB=(double*)malloc(acsz*sizeof(double));
    int *decodedA=(int*)malloc(K*sizeof(int)), *decodedB=(int*)malloc(K*sizeof(int));

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
            gen_random_bits(tbA, tbsz); gen_random_bits(tbB, tbsz);
            attach_crc(tbA, tbsz, CRC24A, tb_crcA);
            attach_crc(tbB, tbsz, CRC24A, tb_crcB);
            ldpc_encode(&ldpc, tb_crcA, codedA);
            ldpc_encode(&ldpc, tb_crcB, codedB);
            nr_ldpc_rate_match_select(codedA, acsz, ldpc.bg, ldpc.Zc,
                                       ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                       /*rv=*/0, E, selbitsA);
            nr_ldpc_rate_match_select(codedB, acsz, ldpc.bg, ldpc.Zc,
                                       ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                       /*rv=*/0, E, selbitsB);
            qam_modulate(selbitsA, E, mcs.modulation, symsA);
            qam_modulate(selbitsB, E, mcs.modulation, symsB);

            for (int k=0;k<active;k++) { tx_grid0[k]=CX_ZERO; tx_grid1[k]=CX_ZERO; }
            for (int p=0;p<nppl;p++) { tx_grid0[pilotA[p]] = dmrsA[p]; tx_grid1[pilotB[p]] = dmrsB[p]; }
            for (int d=0;d<num_data;d++) { tx_grid0[data_pos[d]] = symsA[d]; tx_grid1[data_pos[d]] = symsB[d]; }

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
            memset(soft_bufA, 0, acsz*sizeof(double));
            memset(soft_bufB, 0, acsz*sizeof(double));
            nr_ldpc_rate_match_combine(soft_bufA, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllrA);
            nr_ldpc_rate_match_combine(soft_bufB, acsz, ldpc.bg, ldpc.Zc,
                                        ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                        /*rv=*/0, E, allllrB);

            ldpc_decode(&ldpc, soft_bufA, 25, decodedA);
            ldpc_decode(&ldpc, soft_bufB, 25, decodedB);
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
    printf("\nPUSCH 2x2 SU-MIMO + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(pilotA); free(pilotB);
    free(dmrsA); free(dmrsB);
    free(tbA); free(tbB); free(tb_crcA); free(tb_crcB);
    free(codedA); free(codedB); free(selbitsA); free(selbitsB);
    free(symsA); free(symsB); free(tx_grid0); free(tx_grid1);
    free(rx_grid0); free(rx_grid1); free(x_hatA); free(x_hatB);
    free(allllrA); free(allllrB); free(soft_bufA); free(soft_bufB);
    free(decodedA); free(decodedB);
    free(rx_pilotsA0); free(rx_pilotsA1); free(rx_pilotsB0); free(rx_pilotsB1);
    free(h_pilots00); free(h_pilots10); free(h_pilots01); free(h_pilots11);
    free(h00_full); free(h10_full); free(h01_full); free(h11_full);
    free(taps00); free(taps10); free(taps01); free(taps11);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL SU-MIMO 2x2 공간다중화(SM_2X2), TDL + HARQ 순환버퍼 IR/Chase
 *
 * `run_pdsch_sm2x2_tdl_harq_simulation()`(pdsch.c)와 완전히 동일한 구조를
 * PUSCH 파이프라인으로 이식 — mother LDPC 코드워드 + 영구 soft-combining
 * 버퍼(레이어별 독립), 4개 독립 Tx-Rx 안테나쌍 TDL tap-set을 attempt마다
 * 재드로우(시간 다이버시티), `rate_matching.c`의 범용 circular buffer
 * 재사용. DL과 마찬가지로 두 레이어가 같은 재전송 occasion을 공유한다고
 * 가정(구현 정의 단순화, 이 프로젝트 다른 다중스트림 HARQ 함수들과 동일).
 * CP-OFDM 전용(TS 38.211 §6.3.1.4, `config_parser.c`가 강제) — DL과
 * 마찬가지로 flat+HARQ 조합은 이번 범위 밖(DL도 TDL만 지원하는 동일한
 * 비대칭이 이미 있었음, `tasks/todo.md` 후속 과제).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_sm2x2_tdl_harq_simulation(const L1Config *cfg) {
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
    dmrs_sequence(0x87654321u, nppl, dmrsA);
    dmrs_sequence(0x87abcdefu, nppl, dmrsB);

    int E = num_data * bps;
    int max_dbits = E;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1) tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);
    int ncb = ldpc.coded_size;

    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    printf("=== PUSCH 2x2 SU-MIMO (HARQ Circular Buffer %s, Uplink), TDL Frequency-Selective Fading ===\n",
           is_chase ? "Chase" : "IR");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)K / ncb, ncb, ldpc.bg, ldpc.Zc);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Layers    : 2 (independent codewords, shared retransmission occasion)\n");
    printf("gNB Rx Ant   : 2\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    printf("Waveform     : CP-OFDM (Transform Precoding은 다중 레이어 미지원, TS 38.211 6.3.1.4)\n");
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
        int total_err=0, total_bits=0, blk_err_final=0, blk_err_1st=0;
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
                nr_ldpc_rate_match_select(coded_fullA, ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, selbitsA);
                nr_ldpc_rate_match_select(coded_fullB, ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, selbitsB);
                qam_modulate(selbitsA, E, mcs.modulation, data_symsA);
                qam_modulate(selbitsB, E, mcs.modulation, data_symsB);

                for (int k=0;k<active;k++) { tx_grid0[k]=CX_ZERO; tx_grid1[k]=CX_ZERO; }
                for (int p=0;p<nppl;p++) { tx_grid0[pilotA[p]] = dmrsA[p]; tx_grid1[pilotB[p]] = dmrsB[p]; }
                for (int d=0;d<num_data;d++) { tx_grid0[data_pos[d]] = data_symsA[d]; tx_grid1[data_pos[d]] = data_symsB[d]; }

                /* 채널: attempt마다 4개 안테나쌍 TDL tap-set 새로 드로우
                 * (HARQ 라운드 간 시간 다이버시티) */
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

                nr_ldpc_rate_match_combine(soft_bufA, ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, allllrA);
                nr_ldpc_rate_match_combine(soft_bufB, ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, allllrB);
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
            if (!crcA_1st || beA_1st>0 || !crcB_1st || beB_1st>0) blk_err_1st++;
            total_attempts += attempts;
        }
        double ber       = total_bits > 0 ? (double)total_err/total_bits : 0.0;
        double bler_1st  = (double)blk_err_1st   / cfg->numTrials;
        double bler_final= (double)blk_err_final / cfg->numTrials;
        double avg_tx    = (double)total_attempts / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr, ber, bler_1st, bler_final, avg_tx);
    }
    printf("\nPUSCH 2x2 SU-MIMO + TDL + HARQ simulation complete.\n");

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

/* ─────────────────────────────────────────────────────────────────────────────
 * UL SIMO 수신 빔포밍 — 공간공분산 EVD(전력반복법) 기반, 비-코드북
 * (Genie MRC vs Eigen-BF 비교), 평탄 페이딩
 *
 * ul_eigen_bf.h 문서 참조. UE 1 Tx 안테나, gNB UL_EIGEN_NRX(=16) Rx —
 * DL EIGEN_16PORT의 Tx측 SVD 빔포밍과 대칭되는 Rx측 비-코드북 접근
 * (사용자 지적: UL Rx는 코드북이 아니라 빔포머를 써야 함). 순간 채널
 * 하나만 놓고 보면 고유빔포밍은 수학적으로 MRC와 동일하므로(UE가 1
 * 안테나뿐이라 채널이 항상 rank-1), 이 함수는 "여러 파일럿 관측(각각
 * 잡음 포함)의 공간공분산에서 뽑은 고유빔(Eigen-BF)"과 "완전한 채널
 * 지식으로 만든 이상적 MRC(Genie)"를 같은 트라이얼·같은 채널 실현에서
 * 병렬 비교한다 — 빔 관리 함수의 Genie/P1 비교와 동일한 철학, 파일럿
 * 개수가 늘수록 Eigen-BF가 Genie MRC에 얼마나 접근하는지 보여준다.
 *
 * 두 경로 모두 단위노름 수신빔 w로 얻은 유효 채널 g=w^H·h(복소 스칼라)
 * 로 등가 스칼라 채널(y=g·x+n, ||w||=1이라 결합 후 잡음분산은 원래
 * N0 그대로)로 단순화 — MU-MIMO/빔관리 함수들과 동일한 관례. ZF
 * 등화(x_hat=y/g)로 검출. Genie는 g_genie=||h||(항상 실수 양수, 완벽한
 * 배열이득), Eigen-BF는 |g_est|≤||h||(코시-슈바르츠, 파일럿 잡음으로
 * 인한 정렬 손실)이 되어 두 경로의 격차가 곧 "공분산 추정에 쓴 파일럿
 * 개수/SNR이 만드는 손실"을 직접 보여준다. TDL/HARQ는 미포함(구현 정의
 * 스코프 축소, tasks/todo.md 후속 과제).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_ul_eigen_bf_simulation(const L1Config *cfg) {
    int num_rb    = cfg->numRB;
    int num_pilots = 6 * num_rb;
    int num_data   = 6 * num_rb;

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
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    cx_t *dmrs = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    dmrs_sequence(0x87ee1901u, num_pilots, dmrs);

    printf("=== PUSCH UL SIMO Rx 빔포밍 (Eigen-BF vs Genie MRC, Uplink) ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Tx Ant    : 1\n");
    printf("gNB Rx Ant   : %d (Eigen-BF: 공간공분산 EVD 전력반복법, 비-코드북)\n", UL_EIGEN_NRX);
    printf("Pilot Obs    : %d (Eigen-BF 공분산 추정용 LS 관측 수)\n", num_pilots);
    printf("TB Size      : %d bits (두 경로 동일 고정 MCS)\n", tbsz);
    printf("Data RE      : %d  (Genie는 완전한 채널지식, Eigen-BF는 파일럿 공분산 기반)\n", num_data);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int    *tb[2], *tb_crc[2], *coded[2], *selbits[2], *dec[2];
    cx_t   *sym[2];
    double *allllr[2], *soft_buf[2];
    cx_t   *rx_hat[2];
    for (int s = 0; s < 2; s++) {
        tb[s]     = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[s] = (int    *)malloc(K     * sizeof(int));
        coded[s]  = (int    *)malloc(acsz  * sizeof(int));
        selbits[s]= (int    *)malloc(E     * sizeof(int));
        sym[s]    = (cx_t  *)malloc(num_data * sizeof(cx_t));
        allllr[s] = (double *)malloc(E     * sizeof(double));
        soft_buf[s]=(double *)malloc(acsz  * sizeof(double));
        dec[s]    = (int    *)malloc(K     * sizeof(int));
        rx_hat[s] = (cx_t  *)malloc(num_data * sizeof(cx_t));
    }
    cx_t (*h_est_obs)[UL_EIGEN_NRX] = (cx_t (*)[UL_EIGEN_NRX])malloc((size_t)num_pilots * sizeof(*h_est_obs));

    printf("%-9s  %-12s %-11s  %-12s %-11s  %s\n",
           "SNR(dB)", "BER_Genie", "BLER_Genie", "BER_EigenBF", "BLER_EigenBF", "GainLoss(dB)");
    for (int i = 0; i < 88; i++) printf("-");
    printf("\n");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[2] = {0}, b_err[2] = {0};
        int  e_blk[2] = {0};
        double gain_loss_db_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 참 채널 드로우(genie), 파일럿 관측으로 Eigen-BF 빔 추정 */
            cx_t h[UL_EIGEN_NRX];
            ul_eigen_channel_draw(h);
            for (int p = 0; p < num_pilots; p++) {
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    cx_t y = h[r] * dmrs[p] + CX_MAKE(randn() * sigma, randn() * sigma);
                    h_est_obs[p][r] = y / dmrs[p];   /* |dmrs|=1이라 노이즈 분산 보존 */
                }
            }
            cx_t w_est[UL_EIGEN_NRX];
            ul_eigen_beamform(h_est_obs, num_pilots, w_est);

            /* ② Genie MRC 빔(완전한 채널지식) */
            double hnorm2 = 0.0;
            for (int r = 0; r < UL_EIGEN_NRX; r++) hnorm2 += CX_NORM(h[r]);
            double hnorm = sqrt(hnorm2);
            cx_t w_genie[UL_EIGEN_NRX];
            for (int r = 0; r < UL_EIGEN_NRX; r++) w_genie[r] = h[r] / hnorm;

            /* ③ 유효 채널(복소 스칼라, g=w^H·h) */
            cx_t g[2] = {CX_ZERO, CX_ZERO};
            for (int r = 0; r < UL_EIGEN_NRX; r++) {
                g[0] += conj(w_genie[r]) * h[r];
                g[1] += conj(w_est[r])   * h[r];
            }
            double gain_loss_db = 20.0 * log10(cabs(g[1]) / cabs(g[0]));
            gain_loss_db_sum += gain_loss_db;

            /* ④ 두 경로(Genie/Eigen-BF) 각각 독립 CW 인코딩 + 검출 + 복호 */
            for (int s = 0; s < 2; s++) {
                gen_random_bits(tb[s], tbsz);
                attach_crc(tb[s], tbsz, CRC24A, tb_crc[s]);
                ldpc_encode(&ldpc, tb_crc[s], coded[s]);
                nr_ldpc_rate_match_select(coded[s], acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, E, selbits[s]);
                qam_modulate(selbits[s], E, mcs.modulation, sym[s]);

                double g_abs2 = CX_NORM(g[s]);
                double nv = (g_abs2 > 1e-12) ? N0 / g_abs2 : N0 * 1e6;
                for (int d = 0; d < num_data; d++) {
                    cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                    cx_t y = g[s] * sym[s][d] + noise;
                    rx_hat[s][d] = (g_abs2 > 1e-12) ? y / g[s] : CX_ZERO;
                }
                qam_demap_llr(rx_hat[s], num_data, mcs.modulation, nv, allllr[s]);
                memset(soft_buf[s], 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf[s], acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, E, allllr[s]);
                ldpc_decode(&ldpc, soft_buf[s], 25, dec[s]);

                int crc_ok = check_crc(dec[s], K, CRC24A);
                int be = 0;
                for (int i = 0; i < tbsz; i++) if (tb[s][i] != dec[s][i]) be++;
                b_err[s]  += be;
                t_bits[s] += tbsz;
                if (!crc_ok || be > 0) e_blk[s]++;
            }
        }   /* end trial loop */

        double ber_genie   = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler_genie  = e_blk[0] / (double)cfg->numTrials;
        double ber_eig     = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler_eig    = e_blk[1] / (double)cfg->numTrials;
        double avg_gain_loss = gain_loss_db_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f  %.2f\n",
               snr, ber_genie, bler_genie, ber_eig, bler_eig, avg_gain_loss);
    }   /* end SNR loop */

    printf("\nPUSCH UL Eigen-BF simulation complete.\n");

    ldpc_free(&ldpc);
    free(dmrs);
    free(h_est_obs);
    for (int s = 0; s < 2; s++) {
        free(tb[s]); free(tb_crc[s]); free(coded[s]); free(selbits[s]);
        free(sym[s]); free(allllr[s]); free(soft_buf[s]); free(dec[s]);
        free(rx_hat[s]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL 2계층(SM_2X2) 수신 빔포밍 — 공간공분산 EVD 기반, 비-코드북
 * (Genie SVD vs Eigen-BF 비교), 평탄 페이딩
 *
 * ul_eigen_bf.h 문서 참조. UE 2 Tx(레이어), gNB UL_EIGEN_NRX(=16) Rx —
 * 위 1-Tx 버전(`run_pusch_ul_eigen_bf_simulation`)의 자연스러운 확장.
 * UE가 2개 레이어로 동시 전송하면 채널 H(16×2)가 더 이상 rank-1이
 * 아니므로 진짜 SVD가 필요해진다.
 *
 * Genie 경로: 완전한 채널 지식으로 H의 좌특이벡터 U(16×2, 직교정규)를
 * 정확히 계산(`ul_eigen_svd_genie()`, 2×2 Gram 행렬 닫힌 형식
 * 고유분해) — U는 H의 신호 부분공간을 정확히 張하므로 16차원 수신신호를
 * U로 2차원에 투영해도 신호 손실이 전혀 없다(에너지가 없는 14차원을
 * 버리는 것과 동치) — 이후 2×2 ZF/MMSE와 결합하면 완전한 채널지식
 * 기준 최적 선형 수신기와 동치.
 *
 * Eigen-BF(현실) 경로: 레이어마다 독립적으로 FDM 파일럿 관측 후
 * `ul_eigen_beamform()`을 레이어별 2회 호출해 얻은 w_A, w_B(각각 잡음
 * 섞인 공분산 기반 추정치)를 `ul_eigen_orthogonalize2()`로 그람-슈미트
 * 직교화해 Q=[w_A,w_B_perp] 구성 — 직교화해야 결합 후 잡음 공분산이
 * 정확히 N0·I가 되어 기존 2×2 검출기 가정이 유지된다.
 *
 * 두 경로 모두 U(또는 Q)를 통해 얻은 2×2 유효 채널(H_eff=U^H·H)로
 * 등가 2차원 수신신호(y_eff=H_eff·x+n, ||열||=1이라 결합 후 잡음분산도
 * N0 그대로)를 합성 — 기존 `mimo_zf_detect`/`mimo_mmse_detect`를
 * 그대로 재사용(신규 검출 코드 없음, 검증된 기존 코드 재사용으로
 * 위험 최소화). 두 레이어는 통계적으로 대칭이라 BER/BLER은 레이어
 * 합산 집계(SM_2X2 관례와 동일).
 *
 * **랭크 적응(2026-09-01 추가)**: Genie 특이값 σ1,σ2(및 N0)로 등력분배
 * 기준 추정 용량 C1=log2(1+σ1²/N0) vs C2=Σlog2(1+(σk²/2)/N0)를 비교해
 * rank 1/2를 트라이얼당 1회 결정 — 실제 네트워크에서도 UL 랭크는
 * gNB가 (SRS 등으로) 결정하므로 Genie 채널지식 기준으로 두는 것이
 * 타당(codebook_32port RI 선택과 동일한 "등력분배" 관례), 두 경로
 * (Genie/Eigen-BF)가 같은 rank를 공유해 트라이얼당 CW 개수가 갈리는
 * 복잡함을 피함. rank=1일 때는 이미 계산된 2×2 유효채널의 열 0(레이어
 * A)만 골라 기존 `mrc_combine()`(2-branch, 이미 검증된 함수)으로 검출
 * — Genie는 U가 H의 열공간을 정확히 張해 무손실이고, Eigen-BF는
 * Q[:,0]=w_A가 직교화로 안 바뀌므로 w_A 단독 사용보다 손해가 없다
 * (오히려 w_B_perp 방향으로 샌 레이어 A 에너지까지 회수해 약간 더 좋음).
 * TDL/HARQ 버전도 동일한 랭크 결정 로직을 재사용.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_ul_eigen_bf_2tx_simulation(const L1Config *cfg) {
    int num_rb    = cfg->numRB;
    int num_data  = 6 * num_rb;
    int nppl      = 3 * num_rb;   /* 레이어당 파일럿 관측 수 (총 6*num_rb를 2등분) */

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;
    int    use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc;
    ldpc_init(&ldpc, K, cr);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    cx_t *dmrsA = (cx_t *)malloc(nppl * sizeof(cx_t));
    cx_t *dmrsB = (cx_t *)malloc(nppl * sizeof(cx_t));
    dmrs_sequence(0x87ee1902u, nppl, dmrsA);
    dmrs_sequence(0x87ee1903u, nppl, dmrsB);

    printf("=== PUSCH UL 2-Layer Rx 빔포밍 (Eigen-BF vs Genie SVD, Uplink), Rank-Adaptive ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Tx Layers : 1~2 (랭크 적응 — Genie 특이값 σ1,σ2 기준 등력분배 추정 용량 비교, 두 경로 공유)\n");
    printf("gNB Rx Ant   : %d (Genie: 정확한 SVD / Eigen-BF: 레이어별 공분산 EVD, 비-코드북)\n", UL_EIGEN_NRX);
    printf("Pilot Obs    : %d/layer (Eigen-BF 공분산 추정용 LS 관측 수)\n", nppl);
    printf("Detector     : %s (rank=2일 때 2x2, rank=1일 때 mrc_combine 2-branch)\n", use_mmse ? "MMSE" : "ZF");
    printf("TB Size/layer: %d bits (두 경로·두 레이어 동일 고정 MCS)\n", tbsz);
    printf("Data RE      : %d\n", num_data);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    /* [path][layer]: path 0=Genie, 1=Eigen-BF; layer 0=A, 1=B(rank=1이면 미사용) */
    int    *tb[2][2], *tb_crc[2][2], *coded[2][2], *selbits[2][2], *dec[2][2];
    cx_t   *sym[2][2];
    double *allllr[2][2], *soft_buf[2][2];
    cx_t   *rx_hat[2][2];
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 2; l++) {
            tb[p][l]     = (int    *)malloc(tbsz  * sizeof(int));
            tb_crc[p][l] = (int    *)malloc(K     * sizeof(int));
            coded[p][l]  = (int    *)malloc(acsz  * sizeof(int));
            selbits[p][l]= (int    *)malloc(E     * sizeof(int));
            sym[p][l]    = (cx_t  *)malloc(num_data * sizeof(cx_t));
            allllr[p][l] = (double *)malloc(E     * sizeof(double));
            soft_buf[p][l]=(double *)malloc(acsz  * sizeof(double));
            dec[p][l]    = (int    *)malloc(K     * sizeof(int));
            rx_hat[p][l] = (cx_t  *)malloc(num_data * sizeof(cx_t));
        }
    }
    cx_t (*h_est_obs)[UL_EIGEN_NRX] = (cx_t (*)[UL_EIGEN_NRX])malloc((size_t)nppl * sizeof(*h_est_obs));

    printf("%-9s  %-12s %-11s  %-12s %-11s  %s\n",
           "SNR(dB)", "BER_Genie", "BLER_Genie", "BER_EigenBF", "BLER_EigenBF", "AvgRank");
    for (int i = 0; i < 78; i++) printf("-");
    printf("\n");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[2] = {0}, b_err[2] = {0};
        int  e_blk[2] = {0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 참 채널 H(16x2) 드로우 */
            cx_t H[UL_EIGEN_NRX][2];
            double inv_sq2 = 1.0 / sqrt(2.0);
            for (int r = 0; r < UL_EIGEN_NRX; r++) {
                H[r][0] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
                H[r][1] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
            }

            /* ② Genie: 정확한 SVD로 U(16x2)와 특이값 계산 */
            cx_t U[UL_EIGEN_NRX][2];
            double svs[2];
            ul_eigen_svd_genie(H, U, svs);

            /* ②-1 랭크 적응: 등력분배 기준 추정 용량으로 rank 1 vs 2 비교
             * (네트워크 측 결정이라 Genie 특이값 기준, 두 경로가 공유 —
             * codebook_32port RI 선택과 동일한 "등력분배" 관례) */
            double cap1 = log2(1.0 + (svs[0]*svs[0]) / N0);
            double cap2 = log2(1.0 + (svs[0]*svs[0]/2.0) / N0)
                        + log2(1.0 + (svs[1]*svs[1]/2.0) / N0);
            int rank = (cap2 > cap1) ? 2 : 1;
            rank_sum += rank;

            /* ③ Eigen-BF: 레이어별 파일럿 관측 -> 공분산 기반 빔 추정 + 직교화
             * (rank=1이어도 두 빔 다 추정 — 실사용 여부만 rank가 결정) */
            for (int m = 0; m < nppl; m++) {
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    cx_t yA = H[r][0] * dmrsA[m] + CX_MAKE(randn() * sigma, randn() * sigma);
                    h_est_obs[m][r] = yA / dmrsA[m];
                }
            }
            cx_t w_A[UL_EIGEN_NRX];
            ul_eigen_beamform(h_est_obs, nppl, w_A);
            for (int m = 0; m < nppl; m++) {
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    cx_t yB = H[r][1] * dmrsB[m] + CX_MAKE(randn() * sigma, randn() * sigma);
                    h_est_obs[m][r] = yB / dmrsB[m];
                }
            }
            cx_t w_B[UL_EIGEN_NRX];
            ul_eigen_beamform(h_est_obs, nppl, w_B);
            ul_eigen_orthogonalize2(w_A, w_B);   /* w_B <- w_B - proj_wA(w_B), 재정규화 */
            cx_t Q[UL_EIGEN_NRX][2];
            for (int r = 0; r < UL_EIGEN_NRX; r++) { Q[r][0] = w_A[r]; Q[r][1] = w_B[r]; }

            /* ④ 2x2 유효 채널: H_eff = combiner^H . H (rank=1이어도 열 0만 씀) */
            cx_t Heff_g[2][2] = {{CX_ZERO,CX_ZERO},{CX_ZERO,CX_ZERO}};
            cx_t Heff_e[2][2] = {{CX_ZERO,CX_ZERO},{CX_ZERO,CX_ZERO}};
            for (int r = 0; r < UL_EIGEN_NRX; r++) {
                Heff_g[0][0] += conj(U[r][0]) * H[r][0]; Heff_g[0][1] += conj(U[r][0]) * H[r][1];
                Heff_g[1][0] += conj(U[r][1]) * H[r][0]; Heff_g[1][1] += conj(U[r][1]) * H[r][1];
                Heff_e[0][0] += conj(Q[r][0]) * H[r][0]; Heff_e[0][1] += conj(Q[r][0]) * H[r][1];
                Heff_e[1][0] += conj(Q[r][1]) * H[r][0]; Heff_e[1][1] += conj(Q[r][1]) * H[r][1];
            }

            /* ⑤ 두 경로 x (rank개) 독립 CW 인코딩 */
            for (int p = 0; p < 2; p++) {
                for (int l = 0; l < rank; l++) {
                    gen_random_bits(tb[p][l], tbsz);
                    attach_crc(tb[p][l], tbsz, CRC24A, tb_crc[p][l]);
                    ldpc_encode(&ldpc, tb_crc[p][l], coded[p][l]);
                    nr_ldpc_rate_match_select(coded[p][l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, E, selbits[p][l]);
                    qam_modulate(selbits[p][l], E, mcs.modulation, sym[p][l]);
                }
            }

            double nv_sum[2][2] = {{0,0},{0,0}};
            for (int d = 0; d < num_data; d++) {
                cx_t noise_g[2] = { CX_MAKE(randn()*sigma, randn()*sigma), CX_MAKE(randn()*sigma, randn()*sigma) };
                cx_t noise_e[2] = { CX_MAKE(randn()*sigma, randn()*sigma), CX_MAKE(randn()*sigma, randn()*sigma) };

                if (rank == 2) {
                    cx_t y_g[2] = {
                        Heff_g[0][0]*sym[0][0][d] + Heff_g[0][1]*sym[0][1][d] + noise_g[0],
                        Heff_g[1][0]*sym[0][0][d] + Heff_g[1][1]*sym[0][1][d] + noise_g[1]
                    };
                    cx_t xh_g[2]; double nv_g[2];
                    if (use_mmse) mimo_mmse_detect(Heff_g, y_g, N0, xh_g, nv_g);
                    else          mimo_zf_detect  (Heff_g, y_g, N0, xh_g, nv_g);
                    rx_hat[0][0][d] = xh_g[0]; rx_hat[0][1][d] = xh_g[1];
                    nv_sum[0][0] += nv_g[0]; nv_sum[0][1] += nv_g[1];

                    cx_t y_e[2] = {
                        Heff_e[0][0]*sym[1][0][d] + Heff_e[0][1]*sym[1][1][d] + noise_e[0],
                        Heff_e[1][0]*sym[1][0][d] + Heff_e[1][1]*sym[1][1][d] + noise_e[1]
                    };
                    cx_t xh_e[2]; double nv_e[2];
                    if (use_mmse) mimo_mmse_detect(Heff_e, y_e, N0, xh_e, nv_e);
                    else          mimo_zf_detect  (Heff_e, y_e, N0, xh_e, nv_e);
                    rx_hat[1][0][d] = xh_e[0]; rx_hat[1][1][d] = xh_e[1];
                    nv_sum[1][0] += nv_e[0]; nv_sum[1][1] += nv_e[1];
                } else {   /* rank == 1: 열 0(layer A)만 사용, 2-branch MRC 재사용
                            * (Genie는 U가 H 열공간을 정확히 张해 무손실, Eigen-BF는
                            * Q[:,0]=w_A가 그대로라 w_A 단독보다 손해 없음 — 위 함수
                            * 헤더 주석 참조) */
                    cx_t hcol_g[2] = { Heff_g[0][0], Heff_g[1][0] };
                    cx_t y_g[2] = { Heff_g[0][0]*sym[0][0][d] + noise_g[0],
                                     Heff_g[1][0]*sym[0][0][d] + noise_g[1] };
                    cx_t xh_g; double nv_g;
                    mrc_combine(hcol_g, y_g, N0, &xh_g, &nv_g);
                    rx_hat[0][0][d] = xh_g;
                    nv_sum[0][0] += nv_g;

                    cx_t hcol_e[2] = { Heff_e[0][0], Heff_e[1][0] };
                    cx_t y_e[2] = { Heff_e[0][0]*sym[1][0][d] + noise_e[0],
                                     Heff_e[1][0]*sym[1][0][d] + noise_e[1] };
                    cx_t xh_e; double nv_e;
                    mrc_combine(hcol_e, y_e, N0, &xh_e, &nv_e);
                    rx_hat[1][0][d] = xh_e;
                    nv_sum[1][0] += nv_e;
                }
            }

            int any_err[2] = {0, 0};
            for (int p = 0; p < 2; p++) {
                for (int l = 0; l < rank; l++) {
                    double env = nv_sum[p][l] / num_data;
                    qam_demap_llr(rx_hat[p][l], num_data, mcs.modulation, env, allllr[p][l]);
                    memset(soft_buf[p][l], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[p][l], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, E, allllr[p][l]);
                    ldpc_decode(&ldpc, soft_buf[p][l], 25, dec[p][l]);

                    int crc_ok = check_crc(dec[p][l], K, CRC24A);
                    int be = 0;
                    for (int i = 0; i < tbsz; i++) if (tb[p][l][i] != dec[p][l][i]) be++;
                    b_err[p]  += be;
                    t_bits[p] += tbsz;
                    if (!crc_ok || be > 0) any_err[p] = 1;
                }
                if (any_err[p]) e_blk[p]++;
            }
        }   /* end trial loop */

        double ber_genie  = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler_genie = e_blk[0] / (double)cfg->numTrials;
        double ber_eig    = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler_eig   = e_blk[1] / (double)cfg->numTrials;
        double avg_rank   = rank_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f  %.2f\n",
               snr, ber_genie, bler_genie, ber_eig, bler_eig, avg_rank);
    }   /* end SNR loop */

    printf("\nPUSCH UL 2-Layer Eigen-BF simulation complete.\n");

    ldpc_free(&ldpc);
    free(dmrsA); free(dmrsB);
    free(h_est_obs);
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 2; l++) {
            free(tb[p][l]); free(tb_crc[p][l]); free(coded[p][l]); free(selbits[p][l]);
            free(sym[p][l]); free(allllr[p][l]); free(soft_buf[p][l]); free(dec[p][l]);
            free(rx_hat[p][l]);
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL SIMO 수신 빔포밍(1-Tx) — Genie MRC vs Eigen-BF, TDL 주파수선택적 페이딩
 *
 * 평탄 버전(`run_pusch_ul_eigen_bf_simulation`) 헤더 참조.
 *
 * **배선 전 검증에서 발견·수정한 설계 결함**: 처음에는 다른
 * massive-MIMO TDL 함수들(SM_2X2/MU-MIMO)처럼 16개 Rx 안테나 각각에
 * 독립 TDL tap-set을 부여했는데, standalone 하네스로 `tdl_freq_response()`
 * 의 RE 간 상관을 직접 찍어보니 파일럿이 걸쳐 있는 대역 폭(120RE)
 * 안에서 이미 크게 감쇠·위상회전(coherence bandwidth 훨씬 못 미침)이
 * 일어남을 확인 — 안테나마다 독립으로 이렇게 감쇠되면 "wideband
 * 공분산으로 뽑은 고유빔"이 수렴할 고정된 공간 방향 자체가 없어져
 * (매 RE 방향이 사실상 무작위) Eigen-BF가 SNR을 올려도 Genie에 전혀
 * 수렴하지 못하는 현상으로 나타났음. 원인: SM_2X2/MU-MIMO의 "안테나쌍
 * 마다 독립 TDL"은 서로 다른 Tx-Rx 링크가 서로 다른 산란체를 겪는
 * "풍부한 다중경로 다이버시티" 시나리오에 맞는 모델이지, "고정된 UE
 * 방향에서 오는 하나의 지배경로가 gNB 배열 전체를 코히런트하게
 * 비추는" 빔포밍 시나리오에는 안 맞음 — 후자는 빔 관리 함수의 TDL
 * 확장과 정확히 같은 물리("배열 전체가 공유하는 클러스터 주파수선택적
 * 게인 × 고정 공간 시그니처")를 써야 함. 그래서 채널 모델을 평탄
 * 버전과 동일한 고정 공간 벡터 h(16, 트라이얼당 1회 드로우)에 단일
 * 공유 SISO TDL 클러스터 게인 g(RE)를 곱하는 형태로 교체(빔 관리
 * TDL 함수와 동일 패턴 재사용) — 이제 파일럿마다 스칼라 게인만 다를 뿐
 * 공간 방향은 h로 항상 고정이라, 파일럿 공분산의 지배적 고유벡터가
 * h 방향으로 정확히 수렴한다(평탄 버전과 동일한 수렴 성질 회복).
 *
 * **Genie 경로**는 완전한 채널지식으로 RE마다 정확한 MRC 수행
 * (g_genie[k]=‖h_re,k‖=|g(k)|·‖h‖). **Eigen-BF 경로**는 파일럿들의
 * 공간공분산에서 뽑은 *하나의 고유빔* w_est(공간 방향은 대역 전체에서
 * 고정이므로 wideband로 뽑는 것이 물리적으로 타당)를 전체 대역에
 * 적용(g_est[k]=w_est^H h_re,k). 실제 DMRS/데이터 RE 그리드
 * (`dmrs_pilot_indices`/`dmrs_data_indices`)를 사용(SM_2X2 UL TDL과
 * 동일 관례).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_ul_eigen_bf_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_pilots = 6 * num_rb;
    int num_data   = 6 * num_rb;
    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);

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
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    cx_t *dmrs = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    dmrs_sequence(0x87ee1904u, num_pilots, dmrs);

    printf("=== PUSCH UL SIMO Rx 빔포밍 (Eigen-BF vs Genie MRC, Uplink), TDL 주파수선택적 페이딩 ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Tx Ant    : 1\n");
    printf("gNB Rx Ant   : %d (Genie: RE별 정확한 MRC / Eigen-BF: 파일럿 공분산 기반 wideband 고유빔 1개)\n", UL_EIGEN_NRX);
    printf("Pilot Obs    : %d (Eigen-BF 공분산 추정용, 공간방향은 고정·게인만 RE별 상이)\n", num_pilots);
    printf("Channel      : 고정 공간벡터 h(16) x 공유 SISO TDL 클러스터 게인, DS=%.0fns, %d taps\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("TB Size      : %d bits (두 경로 동일 고정 MCS)\n", tbsz);
    printf("Data RE      : %d\n", num_data);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int    *tb[2], *tb_crc[2], *coded[2], *selbits[2], *dec[2];
    cx_t   *sym[2];
    double *allllr[2], *soft_buf[2];
    cx_t   *rx_hat[2];
    for (int s = 0; s < 2; s++) {
        tb[s]     = (int    *)malloc(tbsz  * sizeof(int));
        tb_crc[s] = (int    *)malloc(K     * sizeof(int));
        coded[s]  = (int    *)malloc(acsz  * sizeof(int));
        selbits[s]= (int    *)malloc(E     * sizeof(int));
        sym[s]    = (cx_t  *)malloc(num_data * sizeof(cx_t));
        allllr[s] = (double *)malloc(E     * sizeof(double));
        soft_buf[s]=(double *)malloc(acsz  * sizeof(double));
        dec[s]    = (int    *)malloc(K     * sizeof(int));
        rx_hat[s] = (cx_t  *)malloc(num_data * sizeof(cx_t));
    }
    cx_t (*h_est_obs)[UL_EIGEN_NRX] = (cx_t (*)[UL_EIGEN_NRX])malloc((size_t)num_pilots * sizeof(*h_est_obs));
    cx_t *cluster_taps = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%-9s  %-12s %-11s  %-12s %-11s\n",
           "SNR(dB)", "BER_Genie", "BLER_Genie", "BER_EigenBF", "BLER_EigenBF");
    for (int i = 0; i < 62; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[2] = {0}, b_err[2] = {0};
        int  e_blk[2] = {0};

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 고정 공간벡터 h(16, genie) + 공유 SISO TDL 클러스터 게인 */
            cx_t h[UL_EIGEN_NRX];
            ul_eigen_channel_draw(h);
            tdl_draw(&tdl_ch, cluster_taps);

            /* ② 파일럿 RE들(공간방향 고정, 게인만 RE별 상이)로 공분산 기반
             * wideband 빔 추정 */
            for (int p = 0; p < num_pilots; p++) {
                cx_t g_re = tdl_freq_response(&tdl_ch, cluster_taps, pilot_pos[p]);
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    cx_t h_re = h[r] * g_re;
                    cx_t y = h_re * dmrs[p] + CX_MAKE(randn() * sigma, randn() * sigma);
                    h_est_obs[p][r] = y / dmrs[p];
                }
            }
            cx_t w_est[UL_EIGEN_NRX];
            ul_eigen_beamform(h_est_obs, num_pilots, w_est);

            /* ③ 두 경로(Genie/Eigen-BF) 각각 독립 CW 인코딩 */
            for (int s = 0; s < 2; s++) {
                gen_random_bits(tb[s], tbsz);
                attach_crc(tb[s], tbsz, CRC24A, tb_crc[s]);
                ldpc_encode(&ldpc, tb_crc[s], coded[s]);
                nr_ldpc_rate_match_select(coded[s], acsz, ldpc.bg, ldpc.Zc,
                                           ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                           /*rv=*/0, E, selbits[s]);
                qam_modulate(selbits[s], E, mcs.modulation, sym[s]);
            }

            /* ④ RE마다 Genie(정확한 MRC)/Eigen-BF(고정 wideband 빔) 유효채널
             * 계산 후 검출 */
            double nv_sum[2] = {0.0, 0.0};
            for (int d = 0; d < num_data; d++) {
                cx_t g_re = tdl_freq_response(&tdl_ch, cluster_taps, data_pos[d]);
                cx_t h_re[UL_EIGEN_NRX];
                double hnorm2 = 0.0;
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    h_re[r] = h[r] * g_re;
                    hnorm2 += CX_NORM(h_re[r]);
                }
                double hnorm = sqrt(hnorm2);
                cx_t g_genie = CX_MAKE(hnorm, 0.0);   /* MRC 유효채널: 항상 실수 양수 */
                cx_t g_eig = CX_ZERO;
                for (int r = 0; r < UL_EIGEN_NRX; r++) g_eig += conj(w_est[r]) * h_re[r];
                cx_t g[2] = { g_genie, g_eig };

                for (int s = 0; s < 2; s++) {
                    double g_abs2 = CX_NORM(g[s]);
                    double nv = (g_abs2 > 1e-12) ? N0 / g_abs2 : N0 * 1e6;
                    nv_sum[s] += nv;
                    cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                    cx_t y = g[s] * sym[s][d] + noise;
                    rx_hat[s][d] = (g_abs2 > 1e-12) ? y / g[s] : CX_ZERO;
                }
            }

            for (int s = 0; s < 2; s++) {
                double env = nv_sum[s] / num_data;
                qam_demap_llr(rx_hat[s], num_data, mcs.modulation, env, allllr[s]);
                memset(soft_buf[s], 0, acsz * sizeof(double));
                nr_ldpc_rate_match_combine(soft_buf[s], acsz, ldpc.bg, ldpc.Zc,
                                            ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                            /*rv=*/0, E, allllr[s]);
                ldpc_decode(&ldpc, soft_buf[s], 25, dec[s]);

                int crc_ok = check_crc(dec[s], K, CRC24A);
                int be = 0;
                for (int i = 0; i < tbsz; i++) if (tb[s][i] != dec[s][i]) be++;
                b_err[s]  += be;
                t_bits[s] += tbsz;
                if (!crc_ok || be > 0) e_blk[s]++;
            }
        }   /* end trial loop */

        double ber_genie   = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler_genie  = e_blk[0] / (double)cfg->numTrials;
        double ber_eig     = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler_eig    = e_blk[1] / (double)cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f\n",
               snr, ber_genie, bler_genie, ber_eig, bler_eig);
    }   /* end SNR loop */

    printf("\nPUSCH UL Eigen-BF + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs);
    free(h_est_obs);
    free(cluster_taps);
    for (int s = 0; s < 2; s++) {
        free(tb[s]); free(tb_crc[s]); free(coded[s]); free(selbits[s]);
        free(sym[s]); free(allllr[s]); free(soft_buf[s]); free(dec[s]);
        free(rx_hat[s]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL SIMO 수신 빔포밍(1-Tx) — Genie MRC vs Eigen-BF, HARQ 순환버퍼 IR/Chase
 * (평탄 페이딩/TDL 모두 지원)
 *
 * 빔(w_est) 추정은 트라이얼당 1회만 수행 — attempt 0의 채널 상태에서
 * 관측한 파일럿으로 공분산을 추정하고, 이후 모든 attempt에서 그
 * w_est를 그대로 재사용한다(빔 관리 HARQ와 동일 철학: SRS 기반 공분산
 * 추정 주기가 HARQ 재전송 주기보다 훨씬 느리다는 가정, "beam이 낡아도
 * 계속 쓴다"는 현실적 근사). 공간 방향 h(16)는 트라이얼 내내 고정
 * (genie 채널지식은 매 attempt 정확), flat이면 클러스터 게인도 고정
 * (attempt마다 잡음만 재드로우), TDL이면 클러스터 게인을 attempt마다
 * 재드로우(빠른 페이딩의 시간 다이버시티, Genie는 매 attempt 정확한
 * per-RE MRC로 이를 완벽 추적하지만 Eigen-BF는 attempt 0에서 고정한
 * w_est를 계속 쓰므로 이후 attempt의 실제 채널과는 다소 어긋날 수
 * 있음 — 현실적인 "낡은 빔" 시나리오).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_ul_eigen_bf_harq_simulation(const L1Config *cfg) {
    int num_rb    = cfg->numRB;
    int num_pilots = 6 * num_rb;
    int num_data   = 6 * num_rb;

    int is_tdl = (strcmp(cfg->channelModel, "TDL") == 0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int *pilot_pos = (int *)malloc(num_pilots * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);

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

    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);
    int ncb = ldpc.coded_size;

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    cx_t *dmrs = (cx_t *)malloc(num_pilots * sizeof(cx_t));
    dmrs_sequence(0x87ee1905u, num_pilots, dmrs);

    printf("=== PUSCH UL SIMO Rx 빔포밍 (HARQ Circular Buffer %s, Uplink), %s ===\n",
           is_chase ? "Chase" : "IR",
           is_tdl ? "TDL Frequency-Selective Fading" : "Flat Fading");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)K / ncb, ncb, ldpc.bg, ldpc.Zc);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Tx Ant    : 1\n");
    printf("gNB Rx Ant   : %d (Genie: 매 attempt 정확한 MRC / Eigen-BF: attempt 0에서 고정한 wideband 고유빔)\n", UL_EIGEN_NRX);
    printf("TB Size      : %d bits (두 경로 동일 고정 MCS)\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : 고정 공간벡터 h(16) x 공유 SISO TDL 클러스터 게인(attempt마다 재드로우), DS=%.0fns, %d taps\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    else
        printf("Channel      : 고정(빔도 채널도 트라이얼 내내 불변, attempt마다 잡음만 재드로우)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2], *tb_crc[2], *coded_full[2], *selbits[2], *dec[2];
    cx_t  *sym[2], *rx_hat[2];
    double *allllr[2], *soft_buf[2];
    for (int s = 0; s < 2; s++) {
        tb[s]         = (int    *)malloc(tbsz     * sizeof(int));
        tb_crc[s]     = (int    *)malloc(K        * sizeof(int));
        coded_full[s] = (int    *)malloc(ncb      * sizeof(int));
        selbits[s]    = (int    *)malloc(E        * sizeof(int));
        dec[s]        = (int    *)malloc(K        * sizeof(int));
        sym[s]        = (cx_t  *)malloc(num_data  * sizeof(cx_t));
        rx_hat[s]     = (cx_t  *)malloc(num_data  * sizeof(cx_t));
        allllr[s]     = (double *)malloc(E        * sizeof(double));
        soft_buf[s]   = (double *)malloc(ncb      * sizeof(double));
    }
    cx_t (*h_est_obs)[UL_EIGEN_NRX] = (cx_t (*)[UL_EIGEN_NRX])malloc((size_t)num_pilots * sizeof(*h_est_obs));
    cx_t *cluster_taps = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%10s%14s%14s%14s%12s\n",
           "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i = 0; i < 62; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        int total_err_g=0, total_bits_g=0, blk_err_final_g=0, blk_err_1st_g=0;
        int total_err_e=0, total_bits_e=0, blk_err_final_e=0, blk_err_1st_e=0;
        long long total_attempts_g = 0, total_attempts_e = 0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 공간 방향 h(16, genie, 트라이얼 내내 고정) */
            cx_t h[UL_EIGEN_NRX];
            ul_eigen_channel_draw(h);

            /* ② attempt 0 채널 상태에서 파일럿 관측 -> wideband 빔 w_est
             * 추정(이후 모든 attempt에서 재사용) */
            if (is_tdl) tdl_draw(&tdl_ch, cluster_taps);
            for (int p = 0; p < num_pilots; p++) {
                cx_t g_re = is_tdl ? tdl_freq_response(&tdl_ch, cluster_taps, pilot_pos[p]) : CX_MAKE(1.0, 0.0);
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    cx_t h_re = h[r] * g_re;
                    cx_t y = h_re * dmrs[p] + CX_MAKE(randn() * sigma, randn() * sigma);
                    h_est_obs[p][r] = y / dmrs[p];
                }
            }
            cx_t w_est[UL_EIGEN_NRX];
            ul_eigen_beamform(h_est_obs, num_pilots, w_est);

            for (int s = 0; s < 2; s++) {
                gen_random_bits(tb[s], tbsz);
                attach_crc(tb[s], tbsz, CRC24A, tb_crc[s]);
                ldpc_encode(&ldpc, tb_crc[s], coded_full[s]);
                for (int i = 0; i < ncb; i++) soft_buf[s][i] = 0.0;
            }

            int crc_ok[2]={0,0}, be[2]={0,0}, be_1st[2]={0,0}, crc_1st[2]={0,0};
            int attempts = 0;

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int s = 0; s < 2; s++) {
                    nr_ldpc_rate_match_select(coded_full[s], ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, selbits[s]);
                    qam_modulate(selbits[s], E, mcs.modulation, sym[s]);
                }

                /* TDL이면 attempt마다 클러스터 게인 재드로우(시간 다이버시티,
                 * attempt 0의 첫 draw는 위 파일럿 관측과 동일 상태를 재사용) */
                if (is_tdl && attempt > 0) tdl_draw(&tdl_ch, cluster_taps);

                double nv_sum[2] = {0.0, 0.0};
                for (int d = 0; d < num_data; d++) {
                    cx_t g_re = is_tdl ? tdl_freq_response(&tdl_ch, cluster_taps, data_pos[d]) : CX_MAKE(1.0, 0.0);
                    cx_t h_re[UL_EIGEN_NRX];
                    double hnorm2 = 0.0;
                    for (int r = 0; r < UL_EIGEN_NRX; r++) {
                        h_re[r] = h[r] * g_re;
                        hnorm2 += CX_NORM(h_re[r]);
                    }
                    double hnorm = sqrt(hnorm2);
                    cx_t g_genie = CX_MAKE(hnorm, 0.0);
                    cx_t g_eig = CX_ZERO;
                    for (int r = 0; r < UL_EIGEN_NRX; r++) g_eig += conj(w_est[r]) * h_re[r];
                    cx_t g[2] = { g_genie, g_eig };

                    for (int s = 0; s < 2; s++) {
                        double g_abs2 = CX_NORM(g[s]);
                        double nv = (g_abs2 > 1e-12) ? N0 / g_abs2 : N0 * 1e6;
                        nv_sum[s] += nv;
                        cx_t noise = CX_MAKE(randn() * sigma, randn() * sigma);
                        cx_t y = g[s] * sym[s][d] + noise;
                        rx_hat[s][d] = (g_abs2 > 1e-12) ? y / g[s] : CX_ZERO;
                    }
                }

                int ml = tbsz < ldpc.info_size - crc_bits ? tbsz : ldpc.info_size - crc_bits;
                if (ml < 0) ml = 0;
                for (int s = 0; s < 2; s++) {
                    double env = nv_sum[s] / num_data;
                    qam_demap_llr(rx_hat[s], num_data, mcs.modulation, env, allllr[s]);
                    nr_ldpc_rate_match_combine(soft_buf[s], ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, allllr[s]);
                    ldpc_decode(&ldpc, soft_buf[s], 25, dec[s]);
                    crc_ok[s] = check_crc(dec[s], K, CRC24A);
                    int biterr = 0;
                    for (int i = 0; i < ml; i++)
                        if (tb[s][i] != dec[s][i]) biterr++;
                    be[s] = biterr;
                    if (attempt == 0) { be_1st[s] = biterr; crc_1st[s] = crc_ok[s]; }
                }

                attempts = attempt + 1;
                if (crc_ok[0] && crc_ok[1]) break;
            }   /* end attempt loop */

            total_err_g  += be[0]; total_bits_g += tbsz;
            if (!crc_1st[0] || be_1st[0] > 0) blk_err_1st_g++;
            if (!crc_ok[0]  || be[0]    > 0) blk_err_final_g++;
            total_attempts_g += attempts;

            total_err_e  += be[1]; total_bits_e += tbsz;
            if (!crc_1st[1] || be_1st[1] > 0) blk_err_1st_e++;
            if (!crc_ok[1]  || be[1]    > 0) blk_err_final_e++;
            total_attempts_e += attempts;
        }   /* end trial loop */

        printf("--- Genie ---\n");
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr,
               total_bits_g>0 ? (double)total_err_g/total_bits_g : 0.0,
               (double)blk_err_1st_g/cfg->numTrials, (double)blk_err_final_g/cfg->numTrials,
               (double)total_attempts_g/cfg->numTrials);
        printf("--- Eigen-BF ---\n");
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr,
               total_bits_e>0 ? (double)total_err_e/total_bits_e : 0.0,
               (double)blk_err_1st_e/cfg->numTrials, (double)blk_err_final_e/cfg->numTrials,
               (double)total_attempts_e/cfg->numTrials);
    }   /* end SNR loop */

    printf("\nPUSCH UL Eigen-BF + %s + HARQ simulation complete.\n", is_tdl ? "TDL" : "Flat Fading");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs);
    free(h_est_obs); free(cluster_taps);
    for (int s = 0; s < 2; s++) {
        free(tb[s]); free(tb_crc[s]); free(coded_full[s]); free(selbits[s]);
        free(dec[s]); free(sym[s]); free(rx_hat[s]);
        free(allllr[s]); free(soft_buf[s]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL 2계층(SM_2X2) 수신 빔포밍 — Genie SVD vs Eigen-BF, TDL 주파수선택적 페이딩
 *
 * 평탄 버전(`run_pusch_ul_eigen_bf_2tx_simulation`) 헤더 참조. 1-Tx TDL
 * 확장에서 배운 교훈(안테나별 독립 TDL이 아니라 "공유 클러스터 게인 ×
 * 고정 공간 시그니처")을 그대로 적용 — 공간 채널 H(16×2)는 트라이얼당
 * 1회 고정, 모든 RE가 공유하는 SISO 클러스터 게인 g(re) 하나만 곱해진다
 * (H_re = g(re)·H). 이 구조 덕분에 U(Genie 좌특이벡터)와 Q(Eigen-BF
 * 직교화 빔)는 RE와 무관하게 트라이얼당 1회만 계산하면 되고, 2×2 유효
 * 채널도 H_eff(re) = g(re)·(combiner^H·H) — 즉 "고정 2×2 베이스 행렬을
 * 스칼라 g(re)로 스케일"하는 것으로 단순화된다(H_eff의 "형태"는 RE
 * 무관, "크기"만 RE마다 다름).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_ul_eigen_bf_2tx_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int nppl     = 3 * num_rb;
    int *pilot_pos = (int *)malloc((2*nppl) * sizeof(int));   /* num_pilots(=6*num_rb) 전체, 아래서 레이어별로 분리 */
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);
    /* 레이어별 파일럿 RE 분리(직교 FDM DMRS) — SM_2X2 UL 관례 재사용 */
    int *pilotA = (int *)malloc(nppl * sizeof(int));
    int *pilotB = (int *)malloc(nppl * sizeof(int));
    for (int i = 0; i < nppl; i++) { pilotA[i] = pilot_pos[2*i]; pilotB[i] = pilot_pos[2*i+1]; }

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr  = get_code_rate(&mcs);
    int    bps = mcs.modulationOrder;
    int    crc_bits = 24;
    int    use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);

    int max_dbits = num_data * bps;
    int tbsz = (int)(max_dbits * cr);
    if (tbsz < 1)    tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;

    int K = tbsz + crc_bits;
    LDPCCodec ldpc;
    ldpc_init(&ldpc, K, cr);
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    cx_t *dmrsA = (cx_t *)malloc(nppl * sizeof(cx_t));
    cx_t *dmrsB = (cx_t *)malloc(nppl * sizeof(cx_t));
    dmrs_sequence(0x87ee1906u, nppl, dmrsA);
    dmrs_sequence(0x87ee1907u, nppl, dmrsB);

    printf("=== PUSCH UL 2-Layer Rx 빔포밍 (Eigen-BF vs Genie SVD, Uplink), TDL 주파수선택적 페이딩, Rank-Adaptive ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Tx Layers : 1~2 (랭크 적응 — Genie 특이값 σ1,σ2 기준, 두 경로 공유)\n");
    printf("gNB Rx Ant   : %d (Genie: 정확한 SVD / Eigen-BF: 레이어별 공분산 EVD, 비-코드북)\n", UL_EIGEN_NRX);
    printf("Pilot Obs    : %d/layer\n", nppl);
    printf("Channel      : 고정 공간행렬 H(16x2) x 공유 SISO TDL 클러스터 게인, DS=%.0fns, %d taps\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Detector     : %s (rank=2일 때 2x2, rank=1일 때 mrc_combine 2-branch)\n", use_mmse ? "MMSE" : "ZF");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Data RE      : %d\n", num_data);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int    *tb[2][2], *tb_crc[2][2], *coded[2][2], *selbits[2][2], *dec[2][2];
    cx_t   *sym[2][2];
    double *allllr[2][2], *soft_buf[2][2];
    cx_t   *rx_hat[2][2];
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 2; l++) {
            tb[p][l]     = (int    *)malloc(tbsz  * sizeof(int));
            tb_crc[p][l] = (int    *)malloc(K     * sizeof(int));
            coded[p][l]  = (int    *)malloc(acsz  * sizeof(int));
            selbits[p][l]= (int    *)malloc(E     * sizeof(int));
            sym[p][l]    = (cx_t  *)malloc(num_data * sizeof(cx_t));
            allllr[p][l] = (double *)malloc(E     * sizeof(double));
            soft_buf[p][l]=(double *)malloc(acsz  * sizeof(double));
            dec[p][l]    = (int    *)malloc(K     * sizeof(int));
            rx_hat[p][l] = (cx_t  *)malloc(num_data * sizeof(cx_t));
        }
    }
    cx_t (*h_est_obs)[UL_EIGEN_NRX] = (cx_t (*)[UL_EIGEN_NRX])malloc((size_t)nppl * sizeof(*h_est_obs));
    cx_t *cluster_taps = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%-9s  %-12s %-11s  %-12s %-11s  %s\n",
           "SNR(dB)", "BER_Genie", "BLER_Genie", "BER_EigenBF", "BLER_EigenBF", "AvgRank");
    for (int i = 0; i < 78; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[2] = {0}, b_err[2] = {0};
        int  e_blk[2] = {0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 고정 공간행렬 H(16x2, genie) + 공유 SISO TDL 클러스터 게인 */
            cx_t H[UL_EIGEN_NRX][2];
            double inv_sq2 = 1.0 / sqrt(2.0);
            for (int r = 0; r < UL_EIGEN_NRX; r++) {
                H[r][0] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
                H[r][1] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
            }
            tdl_draw(&tdl_ch, cluster_taps);

            /* ② Genie: 정확한 SVD로 U(16x2)와 특이값 계산 (RE 무관, 1회) */
            cx_t U[UL_EIGEN_NRX][2];
            double svs[2];
            ul_eigen_svd_genie(H, U, svs);

            /* ②-1 랭크 적응(공유 클러스터 게인은 스칼라라 rank 결정에
             * 영향 없음 — H의 특이값 기준으로 충분) */
            double cap1 = log2(1.0 + (svs[0]*svs[0]) / N0);
            double cap2 = log2(1.0 + (svs[0]*svs[0]/2.0) / N0)
                        + log2(1.0 + (svs[1]*svs[1]/2.0) / N0);
            int rank = (cap2 > cap1) ? 2 : 1;
            rank_sum += rank;

            /* ③ Eigen-BF: 레이어별 파일럿 관측(게인 g(re) 반영) -> 공분산
             * 기반 빔 추정 + 직교화 */
            for (int m = 0; m < nppl; m++) {
                cx_t g_re = tdl_freq_response(&tdl_ch, cluster_taps, pilotA[m]);
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    cx_t yA = g_re * H[r][0] * dmrsA[m] + CX_MAKE(randn() * sigma, randn() * sigma);
                    h_est_obs[m][r] = yA / dmrsA[m];
                }
            }
            cx_t w_A[UL_EIGEN_NRX];
            ul_eigen_beamform(h_est_obs, nppl, w_A);
            for (int m = 0; m < nppl; m++) {
                cx_t g_re = tdl_freq_response(&tdl_ch, cluster_taps, pilotB[m]);
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    cx_t yB = g_re * H[r][1] * dmrsB[m] + CX_MAKE(randn() * sigma, randn() * sigma);
                    h_est_obs[m][r] = yB / dmrsB[m];
                }
            }
            cx_t w_B[UL_EIGEN_NRX];
            ul_eigen_beamform(h_est_obs, nppl, w_B);
            ul_eigen_orthogonalize2(w_A, w_B);
            cx_t Q[UL_EIGEN_NRX][2];
            for (int r = 0; r < UL_EIGEN_NRX; r++) { Q[r][0] = w_A[r]; Q[r][1] = w_B[r]; }

            /* ④ 2x2 베이스 유효 채널(RE 무관, 1회 계산) — 실제 RE별
             * 유효채널은 이 베이스 행렬에 g(re)만 곱하면 됨 */
            cx_t Heff_g_base[2][2] = {{CX_ZERO,CX_ZERO},{CX_ZERO,CX_ZERO}};
            cx_t Heff_e_base[2][2] = {{CX_ZERO,CX_ZERO},{CX_ZERO,CX_ZERO}};
            for (int r = 0; r < UL_EIGEN_NRX; r++) {
                Heff_g_base[0][0] += conj(U[r][0]) * H[r][0]; Heff_g_base[0][1] += conj(U[r][0]) * H[r][1];
                Heff_g_base[1][0] += conj(U[r][1]) * H[r][0]; Heff_g_base[1][1] += conj(U[r][1]) * H[r][1];
                Heff_e_base[0][0] += conj(Q[r][0]) * H[r][0]; Heff_e_base[0][1] += conj(Q[r][0]) * H[r][1];
                Heff_e_base[1][0] += conj(Q[r][1]) * H[r][0]; Heff_e_base[1][1] += conj(Q[r][1]) * H[r][1];
            }

            /* ⑤ 두 경로 x (rank개) 독립 CW 인코딩 */
            for (int p = 0; p < 2; p++) {
                for (int l = 0; l < rank; l++) {
                    gen_random_bits(tb[p][l], tbsz);
                    attach_crc(tb[p][l], tbsz, CRC24A, tb_crc[p][l]);
                    ldpc_encode(&ldpc, tb_crc[p][l], coded[p][l]);
                    nr_ldpc_rate_match_select(coded[p][l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, E, selbits[p][l]);
                    qam_modulate(selbits[p][l], E, mcs.modulation, sym[p][l]);
                }
            }

            double nv_sum[2][2] = {{0,0},{0,0}};
            for (int d = 0; d < num_data; d++) {
                cx_t g_re = tdl_freq_response(&tdl_ch, cluster_taps, data_pos[d]);
                cx_t Heff_g[2][2] = {
                    { g_re*Heff_g_base[0][0], g_re*Heff_g_base[0][1] },
                    { g_re*Heff_g_base[1][0], g_re*Heff_g_base[1][1] }
                };
                cx_t Heff_e[2][2] = {
                    { g_re*Heff_e_base[0][0], g_re*Heff_e_base[0][1] },
                    { g_re*Heff_e_base[1][0], g_re*Heff_e_base[1][1] }
                };

                cx_t noise_g[2] = { CX_MAKE(randn()*sigma, randn()*sigma), CX_MAKE(randn()*sigma, randn()*sigma) };
                cx_t noise_e[2] = { CX_MAKE(randn()*sigma, randn()*sigma), CX_MAKE(randn()*sigma, randn()*sigma) };

                if (rank == 2) {
                    cx_t y_g[2] = {
                        Heff_g[0][0]*sym[0][0][d] + Heff_g[0][1]*sym[0][1][d] + noise_g[0],
                        Heff_g[1][0]*sym[0][0][d] + Heff_g[1][1]*sym[0][1][d] + noise_g[1]
                    };
                    cx_t xh_g[2]; double nv_g[2];
                    if (use_mmse) mimo_mmse_detect(Heff_g, y_g, N0, xh_g, nv_g);
                    else          mimo_zf_detect  (Heff_g, y_g, N0, xh_g, nv_g);
                    rx_hat[0][0][d] = xh_g[0]; rx_hat[0][1][d] = xh_g[1];
                    nv_sum[0][0] += nv_g[0]; nv_sum[0][1] += nv_g[1];

                    cx_t y_e[2] = {
                        Heff_e[0][0]*sym[1][0][d] + Heff_e[0][1]*sym[1][1][d] + noise_e[0],
                        Heff_e[1][0]*sym[1][0][d] + Heff_e[1][1]*sym[1][1][d] + noise_e[1]
                    };
                    cx_t xh_e[2]; double nv_e[2];
                    if (use_mmse) mimo_mmse_detect(Heff_e, y_e, N0, xh_e, nv_e);
                    else          mimo_zf_detect  (Heff_e, y_e, N0, xh_e, nv_e);
                    rx_hat[1][0][d] = xh_e[0]; rx_hat[1][1][d] = xh_e[1];
                    nv_sum[1][0] += nv_e[0]; nv_sum[1][1] += nv_e[1];
                } else {   /* rank == 1: 열 0(layer A)만, 2-branch MRC 재사용 */
                    cx_t hcol_g[2] = { Heff_g[0][0], Heff_g[1][0] };
                    cx_t y_g[2] = { Heff_g[0][0]*sym[0][0][d] + noise_g[0],
                                     Heff_g[1][0]*sym[0][0][d] + noise_g[1] };
                    cx_t xh_g; double nv_g;
                    mrc_combine(hcol_g, y_g, N0, &xh_g, &nv_g);
                    rx_hat[0][0][d] = xh_g;
                    nv_sum[0][0] += nv_g;

                    cx_t hcol_e[2] = { Heff_e[0][0], Heff_e[1][0] };
                    cx_t y_e[2] = { Heff_e[0][0]*sym[1][0][d] + noise_e[0],
                                     Heff_e[1][0]*sym[1][0][d] + noise_e[1] };
                    cx_t xh_e; double nv_e;
                    mrc_combine(hcol_e, y_e, N0, &xh_e, &nv_e);
                    rx_hat[1][0][d] = xh_e;
                    nv_sum[1][0] += nv_e;
                }
            }

            int any_err[2] = {0, 0};
            for (int p = 0; p < 2; p++) {
                for (int l = 0; l < rank; l++) {
                    double env = nv_sum[p][l] / num_data;
                    qam_demap_llr(rx_hat[p][l], num_data, mcs.modulation, env, allllr[p][l]);
                    memset(soft_buf[p][l], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[p][l], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, E, allllr[p][l]);
                    ldpc_decode(&ldpc, soft_buf[p][l], 25, dec[p][l]);

                    int crc_ok = check_crc(dec[p][l], K, CRC24A);
                    int be = 0;
                    for (int i = 0; i < tbsz; i++) if (tb[p][l][i] != dec[p][l][i]) be++;
                    b_err[p]  += be;
                    t_bits[p] += tbsz;
                    if (!crc_ok || be > 0) any_err[p] = 1;
                }
                if (any_err[p]) e_blk[p]++;
            }
        }   /* end trial loop */

        double ber_genie  = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler_genie = e_blk[0] / (double)cfg->numTrials;
        double ber_eig    = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler_eig   = e_blk[1] / (double)cfg->numTrials;
        double avg_rank   = rank_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f  %.2f\n",
               snr, ber_genie, bler_genie, ber_eig, bler_eig, avg_rank);
    }   /* end SNR loop */

    printf("\nPUSCH UL 2-Layer Eigen-BF + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(pilotA); free(pilotB);
    free(dmrsA); free(dmrsB);
    free(h_est_obs); free(cluster_taps);
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 2; l++) {
            free(tb[p][l]); free(tb_crc[p][l]); free(coded[p][l]); free(selbits[p][l]);
            free(sym[p][l]); free(allllr[p][l]); free(soft_buf[p][l]); free(dec[p][l]);
            free(rx_hat[p][l]);
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL 2계층(SM_2X2) 수신 빔포밍 — Genie SVD vs Eigen-BF, HARQ 순환버퍼 IR/Chase
 * (평탄 페이딩/TDL 모두 지원)
 *
 * TDL 버전 헤더 참조. U(Genie 좌특이벡터)는 H에만 의존하므로(공유
 * 클러스터 게인과 무관) 매 attempt 정확히 유효 — 별도의 "낡음" 없이
 * 매 attempt 실제 게인을 그대로 반영한다. Q(Eigen-BF 직교화 빔)는
 * 1-Tx HARQ와 동일하게 attempt 0의 채널 상태로 딱 1회 추정해 이후 모든
 * attempt에서 재사용(SRS 기반 공분산 추정 주기가 HARQ보다 느리다는
 * 가정) — TDL이면 attempt마다 클러스터 게인만 재드로우(시간
 * 다이버시티), flat이면 attempt마다 잡음만 재드로우.
 *
 * **랭크 적응(2026-09-01)**: 플랫/TDL 버전과 동일한 Genie 특이값 기준
 * 등력분배 추정 용량 비교로 rank 1/2 결정 — 여기서는 트라이얼당 1회
 * (attempt 0 이전)만 결정하고 모든 attempt에서 고정(실제 RI도 HARQ
 * 버스트 도중에 안 바뀌는 것과 동일 관례). rank=1은 열 0(레이어 A)만
 * `mrc_combine()`으로 검출.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_ul_eigen_bf_2tx_harq_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int nppl     = 3 * num_rb;

    int is_tdl = (strcmp(cfg->channelModel, "TDL") == 0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int *pilot_pos = (int *)malloc((2*nppl) * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);
    int *pilotA = (int *)malloc(nppl * sizeof(int));
    int *pilotB = (int *)malloc(nppl * sizeof(int));
    for (int i = 0; i < nppl; i++) { pilotA[i] = pilot_pos[2*i]; pilotB[i] = pilot_pos[2*i+1]; }

    MCSTableType tbl = mcs_table_from_str(cfg->mcsTableType);
    MCSEntry mcs = get_mcs_entry(cfg->mcsIndex, tbl);
    double cr   = get_code_rate(&mcs);
    int    bps  = mcs.modulationOrder;
    int    crc_bits = 24;
    int    use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);

    int E    = num_data * bps;
    int tbsz = (int)(E * cr);
    if (tbsz < 1)    tbsz = 1;
    if (tbsz > 8424) tbsz = 8424;
    int K = tbsz + crc_bits;

    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);
    int ncb = ldpc.coded_size;

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    cx_t *dmrsA = (cx_t *)malloc(nppl * sizeof(cx_t));
    cx_t *dmrsB = (cx_t *)malloc(nppl * sizeof(cx_t));
    dmrs_sequence(0x87ee1908u, nppl, dmrsA);
    dmrs_sequence(0x87ee1909u, nppl, dmrsB);

    printf("=== PUSCH UL 2-Layer Rx 빔포밍 (HARQ Circular Buffer %s, Uplink), %s ===\n",
           is_chase ? "Chase" : "IR",
           is_tdl ? "TDL Frequency-Selective Fading" : "Flat Fading");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)K / ncb, ncb, ldpc.bg, ldpc.Zc);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Tx Layers : 1~2 (랭크 적응, 트라이얼당 1회 결정 후 모든 attempt에서 고정 — RI가 HARQ 버스트 중 안 바뀌는 실제 관례와 동일)\n");
    printf("gNB Rx Ant   : %d (Genie: 매 attempt 정확한 SVD / Eigen-BF: attempt 0에서 고정한 직교화 빔)\n", UL_EIGEN_NRX);
    printf("Detector     : %s (rank=2일 때 2x2, rank=1일 때 mrc_combine 2-branch)\n", use_mmse ? "MMSE" : "ZF");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : 고정 공간행렬 H(16x2) x 공유 SISO TDL 클러스터 게인(attempt마다 재드로우), DS=%.0fns, %d taps\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    else
        printf("Channel      : 고정(빔도 채널도 트라이얼 내내 불변, attempt마다 잡음만 재드로우)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2][2], *tb_crc[2][2], *coded_full[2][2], *selbits[2][2], *dec[2][2];
    cx_t  *sym[2][2], *rx_hat[2][2];
    double *allllr[2][2], *soft_buf[2][2];
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 2; l++) {
            tb[p][l]         = (int    *)malloc(tbsz     * sizeof(int));
            tb_crc[p][l]     = (int    *)malloc(K        * sizeof(int));
            coded_full[p][l] = (int    *)malloc(ncb      * sizeof(int));
            selbits[p][l]    = (int    *)malloc(E        * sizeof(int));
            dec[p][l]        = (int    *)malloc(K        * sizeof(int));
            sym[p][l]        = (cx_t  *)malloc(num_data  * sizeof(cx_t));
            rx_hat[p][l]     = (cx_t  *)malloc(num_data  * sizeof(cx_t));
            allllr[p][l]     = (double *)malloc(E        * sizeof(double));
            soft_buf[p][l]   = (double *)malloc(ncb      * sizeof(double));
        }
    }
    cx_t (*h_est_obs)[UL_EIGEN_NRX] = (cx_t (*)[UL_EIGEN_NRX])malloc((size_t)nppl * sizeof(*h_est_obs));
    cx_t *cluster_taps = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%-9s  %10s%14s%14s%12s%10s\n", "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx", "AvgRank");
    for (int i = 0; i < 72; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        int total_err[2]={0,0}, total_bits[2]={0,0}, blk_err_final[2]={0,0}, blk_err_1st[2]={0,0};
        long long total_attempts[2] = {0, 0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 고정 공간행렬 H(16x2, genie, 트라이얼 내내 고정) */
            cx_t H[UL_EIGEN_NRX][2];
            double inv_sq2 = 1.0 / sqrt(2.0);
            for (int r = 0; r < UL_EIGEN_NRX; r++) {
                H[r][0] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
                H[r][1] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
            }
            cx_t U[UL_EIGEN_NRX][2];
            double svs[2];
            ul_eigen_svd_genie(H, U, svs);   /* H에만 의존 — 매 attempt 그대로 유효 */

            /* ①-1 랭크 적응: 트라이얼당 1회 결정, 모든 attempt에서 고정
             * (실제 RI도 HARQ 버스트 중 안 바뀌는 것과 동일 관례) */
            double cap1 = log2(1.0 + (svs[0]*svs[0]) / N0);
            double cap2 = log2(1.0 + (svs[0]*svs[0]/2.0) / N0)
                        + log2(1.0 + (svs[1]*svs[1]/2.0) / N0);
            int rank = (cap2 > cap1) ? 2 : 1;
            rank_sum += rank;

            /* ② attempt 0 채널 상태에서 레이어별 파일럿 관측 -> Eigen-BF
             * 직교화 빔 Q 추정(이후 모든 attempt에서 재사용) */
            if (is_tdl) tdl_draw(&tdl_ch, cluster_taps);
            for (int m = 0; m < nppl; m++) {
                cx_t g_re = is_tdl ? tdl_freq_response(&tdl_ch, cluster_taps, pilotA[m]) : CX_MAKE(1.0, 0.0);
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    cx_t yA = g_re * H[r][0] * dmrsA[m] + CX_MAKE(randn() * sigma, randn() * sigma);
                    h_est_obs[m][r] = yA / dmrsA[m];
                }
            }
            cx_t w_A[UL_EIGEN_NRX];
            ul_eigen_beamform(h_est_obs, nppl, w_A);
            for (int m = 0; m < nppl; m++) {
                cx_t g_re = is_tdl ? tdl_freq_response(&tdl_ch, cluster_taps, pilotB[m]) : CX_MAKE(1.0, 0.0);
                for (int r = 0; r < UL_EIGEN_NRX; r++) {
                    cx_t yB = g_re * H[r][1] * dmrsB[m] + CX_MAKE(randn() * sigma, randn() * sigma);
                    h_est_obs[m][r] = yB / dmrsB[m];
                }
            }
            cx_t w_B[UL_EIGEN_NRX];
            ul_eigen_beamform(h_est_obs, nppl, w_B);
            ul_eigen_orthogonalize2(w_A, w_B);
            cx_t Q[UL_EIGEN_NRX][2];
            for (int r = 0; r < UL_EIGEN_NRX; r++) { Q[r][0] = w_A[r]; Q[r][1] = w_B[r]; }

            /* ③ 2x2 베이스 유효 채널(RE 무관, 1회) */
            cx_t Heff_g_base[2][2] = {{CX_ZERO,CX_ZERO},{CX_ZERO,CX_ZERO}};
            cx_t Heff_e_base[2][2] = {{CX_ZERO,CX_ZERO},{CX_ZERO,CX_ZERO}};
            for (int r = 0; r < UL_EIGEN_NRX; r++) {
                Heff_g_base[0][0] += conj(U[r][0]) * H[r][0]; Heff_g_base[0][1] += conj(U[r][0]) * H[r][1];
                Heff_g_base[1][0] += conj(U[r][1]) * H[r][0]; Heff_g_base[1][1] += conj(U[r][1]) * H[r][1];
                Heff_e_base[0][0] += conj(Q[r][0]) * H[r][0]; Heff_e_base[0][1] += conj(Q[r][0]) * H[r][1];
                Heff_e_base[1][0] += conj(Q[r][1]) * H[r][0]; Heff_e_base[1][1] += conj(Q[r][1]) * H[r][1];
            }

            for (int p = 0; p < 2; p++) {
                for (int l = 0; l < rank; l++) {
                    gen_random_bits(tb[p][l], tbsz);
                    attach_crc(tb[p][l], tbsz, CRC24A, tb_crc[p][l]);
                    ldpc_encode(&ldpc, tb_crc[p][l], coded_full[p][l]);
                    for (int i = 0; i < ncb; i++) soft_buf[p][l][i] = 0.0;
                }
            }

            int crc_ok[2][2]={{0,0},{0,0}}, be[2][2]={{0,0},{0,0}};
            int be_1st[2][2]={{0,0},{0,0}}, crc_1st[2][2]={{0,0},{0,0}};
            int attempts = 0;

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int p = 0; p < 2; p++)
                    for (int l = 0; l < rank; l++) {
                        nr_ldpc_rate_match_select(coded_full[p][l], ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, selbits[p][l]);
                        qam_modulate(selbits[p][l], E, mcs.modulation, sym[p][l]);
                    }

                /* TDL이면 attempt마다 클러스터 게인 재드로우(attempt 0의
                 * 첫 draw는 위 파일럿 관측과 동일 상태를 재사용) */
                if (is_tdl && attempt > 0) tdl_draw(&tdl_ch, cluster_taps);

                double nv_sum[2][2] = {{0.0,0.0},{0.0,0.0}};
                for (int d = 0; d < num_data; d++) {
                    cx_t g_re = is_tdl ? tdl_freq_response(&tdl_ch, cluster_taps, data_pos[d]) : CX_MAKE(1.0, 0.0);
                    cx_t Heff_g[2][2] = {
                        { g_re*Heff_g_base[0][0], g_re*Heff_g_base[0][1] },
                        { g_re*Heff_g_base[1][0], g_re*Heff_g_base[1][1] }
                    };
                    cx_t Heff_e[2][2] = {
                        { g_re*Heff_e_base[0][0], g_re*Heff_e_base[0][1] },
                        { g_re*Heff_e_base[1][0], g_re*Heff_e_base[1][1] }
                    };

                    cx_t noise_g[2] = { CX_MAKE(randn()*sigma, randn()*sigma), CX_MAKE(randn()*sigma, randn()*sigma) };
                    cx_t noise_e[2] = { CX_MAKE(randn()*sigma, randn()*sigma), CX_MAKE(randn()*sigma, randn()*sigma) };

                    if (rank == 2) {
                        cx_t y_g[2] = {
                            Heff_g[0][0]*sym[0][0][d] + Heff_g[0][1]*sym[0][1][d] + noise_g[0],
                            Heff_g[1][0]*sym[0][0][d] + Heff_g[1][1]*sym[0][1][d] + noise_g[1]
                        };
                        cx_t xh_g[2]; double nv_g[2];
                        if (use_mmse) mimo_mmse_detect(Heff_g, y_g, N0, xh_g, nv_g);
                        else          mimo_zf_detect  (Heff_g, y_g, N0, xh_g, nv_g);
                        rx_hat[0][0][d] = xh_g[0]; rx_hat[0][1][d] = xh_g[1];
                        nv_sum[0][0] += nv_g[0]; nv_sum[0][1] += nv_g[1];

                        cx_t y_e[2] = {
                            Heff_e[0][0]*sym[1][0][d] + Heff_e[0][1]*sym[1][1][d] + noise_e[0],
                            Heff_e[1][0]*sym[1][0][d] + Heff_e[1][1]*sym[1][1][d] + noise_e[1]
                        };
                        cx_t xh_e[2]; double nv_e[2];
                        if (use_mmse) mimo_mmse_detect(Heff_e, y_e, N0, xh_e, nv_e);
                        else          mimo_zf_detect  (Heff_e, y_e, N0, xh_e, nv_e);
                        rx_hat[1][0][d] = xh_e[0]; rx_hat[1][1][d] = xh_e[1];
                        nv_sum[1][0] += nv_e[0]; nv_sum[1][1] += nv_e[1];
                    } else {   /* rank == 1: 열 0(layer A)만, 2-branch MRC 재사용 */
                        cx_t hcol_g[2] = { Heff_g[0][0], Heff_g[1][0] };
                        cx_t y_g[2] = { Heff_g[0][0]*sym[0][0][d] + noise_g[0],
                                         Heff_g[1][0]*sym[0][0][d] + noise_g[1] };
                        cx_t xh_g; double nv_g;
                        mrc_combine(hcol_g, y_g, N0, &xh_g, &nv_g);
                        rx_hat[0][0][d] = xh_g;
                        nv_sum[0][0] += nv_g;

                        cx_t hcol_e[2] = { Heff_e[0][0], Heff_e[1][0] };
                        cx_t y_e[2] = { Heff_e[0][0]*sym[1][0][d] + noise_e[0],
                                         Heff_e[1][0]*sym[1][0][d] + noise_e[1] };
                        cx_t xh_e; double nv_e;
                        mrc_combine(hcol_e, y_e, N0, &xh_e, &nv_e);
                        rx_hat[1][0][d] = xh_e;
                        nv_sum[1][0] += nv_e;
                    }
                }

                int ml = tbsz < ldpc.info_size - crc_bits ? tbsz : ldpc.info_size - crc_bits;
                if (ml < 0) ml = 0;
                for (int p = 0; p < 2; p++) {
                    for (int l = 0; l < rank; l++) {
                        double env = nv_sum[p][l] / num_data;
                        qam_demap_llr(rx_hat[p][l], num_data, mcs.modulation, env, allllr[p][l]);
                        nr_ldpc_rate_match_combine(soft_buf[p][l], ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, allllr[p][l]);
                        ldpc_decode(&ldpc, soft_buf[p][l], 25, dec[p][l]);
                        crc_ok[p][l] = check_crc(dec[p][l], K, CRC24A);
                        int biterr = 0;
                        for (int i = 0; i < ml; i++)
                            if (tb[p][l][i] != dec[p][l][i]) biterr++;
                        be[p][l] = biterr;
                        if (attempt == 0) { be_1st[p][l] = biterr; crc_1st[p][l] = crc_ok[p][l]; }
                    }
                }

                attempts = attempt + 1;
                int all_ok = 1;
                for (int p = 0; p < 2; p++)
                    for (int l = 0; l < rank; l++)
                        if (!crc_ok[p][l]) all_ok = 0;
                if (all_ok) break;
            }   /* end attempt loop */

            for (int p = 0; p < 2; p++) {
                int any_err_1st = 0, any_err_final = 0;
                for (int l = 0; l < rank; l++) {
                    total_err[p]  += be[p][l];
                    total_bits[p] += tbsz;
                    if (!crc_1st[p][l] || be_1st[p][l] > 0) any_err_1st = 1;
                    if (!crc_ok[p][l]  || be[p][l]    > 0) any_err_final = 1;
                }
                if (any_err_1st)   blk_err_1st[p]++;
                if (any_err_final) blk_err_final[p]++;
                total_attempts[p] += attempts;
            }
        }   /* end trial loop */

        double avg_rank = rank_sum / cfg->numTrials;
        printf("--- Genie ---\n");
        printf("%-9.1f  %10.4e%14.4f%14.4f%12.2f%10.2f\n", snr,
               total_bits[0]>0 ? (double)total_err[0]/total_bits[0] : 0.0,
               (double)blk_err_1st[0]/cfg->numTrials, (double)blk_err_final[0]/cfg->numTrials,
               (double)total_attempts[0]/cfg->numTrials, avg_rank);
        printf("--- Eigen-BF ---\n");
        printf("%-9.1f  %10.4e%14.4f%14.4f%12.2f%10.2f\n", snr,
               total_bits[1]>0 ? (double)total_err[1]/total_bits[1] : 0.0,
               (double)blk_err_1st[1]/cfg->numTrials, (double)blk_err_final[1]/cfg->numTrials,
               (double)total_attempts[1]/cfg->numTrials, avg_rank);
    }   /* end SNR loop */

    printf("\nPUSCH UL 2-Layer Eigen-BF + %s + HARQ simulation complete.\n", is_tdl ? "TDL" : "Flat Fading");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(pilotA); free(pilotB);
    free(dmrsA); free(dmrsB);
    free(h_est_obs); free(cluster_taps);
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 2; l++) {
            free(tb[p][l]); free(tb_crc[p][l]); free(coded_full[p][l]); free(selbits[p][l]);
            free(dec[p][l]); free(sym[p][l]); free(rx_hat[p][l]);
            free(allllr[p][l]); free(soft_buf[p][l]);
        }
    }
}

/* 4×rank(1~4) 투영 유효채널 h_eff로 한 RE를 검출 — rank에 따라
 * mrc_combine_4rx/mimo_mmse_detect_4rx2/4rx3/mimo_mmse_detect_4x4로
 * 분기(CL_32PORT의 rank-adaptive 검출기 재사용, 신규 검출 코드 없음).
 * CL_32PORT 선례를 따라 등화기 선택 없이 MMSE 고정(ZF는 rank<4 사각
 * 시스템에 대해 이 프로젝트에 미구현 — mrc_combine_4rx는 단일 스트림이라
 * ZF/MMSE/MRC가 모두 동치이므로 rank=1에서는 등화기 선택이 무의미).
 * flat/TDL/HARQ 세 함수가 공유하는 UL Eigen-BF 4-Tx 전용 헬퍼. */
static void ul_eigen4_detect_re(cx_t h_eff[4][4], int rank, const cx_t sym_l[4],
                                 const cx_t noise[4], double N0,
                                 cx_t xh_out[4], double nv_out[4]) {
    cx_t y[4];
    for (int r = 0; r < 4; r++) {
        cx_t s = noise[r];
        for (int l = 0; l < rank; l++) s += h_eff[r][l] * sym_l[l];
        y[r] = s;
    }
    if (rank == 1) {
        cx_t hcol[4] = { h_eff[0][0], h_eff[1][0], h_eff[2][0], h_eff[3][0] };
        cx_t xh; double nv;
        mrc_combine_4rx(hcol, y, N0, &xh, &nv);
        xh_out[0] = xh; nv_out[0] = nv;
    } else if (rank == 2) {
        cx_t he2[4][2];
        for (int r = 0; r < 4; r++) { he2[r][0] = h_eff[r][0]; he2[r][1] = h_eff[r][1]; }
        cx_t xh[2]; double nv[2];
        mimo_mmse_detect_4rx2(he2, y, N0, xh, nv);
        xh_out[0] = xh[0]; xh_out[1] = xh[1];
        nv_out[0] = nv[0]; nv_out[1] = nv[1];
    } else if (rank == 3) {
        cx_t he3[4][3];
        for (int r = 0; r < 4; r++) { he3[r][0] = h_eff[r][0]; he3[r][1] = h_eff[r][1]; he3[r][2] = h_eff[r][2]; }
        cx_t xh[3]; double nv[3];
        mimo_mmse_detect_4rx3(he3, y, N0, xh, nv);
        for (int l = 0; l < 3; l++) { xh_out[l] = xh[l]; nv_out[l] = nv[l]; }
    } else {   /* rank == 4 */
        cx_t xh[4]; double nv[4];
        mimo_mmse_detect_4x4(h_eff, y, N0, xh, nv);
        for (int l = 0; l < 4; l++) { xh_out[l] = xh[l]; nv_out[l] = nv[l]; }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL 최대 4계층 수신 빔포밍 — 공간공분산 EVD 기반, 비-코드북 (Genie SVD
 * vs Eigen-BF 비교), 평탄 페이딩, 랭크 적응(1~4)
 *
 * ul_eigen_bf.h 문서 참조("UE K Tx(레이어, K≤4) 확장" 절). 2-Tx 버전
 * (`run_pusch_ul_eigen_bf_2tx_simulation`)을 K=4로 일반화한 것 —
 * 2×2 Gram 행렬은 닫힌 형식으로 풀렸지만 4×4는 반복법이 필요해 DL
 * EIGEN_16PORT가 이미 검증한 Cyclic Jacobi 고유분해(`herm4x4_eig()`,
 * utils.h로 추출해 두 모듈이 공유)를 그대로 재사용한다.
 *
 * Genie 경로: `ul_eigen_svd_genie4()`가 완전한 채널 지식으로 H(16×4)의
 * 좌특이벡터 U(16×4, 직교정규)와 특이값 σ0≥σ1≥σ2≥σ3을 계산.
 * Eigen-BF(현실) 경로: 레이어마다 독립 FDM 파일럿 관측 →
 * `ul_eigen_beamform()` 4회 호출 → `ul_eigen_orthogonalize4()`로 순차
 * 그람-슈미트 직교화.
 *
 * 랭크 적응: DL EIGEN_16PORT의 `eigen_bf_16port_select_rank()`를 그대로
 * 재사용(등력분배 기준 추정 용량 비교, r=1..4) — 2-Tx의 수동 cap1/cap2
 * 비교를 일반화한 것과 동일 원리, 인터페이스가 이미 (sigma[4],N0,*rank)라
 * 새 함수가 필요 없다. 두 경로(Genie/Eigen-BF)가 rank를 공유(2-Tx와
 * 동일한 이유 — 실제 UL 랭크는 gNB가 결정).
 *
 * 검출: 4×4 투영 유효채널(U^H·H 또는 Q^H·H, rank<4여도 항상 4×4 전체
 * 계산)에서 활성 rank개 열만 사용 — `ul_eigen4_detect_re()`가 CL_32PORT와
 * 동일한 검출기 계열을 재사용(신규 검출 코드 없음).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_ul_eigen_bf_4tx_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int nppl     = (6 * num_rb) / 4;   /* 레이어당 파일럿 관측 수 (총 6*num_rb를 4등분, 정수 나눗셈) */
    if (nppl < 1) nppl = 1;

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
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    static const unsigned int dmrs_cinit[4] = {0x87ee1920u, 0x87ee1921u, 0x87ee1922u, 0x87ee1923u};
    cx_t *dmrs[4];
    for (int l = 0; l < 4; l++) {
        dmrs[l] = (cx_t *)malloc(nppl * sizeof(cx_t));
        dmrs_sequence(dmrs_cinit[l], nppl, dmrs[l]);
    }

    printf("=== PUSCH UL 4-Layer(최대) Rx 빔포밍 (Eigen-BF vs Genie SVD, Uplink), Rank-Adaptive ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Tx Layers : 1~4 (랭크 적응 — eigen_bf_16port_select_rank(), 등력분배 추정 용량, 두 경로 공유)\n");
    printf("gNB Rx Ant   : %d (Genie: 정확한 4x4 SVD / Eigen-BF: 레이어별 공분산 EVD, 비-코드북)\n", UL_EIGEN_NRX);
    printf("Pilot Obs    : %d/layer (Eigen-BF 공분산 추정용 LS 관측 수)\n", nppl);
    printf("Detector     : MMSE 고정 (rank1=mrc_combine_4rx, rank2=4rx2, rank3=4rx3, rank4=4x4 — CL_32PORT와 동일 계열)\n");
    printf("TB Size/layer: %d bits (두 경로·모든 활성 레이어 동일 고정 MCS)\n", tbsz);
    printf("Data RE      : %d\n", num_data);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int    *tb[2][4], *tb_crc[2][4], *coded[2][4], *selbits[2][4], *dec[2][4];
    cx_t   *sym[2][4];
    double *allllr[2][4], *soft_buf[2][4];
    cx_t   *rx_hat[2][4];
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 4; l++) {
            tb[p][l]     = (int    *)malloc(tbsz  * sizeof(int));
            tb_crc[p][l] = (int    *)malloc(K     * sizeof(int));
            coded[p][l]  = (int    *)malloc(acsz  * sizeof(int));
            selbits[p][l]= (int    *)malloc(E     * sizeof(int));
            sym[p][l]    = (cx_t  *)malloc(num_data * sizeof(cx_t));
            allllr[p][l] = (double *)malloc(E     * sizeof(double));
            soft_buf[p][l]=(double *)malloc(acsz  * sizeof(double));
            dec[p][l]    = (int    *)malloc(K     * sizeof(int));
            rx_hat[p][l] = (cx_t  *)malloc(num_data * sizeof(cx_t));
        }
    }
    cx_t (*h_est_obs)[UL_EIGEN_NRX] = (cx_t (*)[UL_EIGEN_NRX])malloc((size_t)nppl * sizeof(*h_est_obs));

    printf("%-9s  %-12s %-11s  %-12s %-11s  %s\n",
           "SNR(dB)", "BER_Genie", "BLER_Genie", "BER_EigenBF", "BLER_EigenBF", "AvgRank");
    for (int i = 0; i < 78; i++) printf("-");
    printf("\n");

    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[2] = {0}, b_err[2] = {0};
        int  e_blk[2] = {0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 참 채널 H(16x4) 드로우 */
            cx_t H[UL_EIGEN_NRX][4];
            double inv_sq2 = 1.0 / sqrt(2.0);
            for (int r = 0; r < UL_EIGEN_NRX; r++)
                for (int c = 0; c < 4; c++)
                    H[r][c] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);

            /* ② Genie: 4x4 SVD로 U(16x4)와 특이값 계산 */
            cx_t U[UL_EIGEN_NRX][4];
            double svs[4];
            ul_eigen_svd_genie4(H, U, svs);

            /* ②-1 랭크 적응: DL EIGEN_16PORT의 등력분배 용량 비교 재사용 */
            int rank;
            eigen_bf_16port_select_rank(svs, N0, &rank);
            rank_sum += rank;

            /* ③ Eigen-BF: 레이어별 파일럿 관측 -> 공분산 기반 빔 추정 + 순차 직교화
             * (rank와 무관하게 4개 빔 모두 추정 — 실사용 여부만 rank가 결정) */
            cx_t w[4][UL_EIGEN_NRX];
            for (int l = 0; l < 4; l++) {
                for (int m = 0; m < nppl; m++) {
                    for (int r = 0; r < UL_EIGEN_NRX; r++) {
                        cx_t yl = H[r][l] * dmrs[l][m] + CX_MAKE(randn() * sigma, randn() * sigma);
                        h_est_obs[m][r] = yl / dmrs[l][m];
                    }
                }
                ul_eigen_beamform(h_est_obs, nppl, w[l]);
            }
            ul_eigen_orthogonalize4(w);
            cx_t Q[UL_EIGEN_NRX][4];
            for (int r = 0; r < UL_EIGEN_NRX; r++)
                for (int l = 0; l < 4; l++) Q[r][l] = w[l][r];

            /* ④ 4x4 유효 채널: H_eff = combiner^H . H (rank<4여도 항상 4x4 전체 계산) */
            cx_t Heff_g[4][4], Heff_e[4][4];
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) {
                    cx_t sg = CX_ZERO, se = CX_ZERO;
                    for (int r = 0; r < UL_EIGEN_NRX; r++) {
                        sg += conj(U[r][i]) * H[r][j];
                        se += conj(Q[r][i]) * H[r][j];
                    }
                    Heff_g[i][j] = sg; Heff_e[i][j] = se;
                }

            /* ⑤ 두 경로 x (rank개) 독립 CW 인코딩 */
            for (int p = 0; p < 2; p++)
                for (int l = 0; l < rank; l++) {
                    gen_random_bits(tb[p][l], tbsz);
                    attach_crc(tb[p][l], tbsz, CRC24A, tb_crc[p][l]);
                    ldpc_encode(&ldpc, tb_crc[p][l], coded[p][l]);
                    nr_ldpc_rate_match_select(coded[p][l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, E, selbits[p][l]);
                    qam_modulate(selbits[p][l], E, mcs.modulation, sym[p][l]);
                }

            double nv_sum[2][4] = {{0}};
            for (int d = 0; d < num_data; d++) {
                cx_t noise_g[4], noise_e[4];
                for (int r = 0; r < 4; r++) {
                    noise_g[r] = CX_MAKE(randn()*sigma, randn()*sigma);
                    noise_e[r] = CX_MAKE(randn()*sigma, randn()*sigma);
                }
                cx_t sym_g[4] = {CX_ZERO,CX_ZERO,CX_ZERO,CX_ZERO};
                cx_t sym_e[4] = {CX_ZERO,CX_ZERO,CX_ZERO,CX_ZERO};
                for (int l = 0; l < rank; l++) { sym_g[l] = sym[0][l][d]; sym_e[l] = sym[1][l][d]; }

                cx_t xh_g[4], xh_e[4]; double nv_g[4], nv_e[4];
                ul_eigen4_detect_re(Heff_g, rank, sym_g, noise_g, N0, xh_g, nv_g);
                ul_eigen4_detect_re(Heff_e, rank, sym_e, noise_e, N0, xh_e, nv_e);
                for (int l = 0; l < rank; l++) {
                    rx_hat[0][l][d] = xh_g[l]; nv_sum[0][l] += nv_g[l];
                    rx_hat[1][l][d] = xh_e[l]; nv_sum[1][l] += nv_e[l];
                }
            }

            int any_err[2] = {0, 0};
            for (int p = 0; p < 2; p++) {
                for (int l = 0; l < rank; l++) {
                    double env = nv_sum[p][l] / num_data;
                    qam_demap_llr(rx_hat[p][l], num_data, mcs.modulation, env, allllr[p][l]);
                    memset(soft_buf[p][l], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[p][l], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, E, allllr[p][l]);
                    ldpc_decode(&ldpc, soft_buf[p][l], 25, dec[p][l]);

                    int crc_ok = check_crc(dec[p][l], K, CRC24A);
                    int be = 0;
                    for (int i = 0; i < tbsz; i++) if (tb[p][l][i] != dec[p][l][i]) be++;
                    b_err[p]  += be;
                    t_bits[p] += tbsz;
                    if (!crc_ok || be > 0) any_err[p] = 1;
                }
                if (any_err[p]) e_blk[p]++;
            }
        }   /* end trial loop */

        double ber_genie  = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler_genie = e_blk[0] / (double)cfg->numTrials;
        double ber_eig    = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler_eig   = e_blk[1] / (double)cfg->numTrials;
        double avg_rank   = rank_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f  %.2f\n",
               snr, ber_genie, bler_genie, ber_eig, bler_eig, avg_rank);
    }   /* end SNR loop */

    printf("\nPUSCH UL Eigen-BF (4-layer max, rank-adaptive) simulation complete.\n");

    ldpc_free(&ldpc);
    for (int l = 0; l < 4; l++) free(dmrs[l]);
    free(h_est_obs);
    for (int p = 0; p < 2; p++)
        for (int l = 0; l < 4; l++) {
            free(tb[p][l]); free(tb_crc[p][l]); free(coded[p][l]); free(selbits[p][l]);
            free(sym[p][l]); free(allllr[p][l]); free(soft_buf[p][l]); free(dec[p][l]);
            free(rx_hat[p][l]);
        }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL 최대 4계층 수신 빔포밍 — Genie SVD vs Eigen-BF, TDL 주파수선택적 페이딩,
 * 랭크 적응(1~4)
 *
 * 평탄 버전(`run_pusch_ul_eigen_bf_4tx_simulation`) 헤더 참조. 1-Tx/2-Tx
 * TDL 확장에서 배운 교훈("공유 클러스터 게인 × 고정 공간 시그니처")을
 * 그대로 적용 — 공간 채널 H(16×4)는 트라이얼당 1회 고정, 모든 RE가
 * 공유하는 SISO 클러스터 게인 g(re) 하나만 곱해진다(H_re = g(re)·H).
 * 이 구조 덕분에 U(Genie)와 Q(Eigen-BF), 특이값(랭크 적응), 4x4 베이스
 * 유효채널은 모두 RE와 무관하게 트라이얼당 1회만 계산하면 되고, 실제
 * RE별 유효채널은 "고정 4×4 베이스 행렬을 스칼라 g(re)로 스케일"하는
 * 것으로 단순화된다.
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_ul_eigen_bf_4tx_tdl_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int nppl     = (6 * num_rb) / 4;
    if (nppl < 1) nppl = 1;

    int *pilot_pos = (int *)malloc((6*num_rb) * sizeof(int));   /* num_pilots(=6*num_rb) 전체, 아래서 레이어별로 분리 */
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);
    /* 레이어별 파일럿 RE 분리(직교 FDM DMRS, 4-way 인터리브) — 1/2-Tx UL 관례 재사용 */
    int *pilotL[4];
    for (int l = 0; l < 4; l++) pilotL[l] = (int *)malloc(nppl * sizeof(int));
    for (int i = 0; i < nppl; i++)
        for (int l = 0; l < 4; l++) pilotL[l][i] = pilot_pos[4*i + l];

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
    int acsz = ldpc.coded_size;
    int E    = num_data * bps;   /* TS 38.212 5.4.2.1 rate-matched output length
                                     (C=1, single code block -- E=G exactly) */

    double scs_hz = (double)cfg->scsKHz * 1000.0;

    static const unsigned int dmrs_cinit[4] = {0x87ee1924u, 0x87ee1925u, 0x87ee1926u, 0x87ee1927u};
    cx_t *dmrs[4];
    for (int l = 0; l < 4; l++) {
        dmrs[l] = (cx_t *)malloc(nppl * sizeof(cx_t));
        dmrs_sequence(dmrs_cinit[l], nppl, dmrs[l]);
    }

    printf("=== PUSCH UL 4-Layer(최대) Rx 빔포밍 (Eigen-BF vs Genie SVD, Uplink), TDL 주파수선택적 페이딩, Rank-Adaptive ===\n");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Code Rate    : %.4f\n", cr);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Tx Layers : 1~4 (랭크 적응 — eigen_bf_16port_select_rank(), 두 경로 공유)\n");
    printf("gNB Rx Ant   : %d (Genie: 정확한 4x4 SVD / Eigen-BF: 레이어별 공분산 EVD, 비-코드북)\n", UL_EIGEN_NRX);
    printf("Pilot Obs    : %d/layer\n", nppl);
    printf("Channel      : 고정 공간행렬 H(16x4) x 공유 SISO TDL 클러스터 게인, DS=%.0fns, %d taps\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Detector     : MMSE 고정 (rank1=mrc_combine_4rx, rank2=4rx2, rank3=4rx3, rank4=4x4)\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Data RE      : %d\n", num_data);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int    *tb[2][4], *tb_crc[2][4], *coded[2][4], *selbits[2][4], *dec[2][4];
    cx_t   *sym[2][4];
    double *allllr[2][4], *soft_buf[2][4];
    cx_t   *rx_hat[2][4];
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 4; l++) {
            tb[p][l]     = (int    *)malloc(tbsz  * sizeof(int));
            tb_crc[p][l] = (int    *)malloc(K     * sizeof(int));
            coded[p][l]  = (int    *)malloc(acsz  * sizeof(int));
            selbits[p][l]= (int    *)malloc(E     * sizeof(int));
            sym[p][l]    = (cx_t  *)malloc(num_data * sizeof(cx_t));
            allllr[p][l] = (double *)malloc(E     * sizeof(double));
            soft_buf[p][l]=(double *)malloc(acsz  * sizeof(double));
            dec[p][l]    = (int    *)malloc(K     * sizeof(int));
            rx_hat[p][l] = (cx_t  *)malloc(num_data * sizeof(cx_t));
        }
    }
    cx_t (*h_est_obs)[UL_EIGEN_NRX] = (cx_t (*)[UL_EIGEN_NRX])malloc((size_t)nppl * sizeof(*h_est_obs));
    cx_t *cluster_taps = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%-9s  %-12s %-11s  %-12s %-11s  %s\n",
           "SNR(dB)", "BER_Genie", "BLER_Genie", "BER_EigenBF", "BLER_EigenBF", "AvgRank");
    for (int i = 0; i < 78; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        long t_bits[2] = {0}, b_err[2] = {0};
        int  e_blk[2] = {0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 고정 공간행렬 H(16x4, genie) + 공유 SISO TDL 클러스터 게인 */
            cx_t H[UL_EIGEN_NRX][4];
            double inv_sq2 = 1.0 / sqrt(2.0);
            for (int r = 0; r < UL_EIGEN_NRX; r++)
                for (int c = 0; c < 4; c++)
                    H[r][c] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
            tdl_draw(&tdl_ch, cluster_taps);

            /* ② Genie: 4x4 SVD로 U(16x4)와 특이값 계산 (RE 무관, 1회) */
            cx_t U[UL_EIGEN_NRX][4];
            double svs[4];
            ul_eigen_svd_genie4(H, U, svs);

            /* ②-1 랭크 적응(공유 클러스터 게인은 스칼라라 rank 결정에
             * 영향 없음 — H의 특이값 기준으로 충분) */
            int rank;
            eigen_bf_16port_select_rank(svs, N0, &rank);
            rank_sum += rank;

            /* ③ Eigen-BF: 레이어별 파일럿 관측(게인 g(re) 반영) -> 공분산
             * 기반 빔 추정 + 순차 직교화 */
            cx_t w[4][UL_EIGEN_NRX];
            for (int l = 0; l < 4; l++) {
                for (int m = 0; m < nppl; m++) {
                    cx_t g_re = tdl_freq_response(&tdl_ch, cluster_taps, pilotL[l][m]);
                    for (int r = 0; r < UL_EIGEN_NRX; r++) {
                        cx_t yl = g_re * H[r][l] * dmrs[l][m] + CX_MAKE(randn() * sigma, randn() * sigma);
                        h_est_obs[m][r] = yl / dmrs[l][m];
                    }
                }
                ul_eigen_beamform(h_est_obs, nppl, w[l]);
            }
            ul_eigen_orthogonalize4(w);
            cx_t Q[UL_EIGEN_NRX][4];
            for (int r = 0; r < UL_EIGEN_NRX; r++)
                for (int l = 0; l < 4; l++) Q[r][l] = w[l][r];

            /* ④ 4x4 베이스 유효 채널(RE 무관, 1회 계산) — 실제 RE별
             * 유효채널은 이 베이스 행렬에 g(re)만 곱하면 됨 */
            cx_t Heff_g_base[4][4], Heff_e_base[4][4];
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) {
                    cx_t sg = CX_ZERO, se = CX_ZERO;
                    for (int r = 0; r < UL_EIGEN_NRX; r++) {
                        sg += conj(U[r][i]) * H[r][j];
                        se += conj(Q[r][i]) * H[r][j];
                    }
                    Heff_g_base[i][j] = sg; Heff_e_base[i][j] = se;
                }

            /* ⑤ 두 경로 x (rank개) 독립 CW 인코딩 */
            for (int p = 0; p < 2; p++)
                for (int l = 0; l < rank; l++) {
                    gen_random_bits(tb[p][l], tbsz);
                    attach_crc(tb[p][l], tbsz, CRC24A, tb_crc[p][l]);
                    ldpc_encode(&ldpc, tb_crc[p][l], coded[p][l]);
                    nr_ldpc_rate_match_select(coded[p][l], acsz, ldpc.bg, ldpc.Zc,
                                               ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                               /*rv=*/0, E, selbits[p][l]);
                    qam_modulate(selbits[p][l], E, mcs.modulation, sym[p][l]);
                }

            double nv_sum[2][4] = {{0}};
            for (int d = 0; d < num_data; d++) {
                cx_t g_re = tdl_freq_response(&tdl_ch, cluster_taps, data_pos[d]);
                cx_t Heff_g[4][4], Heff_e[4][4];
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) {
                        Heff_g[i][j] = g_re * Heff_g_base[i][j];
                        Heff_e[i][j] = g_re * Heff_e_base[i][j];
                    }

                cx_t noise_g[4], noise_e[4];
                for (int r = 0; r < 4; r++) {
                    noise_g[r] = CX_MAKE(randn()*sigma, randn()*sigma);
                    noise_e[r] = CX_MAKE(randn()*sigma, randn()*sigma);
                }
                cx_t sym_g[4] = {CX_ZERO,CX_ZERO,CX_ZERO,CX_ZERO};
                cx_t sym_e[4] = {CX_ZERO,CX_ZERO,CX_ZERO,CX_ZERO};
                for (int l = 0; l < rank; l++) { sym_g[l] = sym[0][l][d]; sym_e[l] = sym[1][l][d]; }

                cx_t xh_g[4], xh_e[4]; double nv_g[4], nv_e[4];
                ul_eigen4_detect_re(Heff_g, rank, sym_g, noise_g, N0, xh_g, nv_g);
                ul_eigen4_detect_re(Heff_e, rank, sym_e, noise_e, N0, xh_e, nv_e);
                for (int l = 0; l < rank; l++) {
                    rx_hat[0][l][d] = xh_g[l]; nv_sum[0][l] += nv_g[l];
                    rx_hat[1][l][d] = xh_e[l]; nv_sum[1][l] += nv_e[l];
                }
            }

            int any_err[2] = {0, 0};
            for (int p = 0; p < 2; p++) {
                for (int l = 0; l < rank; l++) {
                    double env = nv_sum[p][l] / num_data;
                    qam_demap_llr(rx_hat[p][l], num_data, mcs.modulation, env, allllr[p][l]);
                    memset(soft_buf[p][l], 0, acsz * sizeof(double));
                    nr_ldpc_rate_match_combine(soft_buf[p][l], acsz, ldpc.bg, ldpc.Zc,
                                                ldpc.info_size, ldpc.base_info_cols * ldpc.Zc,
                                                /*rv=*/0, E, allllr[p][l]);
                    ldpc_decode(&ldpc, soft_buf[p][l], 25, dec[p][l]);

                    int crc_ok = check_crc(dec[p][l], K, CRC24A);
                    int be = 0;
                    for (int i = 0; i < tbsz; i++) if (tb[p][l][i] != dec[p][l][i]) be++;
                    b_err[p]  += be;
                    t_bits[p] += tbsz;
                    if (!crc_ok || be > 0) any_err[p] = 1;
                }
                if (any_err[p]) e_blk[p]++;
            }
        }   /* end trial loop */

        double ber_genie  = t_bits[0] > 0 ? (double)b_err[0] / t_bits[0] : 0.0;
        double bler_genie = e_blk[0] / (double)cfg->numTrials;
        double ber_eig    = t_bits[1] > 0 ? (double)b_err[1] / t_bits[1] : 0.0;
        double bler_eig   = e_blk[1] / (double)cfg->numTrials;
        double avg_rank   = rank_sum / cfg->numTrials;

        printf("%-9.1f  %-12.4e %-11.4f  %-12.4e %-11.4f  %.2f\n",
               snr, ber_genie, bler_genie, ber_eig, bler_eig, avg_rank);
    }   /* end SNR loop */

    printf("\nPUSCH UL 4-Layer Eigen-BF + TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos);
    for (int l = 0; l < 4; l++) { free(pilotL[l]); free(dmrs[l]); }
    free(h_est_obs); free(cluster_taps);
    for (int p = 0; p < 2; p++)
        for (int l = 0; l < 4; l++) {
            free(tb[p][l]); free(tb_crc[p][l]); free(coded[p][l]); free(selbits[p][l]);
            free(sym[p][l]); free(allllr[p][l]); free(soft_buf[p][l]); free(dec[p][l]);
            free(rx_hat[p][l]);
        }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UL 최대 4계층 수신 빔포밍 — Genie SVD vs Eigen-BF, HARQ 순환버퍼 IR/Chase
 * (평탄 페이딩/TDL 모두 지원), 랭크 적응(1~4)
 *
 * TDL 버전 헤더 참조. U(Genie 좌특이벡터)/특이값은 H에만 의존하므로(공유
 * 클러스터 게인과 무관) 매 attempt 정확히 유효 — 별도의 "낡음" 없이 매
 * attempt 실제 게인을 그대로 반영한다. Q(Eigen-BF 직교화 빔)는 1/2-Tx
 * HARQ와 동일하게 attempt 0의 채널 상태로 딱 1회 추정해 이후 모든
 * attempt에서 재사용(SRS 기반 공분산 추정 주기가 HARQ보다 느리다는 가정).
 *
 * 랭크 적응: 플랫/TDL 버전과 동일한 등력분배 추정 용량 비교(rank 1~4)로
 * 트라이얼당 1회(attempt 0 이전)만 결정하고 모든 attempt에서 고정(실제
 * RI도 HARQ 버스트 도중에 안 바뀌는 것과 동일 관례).
 * ─────────────────────────────────────────────────────────────────────────── */
void run_pusch_ul_eigen_bf_4tx_harq_simulation(const L1Config *cfg) {
    int num_rb   = cfg->numRB;
    int num_data = 6 * num_rb;
    int nppl     = (6 * num_rb) / 4;
    if (nppl < 1) nppl = 1;

    int is_tdl = (strcmp(cfg->channelModel, "TDL") == 0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int *pilot_pos = (int *)malloc((6*num_rb) * sizeof(int));
    int *data_pos  = (int *)malloc(num_data   * sizeof(int));
    dmrs_pilot_indices(num_rb, pilot_pos);
    dmrs_data_indices(num_rb, data_pos);
    int *pilotL[4];
    for (int l = 0; l < 4; l++) pilotL[l] = (int *)malloc(nppl * sizeof(int));
    for (int i = 0; i < nppl; i++)
        for (int l = 0; l < 4; l++) pilotL[l][i] = pilot_pos[4*i + l];

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

    LDPCCodec ldpc; ldpc_init(&ldpc, K, cr);
    int ncb = ldpc.coded_size;

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    static const unsigned int dmrs_cinit[4] = {0x87ee1928u, 0x87ee1929u, 0x87ee192au, 0x87ee192bu};
    cx_t *dmrs[4];
    for (int l = 0; l < 4; l++) {
        dmrs[l] = (cx_t *)malloc(nppl * sizeof(cx_t));
        dmrs_sequence(dmrs_cinit[l], nppl, dmrs[l]);
    }

    printf("=== PUSCH UL 4-Layer(최대) Rx 빔포밍 (HARQ Circular Buffer %s, Uplink), %s, Rank-Adaptive ===\n",
           is_chase ? "Chase" : "IR",
           is_tdl ? "TDL Frequency-Selective Fading" : "Flat Fading");
    printf("MCS Index    : %d (Table %s)\n", cfg->mcsIndex, cfg->mcsTableType);
    printf("Modulation   : %s (Qm=%d)\n", mcs.modulation, bps);
    printf("Target Rate  : %.4f\n", cr);
    printf("Mother Rate  : %.4f (Ncb=%d, actual NR LDPC BG%d/Zc=%d achieved rate)\n",
           (double)K / ncb, ncb, ldpc.bg, ldpc.Zc);
    printf("Num RB       : %d\n", num_rb);
    printf("UE Tx Layers : 1~4 (랭크 적응, 트라이얼당 1회 결정 후 모든 attempt에서 고정)\n");
    printf("gNB Rx Ant   : %d (Genie: 매 attempt 정확한 4x4 SVD / Eigen-BF: attempt 0에서 고정한 직교화 빔)\n", UL_EIGEN_NRX);
    printf("Detector     : MMSE 고정 (rank1=mrc_combine_4rx, rank2=4rx2, rank3=4rx3, rank4=4x4)\n");
    printf("TB Size/layer: %d bits\n", tbsz);
    printf("Max Retx     : %d\n", max_retx);
    if (is_tdl)
        printf("Channel      : 고정 공간행렬 H(16x4) x 공유 SISO TDL 클러스터 게인(attempt마다 재드로우), DS=%.0fns, %d taps\n",
               cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    else
        printf("Channel      : 고정(빔도 채널도 트라이얼 내내 불변, attempt마다 잡음만 재드로우)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);

    int   *tb[2][4], *tb_crc[2][4], *coded_full[2][4], *selbits[2][4], *dec[2][4];
    cx_t  *sym[2][4], *rx_hat[2][4];
    double *allllr[2][4], *soft_buf[2][4];
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 4; l++) {
            tb[p][l]         = (int    *)malloc(tbsz     * sizeof(int));
            tb_crc[p][l]     = (int    *)malloc(K        * sizeof(int));
            coded_full[p][l] = (int    *)malloc(ncb      * sizeof(int));
            selbits[p][l]    = (int    *)malloc(E        * sizeof(int));
            dec[p][l]        = (int    *)malloc(K        * sizeof(int));
            sym[p][l]        = (cx_t  *)malloc(num_data  * sizeof(cx_t));
            rx_hat[p][l]     = (cx_t  *)malloc(num_data  * sizeof(cx_t));
            allllr[p][l]     = (double *)malloc(E        * sizeof(double));
            soft_buf[p][l]   = (double *)malloc(ncb      * sizeof(double));
        }
    }
    cx_t (*h_est_obs)[UL_EIGEN_NRX] = (cx_t (*)[UL_EIGEN_NRX])malloc((size_t)nppl * sizeof(*h_est_obs));
    cx_t *cluster_taps = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    printf("%-9s  %10s%14s%14s%12s%10s\n", "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx", "AvgRank");
    for (int i = 0; i < 72; i++) printf("-");
    printf("\n");

    TDLChannel tdl_ch;
    for (double snr = cfg->snrStart; snr <= cfg->snrEnd + 1e-6; snr += cfg->snrStep) {
        if (is_tdl) tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0    = 1.0 / pow(10.0, snr / 10.0);
        double sigma = sqrt(N0 / 2.0);

        int total_err[2]={0,0}, total_bits[2]={0,0}, blk_err_final[2]={0,0}, blk_err_1st[2]={0,0};
        long long total_attempts[2] = {0, 0};
        double rank_sum = 0.0;

        for (int trial = 0; trial < cfg->numTrials; trial++) {

            /* ① 고정 공간행렬 H(16x4, genie, 트라이얼 내내 고정) */
            cx_t H[UL_EIGEN_NRX][4];
            double inv_sq2 = 1.0 / sqrt(2.0);
            for (int r = 0; r < UL_EIGEN_NRX; r++)
                for (int c = 0; c < 4; c++)
                    H[r][c] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
            cx_t U[UL_EIGEN_NRX][4];
            double svs[4];
            ul_eigen_svd_genie4(H, U, svs);   /* H에만 의존 — 매 attempt 그대로 유효 */

            /* ①-1 랭크 적응: 트라이얼당 1회 결정, 모든 attempt에서 고정 */
            int rank;
            eigen_bf_16port_select_rank(svs, N0, &rank);
            rank_sum += rank;

            /* ② attempt 0 채널 상태에서 레이어별 파일럿 관측 -> Eigen-BF
             * 직교화 빔 Q 추정(이후 모든 attempt에서 재사용) */
            if (is_tdl) tdl_draw(&tdl_ch, cluster_taps);
            cx_t w[4][UL_EIGEN_NRX];
            for (int l = 0; l < 4; l++) {
                for (int m = 0; m < nppl; m++) {
                    cx_t g_re = is_tdl ? tdl_freq_response(&tdl_ch, cluster_taps, pilotL[l][m]) : CX_MAKE(1.0, 0.0);
                    for (int r = 0; r < UL_EIGEN_NRX; r++) {
                        cx_t yl = g_re * H[r][l] * dmrs[l][m] + CX_MAKE(randn() * sigma, randn() * sigma);
                        h_est_obs[m][r] = yl / dmrs[l][m];
                    }
                }
                ul_eigen_beamform(h_est_obs, nppl, w[l]);
            }
            ul_eigen_orthogonalize4(w);
            cx_t Q[UL_EIGEN_NRX][4];
            for (int r = 0; r < UL_EIGEN_NRX; r++)
                for (int l = 0; l < 4; l++) Q[r][l] = w[l][r];

            /* ③ 4x4 베이스 유효 채널(RE 무관, 1회) */
            cx_t Heff_g_base[4][4], Heff_e_base[4][4];
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) {
                    cx_t sg = CX_ZERO, se = CX_ZERO;
                    for (int r = 0; r < UL_EIGEN_NRX; r++) {
                        sg += conj(U[r][i]) * H[r][j];
                        se += conj(Q[r][i]) * H[r][j];
                    }
                    Heff_g_base[i][j] = sg; Heff_e_base[i][j] = se;
                }

            for (int p = 0; p < 2; p++)
                for (int l = 0; l < rank; l++) {
                    gen_random_bits(tb[p][l], tbsz);
                    attach_crc(tb[p][l], tbsz, CRC24A, tb_crc[p][l]);
                    ldpc_encode(&ldpc, tb_crc[p][l], coded_full[p][l]);
                    for (int i = 0; i < ncb; i++) soft_buf[p][l][i] = 0.0;
                }

            int crc_ok[2][4]={{0}}, be[2][4]={{0}};
            int be_1st[2][4]={{0}}, crc_1st[2][4]={{0}};
            int attempts = 0;

            for (int attempt = 0; attempt < max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];

                for (int p = 0; p < 2; p++)
                    for (int l = 0; l < rank; l++) {
                        nr_ldpc_rate_match_select(coded_full[p][l], ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, selbits[p][l]);
                        qam_modulate(selbits[p][l], E, mcs.modulation, sym[p][l]);
                    }

                /* TDL이면 attempt마다 클러스터 게인 재드로우(attempt 0의
                 * 첫 draw는 위 파일럿 관측과 동일 상태를 재사용) */
                if (is_tdl && attempt > 0) tdl_draw(&tdl_ch, cluster_taps);

                double nv_sum[2][4] = {{0}};
                for (int d = 0; d < num_data; d++) {
                    cx_t g_re = is_tdl ? tdl_freq_response(&tdl_ch, cluster_taps, data_pos[d]) : CX_MAKE(1.0, 0.0);
                    cx_t Heff_g[4][4], Heff_e[4][4];
                    for (int i = 0; i < 4; i++)
                        for (int j = 0; j < 4; j++) {
                            Heff_g[i][j] = g_re * Heff_g_base[i][j];
                            Heff_e[i][j] = g_re * Heff_e_base[i][j];
                        }

                    cx_t noise_g[4], noise_e[4];
                    for (int r = 0; r < 4; r++) {
                        noise_g[r] = CX_MAKE(randn()*sigma, randn()*sigma);
                        noise_e[r] = CX_MAKE(randn()*sigma, randn()*sigma);
                    }
                    cx_t sym_g[4] = {CX_ZERO,CX_ZERO,CX_ZERO,CX_ZERO};
                    cx_t sym_e[4] = {CX_ZERO,CX_ZERO,CX_ZERO,CX_ZERO};
                    for (int l = 0; l < rank; l++) { sym_g[l] = sym[0][l][d]; sym_e[l] = sym[1][l][d]; }

                    cx_t xh_g[4], xh_e[4]; double nv_g[4], nv_e[4];
                    ul_eigen4_detect_re(Heff_g, rank, sym_g, noise_g, N0, xh_g, nv_g);
                    ul_eigen4_detect_re(Heff_e, rank, sym_e, noise_e, N0, xh_e, nv_e);
                    for (int l = 0; l < rank; l++) {
                        rx_hat[0][l][d] = xh_g[l]; nv_sum[0][l] += nv_g[l];
                        rx_hat[1][l][d] = xh_e[l]; nv_sum[1][l] += nv_e[l];
                    }
                }

                int ml = tbsz < ldpc.info_size - crc_bits ? tbsz : ldpc.info_size - crc_bits;
                if (ml < 0) ml = 0;
                for (int p = 0; p < 2; p++) {
                    for (int l = 0; l < rank; l++) {
                        double env = nv_sum[p][l] / num_data;
                        qam_demap_llr(rx_hat[p][l], num_data, mcs.modulation, env, allllr[p][l]);
                        nr_ldpc_rate_match_combine(soft_buf[p][l], ncb, ldpc.bg, ldpc.Zc, ldpc.info_size, ldpc.base_info_cols * ldpc.Zc, rv, E, allllr[p][l]);
                        ldpc_decode(&ldpc, soft_buf[p][l], 25, dec[p][l]);
                        crc_ok[p][l] = check_crc(dec[p][l], K, CRC24A);
                        int biterr = 0;
                        for (int i = 0; i < ml; i++)
                            if (tb[p][l][i] != dec[p][l][i]) biterr++;
                        be[p][l] = biterr;
                        if (attempt == 0) { be_1st[p][l] = biterr; crc_1st[p][l] = crc_ok[p][l]; }
                    }
                }

                attempts = attempt + 1;
                int all_ok = 1;
                for (int p = 0; p < 2; p++)
                    for (int l = 0; l < rank; l++)
                        if (!crc_ok[p][l]) all_ok = 0;
                if (all_ok) break;
            }   /* end attempt loop */

            for (int p = 0; p < 2; p++) {
                int any_err_1st = 0, any_err_final = 0;
                for (int l = 0; l < rank; l++) {
                    total_err[p]  += be[p][l];
                    total_bits[p] += tbsz;
                    if (!crc_1st[p][l] || be_1st[p][l] > 0) any_err_1st = 1;
                    if (!crc_ok[p][l]  || be[p][l]    > 0) any_err_final = 1;
                }
                if (any_err_1st)   blk_err_1st[p]++;
                if (any_err_final) blk_err_final[p]++;
                total_attempts[p] += attempts;
            }
        }   /* end trial loop */

        double avg_rank = rank_sum / cfg->numTrials;
        printf("--- Genie ---\n");
        printf("%-9.1f  %10.4e%14.4f%14.4f%12.2f%10.2f\n", snr,
               total_bits[0]>0 ? (double)total_err[0]/total_bits[0] : 0.0,
               (double)blk_err_1st[0]/cfg->numTrials, (double)blk_err_final[0]/cfg->numTrials,
               (double)total_attempts[0]/cfg->numTrials, avg_rank);
        printf("--- Eigen-BF ---\n");
        printf("%-9.1f  %10.4e%14.4f%14.4f%12.2f%10.2f\n", snr,
               total_bits[1]>0 ? (double)total_err[1]/total_bits[1] : 0.0,
               (double)blk_err_1st[1]/cfg->numTrials, (double)blk_err_final[1]/cfg->numTrials,
               (double)total_attempts[1]/cfg->numTrials, avg_rank);
    }   /* end SNR loop */

    printf("\nPUSCH UL 4-Layer Eigen-BF + %s + HARQ simulation complete.\n", is_tdl ? "TDL" : "Flat Fading");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos);
    for (int l = 0; l < 4; l++) { free(pilotL[l]); free(dmrs[l]); }
    free(h_est_obs); free(cluster_taps);
    for (int p = 0; p < 2; p++)
        for (int l = 0; l < 4; l++) {
            free(tb[p][l]); free(tb_crc[p][l]); free(coded_full[p][l]); free(selbits[p][l]);
            free(dec[p][l]); free(sym[p][l]); free(rx_hat[p][l]);
            free(allllr[p][l]); free(soft_buf[p][l]);
        }
}
