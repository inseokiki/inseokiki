#ifndef HARQ_H
#define HARQ_H

/*
 * HARQ Incremental Redundancy (IR) — Circular Buffer Rate Matching
 *
 * Concept (simplified from 3GPP TS 38.212 Section 5.4.2):
 *   - Encoder outputs N_cb coded bits (LDPC output = circular buffer).
 *   - Each HARQ round transmits E bits starting from RV-dependent offset,
 *     wrapping around the buffer (circular).
 *   - Receiver accumulates soft LLRs at the corresponding buffer positions.
 *   - LDPC decoder uses the full N_cb soft buffer (non-transmitted = 0.0 = erasure).
 *
 * RV start offsets (proportional to N_cb):
 *   rv=0 → 0          (systematic bits first — highest priority)
 *   rv=1 → N_cb/4
 *   rv=2 → N_cb/2
 *   rv=3 → 3*N_cb/4
 */

typedef struct {
    double *llr_buf;  /* accumulated soft LLR buffer, length N_cb  */
    int     N_cb;     /* circular buffer size = LDPC coded_size     */
} HARQBuffer;

/* Allocate and zero-init the soft buffer. */
void harq_init(HARQBuffer *h, int N_cb);
void harq_free(HARQBuffer *h);

/* Zero the soft buffer (call at start of each new transport block). */
void harq_reset(HARQBuffer *h);

/*
 * Return the starting bit index in the circular buffer for a given RV.
 *   rv ∈ {0, 1, 2, 3}
 */
int harq_rv_start(int N_cb, int rv);

/*
 * Rate-match encoder output onto E bits for a given RV (TX side).
 *   coded[N_cb]     : full LDPC encoded bits
 *   E               : number of bits to transmit per round
 *   rv              : redundancy version 0-3
 *   out[E]          : output rate-matched bits
 */
void harq_rate_match(const int *coded, int N_cb, int E, int rv, int *out);

/*
 * Soft-combine received LLRs into the buffer (RX side).
 *   llr_rx[E]  : received soft LLRs for this round
 *   E          : number of LLRs (same as rate-matched bit count)
 *   rv         : redundancy version of this round
 */
void harq_combine(HARQBuffer *h, const double *llr_rx, int E, int rv);

/*
 * Return pointer to the accumulated soft buffer for LDPC decoding.
 * Valid until next harq_reset() or harq_free().
 */
const double *harq_get_buf(const HARQBuffer *h);

#endif /* HARQ_H */
