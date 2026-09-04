/* ================================================================
 *  polar.h
 *  Polar encoder/decoder (control channel)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef POLAR_H
#define POLAR_H

#include "crc.h"

typedef struct {
    int  N;
    int  K;            /* info+CRC bits (excl. PC bits) */
    int  I_IL;          /* TS 38.212 5.3.1.1 input interleaver on/off */
    int  n_PC;            /* TS 38.212 5.3.1.2 number of parity-check bits (0 if unused) */
    int  n_PC_wm;          /* of n_PC, how many use min-row-weight placement (0 or 1) */
    int *pc_mask;            /* pc_mask[i]=1 if bit index i in Q_I^N is a PC bit, length N
                                 (only meaningful where frozen_mask[i]==0) */
    int *frozen_mask;          /* frozen_mask[i]=1 if bit index i is frozen (not in Q_I^N), length N */
} PolarCodec;

/* E = rate-matched output length (same E passed to polar_rate_match).
   Needed here because TS 38.212 5.4.1.1 shortening forces certain
   coded-bit positions to 0 at the encoder; those positions must be
   selected as frozen (not info) bits, or the decoder's forced-zero
   assumption for them will corrupt real info bits placed there.

   I_IL: TS 38.212 5.3.1.1 input interleaver flag -- 1 for PBCH (7.1.4)
   and PDCCH (7.3.3), 0 for UCI Polar (6.3.1.3.1/6.3.2.3.1, an identity
   no-op there). n_PC/n_PC_wm: TS 38.212 5.3.1.2 parity-check bit count
   -- always 0 for PBCH/PDCCH; for UCI, see polar_uci_npc() below
   (0 unless 18<=K<=25, confirmed from TS 38.212 6.3.1.3.1). */
void polar_init(PolarCodec *pc, int N, int K, int E,
                 int I_IL, int n_PC, int n_PC_wm);
void polar_free(PolarCodec *pc);

/* TS 38.212 6.3.1.3.1/6.3.2.3.1 UCI parity-check bit count: n_PC=3
 * (n_PC_wm determined by E-K+3 vs 192) for 18<=K<=25, else n_PC=0.
 * KNOWN OPEN QUESTION (2026-09-04, confirmed via 3gpp-server MCP TS
 * 38.212 v18.8.0 6.3.1.3.1 direct image inspection): the primary source
 * gives explicit conditions only for 18<=K<=25 and K>30; the K in
 * [26,30] case is not covered by an explicit branch in that section's
 * text. This function applies the K>30 branch's result (n_PC=0) to
 * 26<=K<=30 too as the conservative default (matches n_PC=0 on both
 * sides of the confirmed gap) rather than guess -- flagged here rather
 * than silently assumed. */
void polar_uci_npc(int K, int E, int *n_PC, int *n_PC_wm);

/* encode: info[K] -> coded[N] */
void polar_encode(const PolarCodec *pc, const int *info, int *coded);

/* SC decode: llr[N] -> decoded[K] */
void polar_decode(const PolarCodec *pc, const double *llr, int *decoded);

/* CA-SCL (CRC-Aided Successive Cancellation List) decode: llr[N] ->
 * decoded[K]. NOT a TS 38.212-mandated algorithm -- 3GPP specifies only
 * the encoder; the decoder is implementation-defined (same status as
 * this project's LDPC belief-propagation decoder). Standard literature
 * algorithm (Tal & Vardy, "List Decoding of Polar Codes"; path-metric
 * update per Balatsoukas-Stimming et al.'s LLR-domain formulation;
 * CRC-aided path selection per Niu & Chen) -- no 3GPP spec-lookup
 * applies here, this is general coding-theory literature, not a 3GPP
 * claim.
 *
 * L = list size (number of parallel decoding paths kept at each
 * information-bit split, pruned to the L lowest-path-metric survivors).
 * use_crc: if nonzero, among the L final candidates (ranked by
 * ascending path metric), the first one whose decoded[K] passes
 * check_crc()/check_crc_rnti() (with crc_type, and RNTI-masked if
 * use_rnti is nonzero -- PDCCH's DCI CRC is RNTI-masked via
 * attach_crc_rnti(), matching this project's universal info+outer-CRC
 * K convention) is returned; if none pass, or use_crc==0, the single
 * lowest-path-metric candidate is returned regardless (use_rnti/rnti
 * are unused when use_crc==0). Returns 1 if a CRC-passing candidate
 * was selected, 0 otherwise (use_crc==0 always returns 1; decoded[]
 * holds a best-effort result in both cases). */
int polar_decode_scl(const PolarCodec *pc, const double *llr, int L,
                      int use_crc, CRCType crc_type,
                      int use_rnti, uint16_t rnti, int *decoded);

/* Shared CA-SCL list size for this LLS's channel simulations -- implementation-
 * defined (list size is not specified by 3GPP), 8 is a common literature/
 * industry default trading decode complexity for near-ML performance. */
#define POLAR_SCL_L 8

#endif
