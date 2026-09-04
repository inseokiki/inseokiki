/* ================================================================
 *  polar_tables.h
 *  TS 38.212 Table 5.3.1.2-1 (Polar sequence Q_Nmax, N=1024) and
 *  Table 5.3.1.1-1 (input interleaver pattern Pi_IL_max, K_ILmax=164)
 *  -- data only, no logic.
 *
 *  Transcribed programmatically (not by hand) from the local spec copy
 *  at 3gpp/38212-hc0/38212-hc0.docx (word/document.xml table cells,
 *  zipfile+regex extraction, same method as ldpc_tables.h/2026-09-02),
 *  2026-09-04. Cross-verified two independent ways: (1) the docx
 *  extraction was compared cell-for-cell against the same table as
 *  rendered by the 3gpp-server MCP (TS 38.212 v18.8.0) -- identical;
 *  (2) both arrays confirmed to be valid permutations of
 *  0..POLAR_NMAX-1 / 0..POLAR_KILMAX-1 (every reliability/interleaver
 *  table must be a permutation by construction); POLAR_Q_NMAX's first
 *  32 entries additionally cross-checked against well-known reference
 *  values for the standard NR Polar sequence.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef POLAR_TABLES_H
#define POLAR_TABLES_H

#define POLAR_NMAX    1024   /* max Polar code length (2^10) */
#define POLAR_KILMAX   164   /* max input-interleaver length, TS 38.212 5.3.1.1 */

/* Q_Nmax[i] = the i-th bit index (0..POLAR_NMAX-1) in ascending order of
 * reliability -- TS 38.212 Table 5.3.1.2-1. For a code of length N<POLAR_NMAX,
 * the sequence Q_N is the subsequence of entries < N, in the same
 * (ascending-reliability) relative order -- see polar.c. */
extern const short POLAR_Q_NMAX[POLAR_NMAX];

/* Pi_IL_max[k] = the k-th value of the input-interleaver pattern
 * (TS 38.212 Table 5.3.1.1-1), used by 5.3.1.1's Pi(k) construction for
 * K < POLAR_KILMAX (I_IL=1 channels only -- PBCH/PDCCH; UCI sets I_IL=0,
 * an identity no-op, so this table is unused there -- see polar.c). */
extern const unsigned char POLAR_PI_IL_MAX[POLAR_KILMAX];

#endif
