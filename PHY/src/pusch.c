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
#include "rate_matching.h"
#include "modulation.h"
#include "channel.h"
#include "dmrs.h"
#include "channel_estimation.h"
#include "dft_precode.h"
#include "tdl.h"
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
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);

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
    int *txbits  = (int *)malloc(padsz * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc((padsz/bps) * sizeof(cx_t));
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

            int nd = nsym < num_data ? nsym : num_data;
            cx_t *tx_data = data_syms;
            if (precode) {
                dft_precode(data_syms, nd, precoded);
                tx_data = precoded;
            }

            /* build resource grid */
            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<nd;d++) tx_grid[data_pos[d]] = tx_data[d];

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
                idft_precode(eq_data, nd, descrambled);
                demod_in = descrambled;
            }

            if (use_mmse) {
                qam_demap_llr_mmse(demod_in, nd, mcs.modulation, h_data, N0, allllr);
            } else {
                double mhp = 0.0;
                for (int d=0;d<num_data;d++) mhp += CX_NORM(h_data[d]);
                mhp /= num_data;
                double env = (mhp > 1e-10) ? N0/mhp : N0;
                qam_demap_llr(demod_in, nd, mcs.modulation, env, allllr);
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
    printf("\nPUSCH simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(txbits);
    free(data_syms); free(precoded); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data); free(descrambled);
    free(allllr); free(llr); free(decoded);
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
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);

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
    int *txbits  = (int *)malloc(padsz * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc((padsz/bps) * sizeof(cx_t));
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
    double *allllr   = (double *)malloc(padsz        * sizeof(double));
    double *llr      = (double *)malloc(acsz         * sizeof(double));
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
            memcpy(txbits, coded, acsz * sizeof(int));
            for (int i=acsz; i<padsz; i++) txbits[i] = 0;
            int nsym = padsz / bps;
            qam_modulate(txbits, padsz, mcs.modulation, data_syms);

            int nd = nsym < num_data ? nsym : num_data;
            cx_t *tx_data = data_syms;
            if (precode) {
                dft_precode(data_syms, nd, precoded);
                tx_data = precoded;
            }

            for (int k=0;k<active;k++) tx_grid[k] = CX_ZERO;
            for (int p=0;p<num_pilots;p++) tx_grid[pilot_pos[p]] = dmrs_sym[p];
            for (int d=0;d<nd;d++) tx_grid[data_pos[d]] = tx_data[d];

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
                    idft_precode(eq_data, nd, descrambled);
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
                    qam_demap_llr_mmse(eq_data, nd, mcs.modulation, h_data, N0, allllr);
                    env = -1.0; /* already demapped below */
                }
            } else {
                zf_equalize(rx_data, h_data, num_data, eq_data);
                if (precode) {
                    idft_precode(eq_data, nd, descrambled);
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
                qam_demap_llr(demod_in, nd, mcs.modulation, env, allllr);

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
    printf("\nPUSCH+TDL simulation complete.\n");

    ldpc_free(&ldpc);
    free(pilot_pos); free(data_pos); free(dmrs_sym);
    free(tb); free(tb_crc); free(coded); free(txbits);
    free(data_syms); free(precoded); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data); free(descrambled);
    free(allllr); free(llr); free(decoded); free(taps); free(alpha);
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
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);

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
    int *txbits  = (int *)malloc(padsz * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc((padsz/bps) * sizeof(cx_t));
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
    double *allllr   = (double *)malloc(padsz        * sizeof(double));
    double *llr      = (double *)malloc(acsz         * sizeof(double));
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
            memcpy(txbits, coded, acsz * sizeof(int));
            for (int i=acsz; i<padsz; i++) txbits[i] = 0;
            int nsym = padsz / bps;
            qam_modulate(txbits, padsz, mcs.modulation, data_syms);

            int nd = nsym < num_data ? nsym : num_data;
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
            int llr_len = acsz < nd*bps ? acsz : nd*bps;
            memcpy(llr, allllr, llr_len * sizeof(double));
            for (int i=llr_len; i<acsz; i++) llr[i] = 0.0;
            ldpc_decode(&ldpc, llr, 25, decoded);
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
            memcpy(llr, allllr, llr_len * sizeof(double));
            for (int i=llr_len; i<acsz; i++) llr[i] = 0.0;
            ldpc_decode(&ldpc, llr, 25, decoded);
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
    free(tb); free(tb_crc); free(coded); free(txbits);
    free(data_syms); free(precoded); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(y_raw); free(demod_nodfe); free(x_hat0); free(y_dfe);
    free(alpha_cx); free(g); free(alpha);
    free(allllr); free(llr); free(decoded); free(bits_tmp); free(taps);
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
    int acsz  = ldpc.coded_size;
    int padsz = acsz + ((acsz % bps) ? bps - acsz % bps : 0);

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
    int *txbits  = (int *)malloc(padsz * sizeof(int));
    cx_t *data_syms  = (cx_t *)malloc((padsz/bps) * sizeof(cx_t));
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
    double *apriori  = (double *)malloc(padsz       * sizeof(double));
    double *posterior= (double *)malloc(acsz        * sizeof(double));
    double *alpha    = (double *)malloc(num_data    * sizeof(double));
    double *allllr   = (double *)malloc(padsz        * sizeof(double));
    double *llr      = (double *)malloc(acsz         * sizeof(double));
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
            memcpy(txbits, coded, acsz * sizeof(int));
            for (int i=acsz; i<padsz; i++) txbits[i] = 0;
            int nsym = padsz / bps;
            qam_modulate(txbits, padsz, mcs.modulation, data_syms);

            int nd = nsym < num_data ? nsym : num_data;
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
            int llr_len = acsz < nd*bps ? acsz : nd*bps;

            /* --- path 1: no DFE (residual ISI folded into noise) --- */
            for (int d=0;d<nd;d++) demod_nodfe[d] = y_raw[d] / abar_safe;
            double env_nodfe = (mvar + var_alpha) / (abar_safe*abar_safe);
            qam_demap_llr(demod_nodfe, nd, mcs.modulation, env_nodfe, allllr);
            memcpy(llr, allllr, llr_len * sizeof(double));
            for (int i=llr_len; i<acsz; i++) llr[i] = 0.0;
            ldpc_decode(&ldpc, llr, 25, decoded);
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
            memcpy(llr, allllr, llr_len * sizeof(double));
            for (int i=llr_len; i<acsz; i++) llr[i] = 0.0;
            ldpc_decode(&ldpc, llr, 25, decoded);
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
            for (int i=0;i<padsz;i++) apriori[i] = 0.0;
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
                memcpy(llr, allllr, llr_len * sizeof(double));
                for (int i=llr_len; i<acsz; i++) llr[i] = 0.0;

                ldpc_decode_soft(&ldpc, llr, 25, decoded, posterior);
                crc_turbo = check_crc(decoded, K, CRC24A);
                be_turbo = 0;
                for (int i=0;i<ml;i++) if (tb[i]!=decoded[i]) be_turbo++;

                if (iter < turbo_iters-1) {
                    for (int i=0;i<llr_len;i++) apriori[i] = posterior[i] - llr[i];
                    for (int i=llr_len;i<padsz;i++) apriori[i] = 0.0;
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
    free(tb); free(tb_crc); free(coded); free(txbits);
    free(data_syms); free(precoded); free(tx_grid); free(rx_grid);
    free(rx_pilots); free(h_pilots); free(h_full);
    free(rx_data); free(h_data); free(eq_data);
    free(y_raw); free(demod_nodfe); free(x_hat0); free(y_dfe); free(y_soft);
    free(alpha_cx); free(g); free(E_x); free(Var_x); free(apriori); free(posterior);
    free(alpha);
    free(allllr); free(llr); free(decoded); free(bits_tmp); free(taps);
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

    LDPCCodec ldpc; ldpc_init(&ldpc, K, HARQ_MOTHER_RATE);
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
    printf("Mother Rate  : %.4f (Ncb=%d)\n", HARQ_MOTHER_RATE, ncb);
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

                rate_match_select(coded_full, ncb, rv, E, selbits);
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

                rate_match_combine(soft_buf, ncb, rv, E, allllr);
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
