#ifndef TBS_H
#define TBS_H

/*
 * Transport Block Size (TBS) Calculation — 3GPP TS 38.214 Section 5.1.3.2
 *
 * Procedure:
 *  1. N'_RE = 12 * n_symb - n_dmrs_prb - n_oh_prb        (REs per PRB)
 *     N_RE  = min(156, N'_RE) * n_prb                     (total data REs)
 *  2. N_info = N_RE * R * Qm * v                           (info bits, approx)
 *  3. Quantize N_info → TBS via Table 5.1.3.2-1 or formula
 */

/*
 * calc_tbs — returns TBS in bits.
 *
 *  n_prb      : number of allocated PRBs
 *  n_symb     : number of allocated OFDM symbols (1-14)
 *  n_dmrs_prb : DMRS REs per PRB  (e.g. 6 for Type1 single CDM group)
 *  n_oh_prb   : additional overhead REs per PRB (0 for normal operation)
 *  code_rate  : target code rate (decimal, e.g. 0.4785 for MCS10 Table1)
 *  Qm         : modulation order in bits/symbol (2=QPSK, 4=16QAM, ...)
 *  v          : number of layers (1-4)
 *
 * For single-symbol OFDM simulation (current LLS):
 *   n_symb = 1, n_dmrs_prb = 0 (resource grid already has pilots excluded)
 *   → use data_re directly: call calc_tbs_from_nre(data_re, ...) below
 */
int calc_tbs(int n_prb, int n_symb, int n_dmrs_prb, int n_oh_prb,
             double code_rate, int Qm, int v);

/*
 * calc_tbs_from_nre — TBS from known total data RE count (pilot-excluded).
 * Useful when the LLS already separates pilot and data REs.
 *
 *  n_data_re  : total data REs (pilots already excluded)
 *  code_rate  : target code rate
 *  Qm         : modulation order
 *  v          : number of layers
 */
int calc_tbs_from_nre(int n_data_re, double code_rate, int Qm, int v);

#endif /* TBS_H */
