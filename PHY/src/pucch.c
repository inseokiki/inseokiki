/* ================================================================
 *  pucch.c
 *  PUCCH (uplink control) formats 0-3, with TDL and HARQ variants
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "pucch.h"
#include "pucch_seq.h"
#include "polar.h"
#include "polar_rate_match.h"
#include "rate_matching.h"
#include "dft_precode.h"
#include "modulation.h"
#include "channel.h"
#include "channel_estimation.h"
#include "tdl.h"
#include "utils.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Format 0: sequence-based detection, no channel coding, no bit
   demodulation at all -- the UCI value selects one of 2^uci_bits
   candidate cyclic shifts of a common base sequence, and the receiver
   picks the candidate with the largest correlation. AWGN only (H=1),
   so no channel estimation is needed. */
void run_pucch_format0_simulation(const L1Config *cfg) {
    int uci_bits = cfg->pucchUciBits;
    if (uci_bits < 1) uci_bits = 1;
    if (uci_bits > 2) uci_bits = 2;
    int num_cand = 1 << uci_bits;
    const int len = 12;
    const int u = 1;

    cx_t base[12];
    pucch_base_sequence(u, len, base);

    cx_t cand_seq[4][12];
    for (int c = 0; c < num_cand; c++) {
        int shift = c * (len / num_cand);
        double alpha = 2.0 * PHY_PI * shift / len;
        pucch_cyclic_shift(base, len, alpha, cand_seq[c]);
    }

    printf("=== PUCCH Format 0 (Sequence-Based ACK/NACK/SR) ===\n");
    printf("UCI bits     : %d (%d candidate cyclic shifts)\n", uci_bits, num_cand);
    printf("Base sequence: Zadoff-Chu approx (u=%d, len=%d, not exact TS 38.211 table)\n", u, len);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%15s\n", "SNR (dB)", "UCI BER");
    for (int i=0;i<27;i++) printf("-");
    printf("\n");

    cx_t tx[12], rx[12];
    AWGNChannel ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        awgn_init(&ch, snr);
        int bit_err = 0, total_bits = 0;
        for (int trial=0; trial<cfg->numTrials; trial++) {
            int uci_val_bits[2];
            gen_random_bits(uci_val_bits, uci_bits);
            int uci_val = 0;
            for (int b=0;b<uci_bits;b++) uci_val = (uci_val<<1) | uci_val_bits[b];

            memcpy(tx, cand_seq[uci_val], len*sizeof(cx_t));
            awgn_add_noise(&ch, tx, len, 0, rx);

            double best_corr = -1.0; int best_val = 0;
            for (int c=0;c<num_cand;c++) {
                cx_t corr = CX_ZERO;
                for (int n=0;n<len;n++) corr += rx[n]*conj(cand_seq[c][n]);
                double mag = CX_NORM(corr);
                if (mag > best_corr) { best_corr = mag; best_val = c; }
            }
            for (int b=0;b<uci_bits;b++) {
                int bit_tx = (uci_val  >> (uci_bits-1-b)) & 1;
                int bit_rx = (best_val >> (uci_bits-1-b)) & 1;
                if (bit_tx != bit_rx) bit_err++;
            }
            total_bits += uci_bits;
        }
        double ber = (double)bit_err/total_bits;
        printf("%12.1f%15.4e\n", snr, ber);
    }
    printf("\nPUCCH Format 0 simulation complete.\n");
}

/* Format 1: same base sequence as Format 0, but a single BPSK/QPSK
   symbol is repeated over PUCCH_NUM_SYMBOLS OFDM symbols and coherently
   combined at the receiver (matched filter against the base sequence,
   summed across symbols). OCC (for multi-UE multiplexing) is all-ones
   this round -- no multi-UE interference is simulated, so it collapses
   to plain repetition/MRC-style combining. */
void run_pucch_format1_simulation(const L1Config *cfg) {
    int uci_bits = cfg->pucchUciBits;
    if (uci_bits < 1) uci_bits = 1;
    if (uci_bits > 2) uci_bits = 2;
    int num_sym = cfg->pucchNumSymbols;
    if (num_sym < 1) num_sym = 1;
    const int len = 12;
    const int u = 1;

    cx_t base[12];
    pucch_base_sequence(u, len, base);

    printf("=== PUCCH Format 1 (Sequence + Repetition, ACK/NACK/SR) ===\n");
    printf("UCI bits     : %d\n", uci_bits);
    printf("Num Symbols  : %d (repetition/coherent-combining gain)\n", num_sym);
    printf("Base sequence: Zadoff-Chu approx (u=%d, len=%d, not exact TS 38.211 table)\n", u, len);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%15s\n", "SNR (dB)", "UCI BER");
    for (int i=0;i<27;i++) printf("-");
    printf("\n");

    cx_t tx[12], rx[12];
    AWGNChannel ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        awgn_init(&ch, snr);
        int bit_err=0, total_bits=0;
        for (int trial=0; trial<cfg->numTrials; trial++) {
            int uci_val_bits[2];
            gen_random_bits(uci_val_bits, uci_bits);

            cx_t d;
            if (uci_bits == 1) {
                d = CX_MAKE(uci_val_bits[0] ? -1.0 : 1.0, 0.0);  /* BPSK */
            } else {
                qam_modulate(uci_val_bits, 2, "QPSK", &d);
            }

            cx_t combined = CX_ZERO;
            for (int sym=0; sym<num_sym; sym++) {
                for (int n=0;n<len;n++) tx[n] = base[n]*d;
                awgn_add_noise(&ch, tx, len, 0, rx);
                for (int n=0;n<len;n++) combined += rx[n]*conj(base[n]);
            }
            double scale = (double)num_sym * len;
            cx_t d_hat = combined / scale;

            int bits_out[2];
            if (uci_bits == 1) {
                bits_out[0] = (creal(d_hat) < 0) ? 1 : 0;
            } else {
                qam_demodulate(&d_hat, 1, "QPSK", bits_out);
            }
            for (int b=0;b<uci_bits;b++) if (bits_out[b] != uci_val_bits[b]) bit_err++;
            total_bits += uci_bits;
        }
        double ber = (double)bit_err/total_bits;
        printf("%12.1f%15.4e\n", snr, ber);
    }
    printf("\nPUCCH Format 1 simulation complete.\n");
}

/* Format 1 over a TDL frequency-selective channel, genie-aided (perfect
   CSI). PUCCH has no DMRS/channel-estimation pipeline in this LLS (unlike
   PDSCH/PUSCH), so rather than inventing a new pilot structure, the true
   per-RE channel is taken directly from tdl_channel_apply()'s h_out
   parameter (already exposed by tdl.c "for verification") and used as if
   perfectly known at the receiver -- this isolates the repetition/
   combining gain from channel-estimation error, matching the AWGN
   version's implicit H=1 assumption but now under real fading.

   Each of the num_sym repeated symbols draws an independent TDL tap-set
   (models time diversity across the repetition window, the same
   simplification run_pdsch_harq_simulation() uses for HARQ rounds). The
   plain matched-filter sum-and-scale combining of the AWGN version
   generalizes to channel-weighted MRC: each (symbol, tone) branch is
   weighted by conj(h), normalized by sum|h|^2. This reduces exactly to
   the AWGN version when h=1 for every branch. */
void run_pucch_format1_tdl_simulation(const L1Config *cfg) {
    int uci_bits = cfg->pucchUciBits;
    if (uci_bits < 1) uci_bits = 1;
    if (uci_bits > 2) uci_bits = 2;
    int num_sym = cfg->pucchNumSymbols;
    if (num_sym < 1) num_sym = 1;
    const int len = 12;
    const int u = 1;
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    cx_t base[12];
    pucch_base_sequence(u, len, base);

    printf("=== PUCCH Format 1 (Sequence + Repetition), TDL Frequency-Selective Fading ===\n");
    printf("UCI bits     : %d\n", uci_bits);
    printf("Num Symbols  : %d (repetition/genie-aided MRC combining gain)\n", num_sym);
    printf("Base sequence: Zadoff-Chu approx (u=%d, len=%d, not exact TS 38.211 table)\n", u, len);
    printf("Channel      : TDL per repeated symbol, genie-aided CSI (DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%15s\n", "SNR (dB)", "UCI BER");
    for (int i=0;i<27;i++) printf("-");
    printf("\n");

    cx_t tx[12], rx[12], h_known[12];
    cx_t taps[TDL_MAX_TAPS];
    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        int bit_err=0, total_bits=0;
        for (int trial=0; trial<cfg->numTrials; trial++) {
            int uci_val_bits[2];
            gen_random_bits(uci_val_bits, uci_bits);

            cx_t d;
            if (uci_bits == 1) {
                d = CX_MAKE(uci_val_bits[0] ? -1.0 : 1.0, 0.0);  /* BPSK */
            } else {
                qam_modulate(uci_val_bits, 2, "QPSK", &d);
            }

            cx_t combined = CX_ZERO;
            double weight = 0.0;
            for (int sym=0; sym<num_sym; sym++) {
                for (int n=0;n<len;n++) tx[n] = base[n]*d;
                tdl_draw(&tdl_ch, taps);
                tdl_channel_apply(&tdl_ch, taps, tx, len, rx, h_known);
                for (int n=0;n<len;n++) {
                    cx_t hb = h_known[n]*base[n];
                    combined += conj(hb) * rx[n];
                    weight   += CX_NORM(h_known[n]);
                }
            }
            if (weight < 1e-12) weight = 1e-12;
            cx_t d_hat = combined / weight;

            int bits_out[2];
            if (uci_bits == 1) {
                bits_out[0] = (creal(d_hat) < 0) ? 1 : 0;
            } else {
                qam_demodulate(&d_hat, 1, "QPSK", bits_out);
            }
            for (int b=0;b<uci_bits;b++) if (bits_out[b] != uci_val_bits[b]) bit_err++;
            total_bits += uci_bits;
        }
        double ber = (double)bit_err/total_bits;
        printf("%12.1f%15.4e\n", snr, ber);
    }
    printf("\nPUCCH Format 1 + TDL simulation complete.\n");
}

/* Format 1 + TDL + HARQ-style repeated retransmission. PUCCH carries UCI
   (including the HARQ-ACK bit itself for PDSCH), so unlike PDSCH/PUSCH
   there is no CRC on the UCI payload to drive a real ARQ decision --
   Format 1's uci_bits (1-2 raw ACK/NACK/SR bits) are transmitted with no
   channel coding at all. To still exercise the "keep retransmitting
   until correctly received" combining pattern used by the PDSCH/PUSCH
   HARQ functions, this substitutes a genie (ground-truth) bit-match
   check for the missing CRC as the stopping criterion -- not physically
   realizable at a real receiver, but consistent with the genie-aided CSI
   already used by run_pucch_format1_tdl_simulation() above, and here it
   only decides *when a simulated retransmission would have stopped*, not
   anything a receiver could use to alter what it decodes.

   Each retransmission attempt repeats the num_sym-symbol occasion (fresh
   independent TDL draw per symbol, as in the non-HARQ version), and the
   channel-weighted MRC statistic (combined/weight) accumulates *across*
   attempts too -- attempts are just an outer repetition loop around the
   same coherent-combining structure as the inner one. Because there is
   no channel coding, the IR/Chase distinction (cfg->harqRvSeq) does not
   apply here: every attempt retransmits the identical symbol, so
   combining across attempts is inherently Chase-like regardless of the
   configured RV sequence. */
void run_pucch_format1_tdl_harq_simulation(const L1Config *cfg) {
    int uci_bits = cfg->pucchUciBits;
    if (uci_bits < 1) uci_bits = 1;
    if (uci_bits > 2) uci_bits = 2;
    int num_sym = cfg->pucchNumSymbols;
    if (num_sym < 1) num_sym = 1;
    const int len = 12;
    const int u = 1;
    double scs_hz = (double)cfg->scsKHz * 1000.0;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    cx_t base[12];
    pucch_base_sequence(u, len, base);

    printf("=== PUCCH Format 1 (Sequence + Repetition) + HARQ Retransmission, TDL Frequency-Selective Fading ===\n");
    printf("UCI bits     : %d\n", uci_bits);
    printf("Num Symbols  : %d per occasion (genie-aided MRC combining)\n", num_sym);
    printf("Max Retx     : %d (no coding -> IR/Chase n/a, genie bit-match stop)\n", max_retx);
    printf("Base sequence: Zadoff-Chu approx (u=%d, len=%d, not exact TS 38.211 table)\n", u, len);
    printf("Channel      : TDL per repeated symbol, genie-aided CSI, redrawn per attempt (DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%10s%14s%14s%14s%12s\n", "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i=0;i<64;i++) printf("-");
    printf("\n");

    cx_t tx[12], rx[12], h_known[12];
    cx_t taps[TDL_MAX_TAPS];
    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        int total_err=0, total_bits=0, blk_err_final=0;
        int total_err_1st=0, blk_err_1st=0;
        long long total_attempts=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            int uci_val_bits[2];
            gen_random_bits(uci_val_bits, uci_bits);

            cx_t d;
            if (uci_bits == 1) {
                d = CX_MAKE(uci_val_bits[0] ? -1.0 : 1.0, 0.0);  /* BPSK */
            } else {
                qam_modulate(uci_val_bits, 2, "QPSK", &d);
            }

            cx_t combined = CX_ZERO;
            double weight = 0.0;
            int bits_out[2] = {0,0};
            int be_1st = 0, be_final = 0, attempts = 0;

            for (int attempt=0; attempt<max_retx; attempt++) {
                for (int sym=0; sym<num_sym; sym++) {
                    for (int n=0;n<len;n++) tx[n] = base[n]*d;
                    tdl_draw(&tdl_ch, taps);
                    tdl_channel_apply(&tdl_ch, taps, tx, len, rx, h_known);
                    for (int n=0;n<len;n++) {
                        cx_t hb = h_known[n]*base[n];
                        combined += conj(hb) * rx[n];
                        weight   += CX_NORM(h_known[n]);
                    }
                }
                double w = weight < 1e-12 ? 1e-12 : weight;
                cx_t d_hat = combined / w;

                if (uci_bits == 1) {
                    bits_out[0] = (creal(d_hat) < 0) ? 1 : 0;
                } else {
                    qam_demodulate(&d_hat, 1, "QPSK", bits_out);
                }
                int be = 0;
                for (int b=0;b<uci_bits;b++) if (bits_out[b] != uci_val_bits[b]) be++;
                if (attempt==0) be_1st = be;
                be_final = be;
                attempts = attempt+1;
                if (be==0) break;
            }

            total_err  += be_final;
            total_bits += uci_bits;
            if (be_final > 0) blk_err_final++;
            total_err_1st += be_1st;
            if (be_1st > 0) blk_err_1st++;
            total_attempts += attempts;
        }
        double ber       = (double)total_err/total_bits;
        double bler_1st  = (double)blk_err_1st   / cfg->numTrials;
        double bler_final= (double)blk_err_final / cfg->numTrials;
        double avg_tx    = (double)total_attempts / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr, ber, bler_1st, bler_final, avg_tx);
    }
    printf("\nPUCCH Format 1 + TDL + HARQ simulation complete.\n");
}

/* Format 2: short (1-2 symbol), coded UCI (>2 bits, e.g. CQI). Spec
   uses Reed-Muller(32,O) for O<=11 with no separate CRC; this LLS
   already has a working Polar codec (used for PBCH/PDCCH) and no
   verified Reed-Muller generator matrix, so Polar is substituted here
   (implementation-defined -- code family differs from spec, structure
   doesn't). Reported metric is decoded-bit BER, matching how the spec
   itself characterizes O<=11 UCI performance (missed-detection/false-
   alarm on decoded bits, not block CRC). */
void run_pucch_format2_simulation(const L1Config *cfg) {
    int K = cfg->pucchUciBits;
    if (K < 3)  K = 3;
    if (K > 11) K = 11;
    const int N = 32;
    int num_prb = cfg->pucchNumPrb > 0 ? cfg->pucchNumPrb : 1;
    int num_sym = cfg->pucchNumSymbols;
    if (num_sym < 1) num_sym = 1;
    if (num_sym > 2) num_sym = 2;
    int num_re = num_prb * 12 * num_sym;
    const int bps = 2;
    int E = num_re * bps;

    int n_PC, n_PC_wm;
    polar_uci_npc(K, E, &n_PC, &n_PC_wm);
    PolarCodec polar;
    polar_init(&polar, N, K, E, /*I_IL=*/0, n_PC, n_PC_wm);   /* TS 38.212 6.3.1.3.1 */

    printf("=== PUCCH Format 2 (Short, Coded UCI, e.g. CQI) ===\n");
    printf("UCI bits     : %d\n", K);
    printf("Coding       : Polar (%d,%d) -- substituted for spec's Reed-Muller(32,O), no CRC\n", N, K);
    printf("Num PRB      : %d, Num Symbols: %d, Data REs: %d\n", num_prb, num_sym, num_re);
    printf("Modulation   : QPSK\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%15s\n", "SNR (dB)", "UCI BER");
    for (int i=0;i<27;i++) printf("-");
    printf("\n");

    int *uci      = (int *)malloc(K * sizeof(int));
    int *coded    = (int *)malloc(N * sizeof(int));
    int *rm       = (int *)malloc(E * sizeof(int));
    cx_t *syms    = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *rx_syms = (cx_t *)malloc(num_re * sizeof(cx_t));
    double *llr_qam = (double *)malloc(E * sizeof(double));
    double *llr_dm  = (double *)malloc(N * sizeof(double));
    int *decoded  = (int *)malloc(K * sizeof(int));

    AWGNChannel ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        awgn_init(&ch, snr);
        int bit_err=0, total_bits=0;
        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(uci, K);
            polar_encode(&polar, uci, coded);
            polar_rate_match(coded, N, E, K, rm);
            qam_modulate(rm, E, "QPSK", syms);
            awgn_add_noise(&ch, syms, num_re, 0, rx_syms);
            double nv = 1.0 / pow(10.0, snr/10.0);
            qam_demap_llr(rx_syms, num_re, "QPSK", nv, llr_qam);
            polar_rate_dematch(llr_qam, E, N, K, llr_dm);
            polar_decode_scl(&polar, llr_dm, POLAR_SCL_L, /*use_crc=*/0, CRC24C,
                              /*use_rnti=*/0, 0, decoded);   /* K<=11 UCI: no spec CRC, genie bit-compare */
            for (int i=0;i<K;i++) if (decoded[i]!=uci[i]) bit_err++;
            total_bits += K;
        }
        double ber = (double)bit_err/total_bits;
        printf("%12.1f%15.4e\n", snr, ber);
    }
    printf("\nPUCCH Format 2 simulation complete.\n");

    polar_free(&polar);
    free(uci); free(coded); free(rm); free(syms); free(rx_syms);
    free(llr_qam); free(llr_dm); free(decoded);
}

/* Format 3: like Format 2 but long (4-14 symbols, no OCC/multiplexing)
   and transform-precoded (DFT-s-OFDM), same as PUSCH. Under pure AWGN
   (H=1) the unitary DFT/IDFT pair is exactly transparent -- unlike
   PUSCH+TDL, there's no equalizer bias to worry about, so this is the
   cleanest possible precoding case and a good sanity check that
   dft_precode/idft_precode really are lossless under a flat channel. */
void run_pucch_format3_simulation(const L1Config *cfg) {
    int K = cfg->pucchUciBits;
    if (K < 3)  K = 3;
    if (K > 11) K = 11;
    const int N = 64;
    int num_prb = cfg->pucchNumPrb > 0 ? cfg->pucchNumPrb : 1;
    int num_sym = cfg->pucchNumSymbols;
    if (num_sym < 4)  num_sym = 4;
    if (num_sym > 14) num_sym = 14;
    int num_re = num_prb * 12 * num_sym;
    const int bps = 2;
    int E = num_re * bps;

    int n_PC, n_PC_wm;
    polar_uci_npc(K, E, &n_PC, &n_PC_wm);
    PolarCodec polar;
    polar_init(&polar, N, K, E, /*I_IL=*/0, n_PC, n_PC_wm);   /* TS 38.212 6.3.1.3.1 */

    printf("=== PUCCH Format 3 (Long, Coded UCI, DFT-s-OFDM) ===\n");
    printf("UCI bits     : %d\n", K);
    printf("Coding       : Polar (%d,%d) -- substituted for spec's RM/Polar mix, no CRC\n", N, K);
    printf("Num PRB      : %d, Num Symbols: %d, Data REs: %d\n", num_prb, num_sym, num_re);
    printf("Modulation   : QPSK, Transform Precoding: ON (DFT-s-OFDM)\n");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%15s\n", "SNR (dB)", "UCI BER");
    for (int i=0;i<27;i++) printf("-");
    printf("\n");

    int *uci        = (int *)malloc(K * sizeof(int));
    int *coded      = (int *)malloc(N * sizeof(int));
    int *rm         = (int *)malloc(E * sizeof(int));
    cx_t *syms      = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *precoded  = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *rx_syms   = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *descrambled = (cx_t *)malloc(num_re * sizeof(cx_t));
    double *llr_qam = (double *)malloc(E * sizeof(double));
    double *llr_dm  = (double *)malloc(N * sizeof(double));
    int *decoded    = (int *)malloc(K * sizeof(int));

    AWGNChannel ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        awgn_init(&ch, snr);
        int bit_err=0, total_bits=0;
        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(uci, K);
            polar_encode(&polar, uci, coded);
            polar_rate_match(coded, N, E, K, rm);
            qam_modulate(rm, E, "QPSK", syms);
            dft_precode(syms, num_re, precoded);
            awgn_add_noise(&ch, precoded, num_re, 0, rx_syms);
            idft_precode(rx_syms, num_re, descrambled);
            double nv = 1.0 / pow(10.0, snr/10.0);
            qam_demap_llr(descrambled, num_re, "QPSK", nv, llr_qam);
            polar_rate_dematch(llr_qam, E, N, K, llr_dm);
            polar_decode_scl(&polar, llr_dm, POLAR_SCL_L, /*use_crc=*/0, CRC24C,
                              /*use_rnti=*/0, 0, decoded);   /* K<=11 UCI: no spec CRC, genie bit-compare */
            for (int i=0;i<K;i++) if (decoded[i]!=uci[i]) bit_err++;
            total_bits += K;
        }
        double ber = (double)bit_err/total_bits;
        printf("%12.1f%15.4e\n", snr, ber);
    }
    printf("\nPUCCH Format 3 simulation complete.\n");

    polar_free(&polar);
    free(uci); free(coded); free(rm); free(syms); free(precoded);
    free(rx_syms); free(descrambled); free(llr_qam); free(llr_dm); free(decoded);
}

/* Format 3 over a TDL frequency-selective channel, genie-aided (perfect
   CSI, same rationale as run_pucch_format1_tdl_simulation()). Unlike
   Format 1's per-symbol-repetition channel, Format 3 is one continuous
   long transmission across num_re REs, so -- like PUSCH -- it draws a
   single TDL tap-set per trial (block-flat in time, frequency-selective
   across REs) via run_pusch_tdl_simulation()'s pattern. The true channel
   is taken straight from tdl_channel_apply()'s h_out and fed directly
   into zf_equalize()/mmse_equalize() (channel_estimation.h, unmodified --
   those functions don't care whether h came from LS+interpolation or a
   genie), skipping the LS/interpolation step since there's no separate
   PUCCH DMRS to estimate from in this LLS.

   The post-IDFT bias/noise-variance correction is exactly the derivation
   already verified in run_pusch_tdl_simulation() (ZF: uniform noise
   variance from Parseval; MMSE: de-bias by mean(alpha), variance from
   mean(alpha*(1-alpha)) + Var(alpha)) -- reused unchanged, only the
   codec (Polar, no CRC, QPSK-fixed UCI-BER metric) differs, matching the
   existing AWGN Format 3. */
void run_pucch_format3_tdl_simulation(const L1Config *cfg) {
    int K = cfg->pucchUciBits;
    if (K < 3)  K = 3;
    if (K > 11) K = 11;
    const int N = 64;
    int num_prb = cfg->pucchNumPrb > 0 ? cfg->pucchNumPrb : 1;
    int num_sym = cfg->pucchNumSymbols;
    if (num_sym < 4)  num_sym = 4;
    if (num_sym > 14) num_sym = 14;
    int num_re = num_prb * 12 * num_sym;
    const int bps = 2;
    int E = num_re * bps;
    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int n_PC, n_PC_wm;
    polar_uci_npc(K, E, &n_PC, &n_PC_wm);
    PolarCodec polar;
    polar_init(&polar, N, K, E, /*I_IL=*/0, n_PC, n_PC_wm);   /* TS 38.212 6.3.1.3.1 */

    printf("=== PUCCH Format 3 (Long, Coded UCI, DFT-s-OFDM), TDL Frequency-Selective Fading ===\n");
    printf("UCI bits     : %d\n", K);
    printf("Coding       : Polar (%d,%d) -- substituted for spec's RM/Polar mix, no CRC\n", N, K);
    printf("Num PRB      : %d, Num Symbols: %d, Data REs: %d\n", num_prb, num_sym, num_re);
    printf("Modulation   : QPSK, Transform Precoding: ON (DFT-s-OFDM)\n");
    printf("Channel      : TDL, genie-aided CSI (DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Equalizer    : %s\n", use_mmse ? "MMSE" : "ZF");
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%12s%15s\n", "SNR (dB)", "UCI BER");
    for (int i=0;i<27;i++) printf("-");
    printf("\n");

    int *uci        = (int *)malloc(K * sizeof(int));
    int *coded      = (int *)malloc(N * sizeof(int));
    int *rm         = (int *)malloc(E * sizeof(int));
    cx_t *syms      = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *precoded  = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *rx_syms   = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *eq_syms   = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *descrambled = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *h_known   = (cx_t *)malloc(num_re * sizeof(cx_t));
    double *alpha   = (double *)malloc(num_re * sizeof(double));
    double *llr_qam = (double *)malloc(E * sizeof(double));
    double *llr_dm  = (double *)malloc(N * sizeof(double));
    int *decoded    = (int *)malloc(K * sizeof(int));
    cx_t *taps      = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0 = 1.0 / pow(10.0, snr/10.0);
        int bit_err=0, total_bits=0;
        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(uci, K);
            polar_encode(&polar, uci, coded);
            polar_rate_match(coded, N, E, K, rm);
            qam_modulate(rm, E, "QPSK", syms);
            dft_precode(syms, num_re, precoded);

            tdl_draw(&tdl_ch, taps);
            tdl_channel_apply(&tdl_ch, taps, precoded, num_re, rx_syms, h_known);

            cx_t *demod_in; double env;
            if (use_mmse) {
                mmse_equalize(rx_syms, h_known, num_re, N0, eq_syms, alpha);
                idft_precode(eq_syms, num_re, descrambled);
                double abar=0.0, msq=0.0;
                for (int d=0;d<num_re;d++) abar += alpha[d];
                abar /= num_re;
                for (int d=0;d<num_re;d++) msq += alpha[d]*alpha[d];
                msq /= num_re;
                double var_alpha = msq - abar*abar;
                double mvar = 0.0;
                for (int d=0;d<num_re;d++) mvar += alpha[d]*(1.0-alpha[d]);
                mvar /= num_re;
                if (abar < 1e-6) abar = 1e-6;
                for (int d=0;d<num_re;d++) descrambled[d] /= abar;
                demod_in = descrambled;
                env = (mvar + var_alpha) / (abar*abar);
            } else {
                zf_equalize(rx_syms, h_known, num_re, eq_syms);
                idft_precode(eq_syms, num_re, descrambled);
                demod_in = descrambled;
                double inv_sum = 0.0;
                for (int d=0;d<num_re;d++) {
                    double hp = CX_NORM(h_known[d]);
                    inv_sum += (hp > 1e-10) ? 1.0/hp : 1.0/1e-10;
                }
                env = N0 * inv_sum / num_re;
            }

            qam_demap_llr(demod_in, num_re, "QPSK", env, llr_qam);
            polar_rate_dematch(llr_qam, E, N, K, llr_dm);
            polar_decode_scl(&polar, llr_dm, POLAR_SCL_L, /*use_crc=*/0, CRC24C,
                              /*use_rnti=*/0, 0, decoded);   /* K<=11 UCI: no spec CRC, genie bit-compare */
            for (int i=0;i<K;i++) if (decoded[i]!=uci[i]) bit_err++;
            total_bits += K;
        }
        double ber = (double)bit_err/total_bits;
        printf("%12.1f%15.4e\n", snr, ber);
    }
    printf("\nPUCCH Format 3 + TDL simulation complete.\n");

    polar_free(&polar);
    free(uci); free(coded); free(rm); free(syms); free(precoded);
    free(rx_syms); free(eq_syms); free(descrambled); free(h_known); free(alpha);
    free(llr_qam); free(llr_dm); free(decoded); free(taps);
}

/* Format 3 + TDL + HARQ-style circular-buffer retransmission. Reuses the
   generic circular-buffer rate matching from rate_matching.c (built for
   the LDPC mother codeword in run_pdsch_harq_simulation()) directly on
   top of the N=64 Polar mother codeword, in place of the spec-accurate
   polar_rate_match()/polar_rate_dematch() shortening scheme used by the
   non-HARQ run_pucch_format3_tdl_simulation() above -- the same
   substitution the PDSCH HARQ functions make relative to their non-HARQ
   counterparts (full unshortened mother codeword + RV-indexed circular
   window, instead of a single spec-shortened rate-match). polar_init()
   is therefore called with E=N: passing E==N disables the shortening/
   forced-zero branch in polar.c's frozen-bit construction (E<N required
   to trigger it), so this yields a plain unshortened rate-K/N polar
   code with no forced-zero positions to protect against.

   Format 2/3 UCI (K<=11) has no separate CRC in the spec, so -- exactly
   as in run_pucch_format1_tdl_harq_simulation() above -- a genie
   (ground-truth) bit-match against the known UCI substitutes for the
   missing CRC as the retransmission stopping criterion. Channel is
   redrawn every attempt (time diversity across HARQ rounds), same as
   every other TDL+HARQ function in this LLS. */
void run_pucch_format3_tdl_harq_simulation(const L1Config *cfg) {
    int K = cfg->pucchUciBits;
    if (K < 3)  K = 3;
    if (K > 11) K = 11;
    const int N = 64;
    int num_prb = cfg->pucchNumPrb > 0 ? cfg->pucchNumPrb : 1;
    int num_sym = cfg->pucchNumSymbols;
    if (num_sym < 4)  num_sym = 4;
    if (num_sym > 14) num_sym = 14;
    int num_re = num_prb * 12 * num_sym;
    const int bps = 2;
    int E = num_re * bps;
    int use_mmse = (strcmp(cfg->equalizer,"MMSE")==0);
    double scs_hz = (double)cfg->scsKHz * 1000.0;

    int is_chase = (strcmp(cfg->harqRvSeq,"CHASE")==0);
    int rvseq[4] = {0,2,3,1};
    int rvseq_len = is_chase ? 1 : 4;
    int max_retx = cfg->harqMaxRetx > 0 ? cfg->harqMaxRetx : 1;

    PolarCodec polar;
    int n_PC, n_PC_wm;
    polar_uci_npc(K, N, &n_PC, &n_PC_wm);
    polar_init(&polar, N, K, N, /*I_IL=*/0, n_PC, n_PC_wm);   /* TS 38.212 6.3.1.3.1, E=N: no shortening, full mother codeword */
    int ncb = N;

    printf("=== PUCCH Format 3 (Long, Coded UCI, DFT-s-OFDM) + HARQ (Circular Buffer %s), TDL Frequency-Selective Fading ===\n",
           is_chase ? "Chase" : "IR");
    printf("UCI bits     : %d\n", K);
    printf("Coding       : Polar (%d,%d) mother code, full-buffer RV combining -- no CRC (genie stop)\n", N, K);
    printf("Num PRB      : %d, Num Symbols: %d, Data REs: %d\n", num_prb, num_sym, num_re);
    printf("Modulation   : QPSK, Transform Precoding: ON (DFT-s-OFDM)\n");
    printf("Channel      : TDL, genie-aided CSI, redrawn per attempt (DS=%.0fns, %d taps)\n",
           cfg->tdlDelaySpreadNs, TDL_MAX_TAPS);
    printf("Equalizer    : %s\n", use_mmse ? "MMSE" : "ZF");
    printf("Max Retx     : %d\n", max_retx);
    printf("Trials/SNR   : %d\n\n", cfg->numTrials);
    printf("%10s%14s%14s%14s%12s\n", "SNR(dB)", "BER(final)", "BLER(1st)", "BLER(HARQ)", "AvgTx");
    for (int i=0;i<64;i++) printf("-");
    printf("\n");

    int *uci        = (int *)malloc(K * sizeof(int));
    int *coded_full = (int *)malloc(ncb * sizeof(int));
    int *selbits    = (int *)malloc(E * sizeof(int));
    cx_t *syms      = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *precoded  = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *rx_syms   = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *eq_syms   = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *descrambled = (cx_t *)malloc(num_re * sizeof(cx_t));
    cx_t *h_known   = (cx_t *)malloc(num_re * sizeof(cx_t));
    double *alpha   = (double *)malloc(num_re * sizeof(double));
    double *allllr  = (double *)malloc(E * sizeof(double));
    double *soft_buf= (double *)malloc(ncb * sizeof(double));
    int *decoded    = (int *)malloc(K * sizeof(int));
    cx_t *taps      = (cx_t *)malloc(TDL_MAX_TAPS * sizeof(cx_t));

    TDLChannel tdl_ch;
    for (double snr=cfg->snrStart; snr<=cfg->snrEnd+1e-6; snr+=cfg->snrStep) {
        tdl_channel_init(&tdl_ch, cfg->tdlDelaySpreadNs, scs_hz, snr);
        double N0 = 1.0 / pow(10.0, snr/10.0);
        int total_err=0, total_bits=0, blk_err_final=0;
        int total_err_1st=0, blk_err_1st=0;
        long long total_attempts=0;

        for (int trial=0; trial<cfg->numTrials; trial++) {
            gen_random_bits(uci, K);
            polar_encode(&polar, uci, coded_full);
            for (int i=0;i<ncb;i++) soft_buf[i] = 0.0;

            int be_1st = 0, be_final = 0, attempts = 0;

            for (int attempt=0; attempt<max_retx; attempt++) {
                int rv = rvseq[attempt % rvseq_len];
                rate_match_select(coded_full, ncb, rv, E, selbits);
                qam_modulate(selbits, E, "QPSK", syms);
                dft_precode(syms, num_re, precoded);

                tdl_draw(&tdl_ch, taps);
                tdl_channel_apply(&tdl_ch, taps, precoded, num_re, rx_syms, h_known);

                cx_t *demod_in; double env;
                if (use_mmse) {
                    mmse_equalize(rx_syms, h_known, num_re, N0, eq_syms, alpha);
                    idft_precode(eq_syms, num_re, descrambled);
                    double abar=0.0, msq=0.0;
                    for (int d=0;d<num_re;d++) abar += alpha[d];
                    abar /= num_re;
                    for (int d=0;d<num_re;d++) msq += alpha[d]*alpha[d];
                    msq /= num_re;
                    double var_alpha = msq - abar*abar;
                    double mvar = 0.0;
                    for (int d=0;d<num_re;d++) mvar += alpha[d]*(1.0-alpha[d]);
                    mvar /= num_re;
                    if (abar < 1e-6) abar = 1e-6;
                    for (int d=0;d<num_re;d++) descrambled[d] /= abar;
                    demod_in = descrambled;
                    env = (mvar + var_alpha) / (abar*abar);
                } else {
                    zf_equalize(rx_syms, h_known, num_re, eq_syms);
                    idft_precode(eq_syms, num_re, descrambled);
                    demod_in = descrambled;
                    double inv_sum = 0.0;
                    for (int d=0;d<num_re;d++) {
                        double hp = CX_NORM(h_known[d]);
                        inv_sum += (hp > 1e-10) ? 1.0/hp : 1.0/1e-10;
                    }
                    env = N0 * inv_sum / num_re;
                }

                qam_demap_llr(demod_in, num_re, "QPSK", env, allllr);
                rate_match_combine(soft_buf, ncb, rv, E, allllr);
                polar_decode_scl(&polar, soft_buf, POLAR_SCL_L, /*use_crc=*/0, CRC24C,
                                  /*use_rnti=*/0, 0, decoded);   /* K<=11 UCI: no spec CRC, genie bit-compare */

                int be = 0;
                for (int i=0;i<K;i++) if (decoded[i]!=uci[i]) be++;
                if (attempt==0) be_1st = be;
                be_final = be;
                attempts = attempt+1;
                if (be==0) break;
            }

            total_err  += be_final;
            total_bits += K;
            if (be_final > 0) blk_err_final++;
            total_err_1st += be_1st;
            if (be_1st > 0) blk_err_1st++;
            total_attempts += attempts;
        }
        double ber       = (double)total_err/total_bits;
        double bler_1st  = (double)blk_err_1st   / cfg->numTrials;
        double bler_final= (double)blk_err_final / cfg->numTrials;
        double avg_tx    = (double)total_attempts / cfg->numTrials;
        printf("%10.1f%14.4e%14.4f%14.4f%12.2f\n", snr, ber, bler_1st, bler_final, avg_tx);
    }
    printf("\nPUCCH Format 3 + TDL + HARQ simulation complete.\n");

    polar_free(&polar);
    free(uci); free(coded_full); free(selbits); free(syms); free(precoded);
    free(rx_syms); free(eq_syms); free(descrambled); free(h_known); free(alpha);
    free(allllr); free(soft_buf); free(decoded); free(taps);
}
