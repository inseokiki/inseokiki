/* ================================================================
 *  tbs.h
 *  TS 38.214 §5.1.3.2 Transport Block Size determination (Steps 2-4)
 *
 *  Scope (2026-09-14, per tasks/todo.md's "TBS를 TS 38.214 §5.1.3.2
 *  표준 절차로 교체" item, docs/analysis/phy_development_direction_
 *  validation.md P1-4): every run_pdsch_ and run_pusch_ function in this
 *  project (except run_pdsch_simulation()'s pilot, which takes a
 *  directly user-configured cfg->tbSize and never derives TBS from
 *  N_RE/MCS at all) previously computed
 *    tbsz = (int)(max_dbits * code_rate)        // max_dbits = N_RE*Qm
 *  with no quantization step and no Table 5.1.3.2-1 snapping -- this
 *  module replaces exactly that final step with the standard procedure.
 *
 *  Step 1 (N_RE = min(156,N'_RE)*n_PRB, N_DMRS/N_oh/xOverhead) is
 *  DELIBERATELY NOT reimplemented here: this project already computes
 *  its own N_RE-per-codeword value at each call site (the existing
 *  "max_dbits"/"E" variable, from its own DMRS-position bookkeeping,
 *  not the spec's per-PRB-cap formula) -- redoing Step 1 as well was
 *  not what tasks/todo.md flagged (it named "N_info 양자화·근접 TBS
 *  테이블 절차 없음" specifically), and every one of this project's
 *  existing REs-per-PRB values is far below the 156 cap in practice
 *  (typically 6, from `num_data = 6*num_rb`, vs. a 168-RE-per-PRB
 *  theoretical ceiling at 14 symbols/12 subcarriers) so the cap would
 *  not bind even if reimplemented -- documented here as an explicit,
 *  known scope limitation rather than silently assumed.
 *
 *  This project's multi-layer MIMO functions (SM_2X2/SM_4X4/etc.) give
 *  each of the NL layers its own INDEPENDENT codeword sized as if it
 *  alone occupied the full N_RE budget (see e.g. pdsch.c's
 *  run_pdsch_sm4x4_harq_simulation()) rather than the single-codeword,
 *  v-layer-mapped TBS TS 38.214 §5.1.3.1/5.1.3.2 describe for real
 *  gNB scheduling -- an existing project-level modeling choice this
 *  change does not revisit. Consequently v=1 is implicit at every call
 *  site (each layer's own N_RE*Qm already IS this codeword's N_RE*Qm),
 *  so nr_determine_tbs() takes the N_RE*Qm product directly (exactly
 *  what "max_dbits"/"E" already meant) rather than separate N_RE/Qm/v
 *  parameters.
 *
 *  Confirmed 2026-09-14 via a dedicated spec-lookup pass against the
 *  local TS 38.214 V17.14.0 (2025-06) primary source (§5.1.3.2's own
 *  legacy MathType/OLE equation images could not be pixel-rendered in
 *  this environment, so the two quantization formulas are corroborated
 *  by two independent secondary sources rather than directly read from
 *  the primary source's own images -- flagged here, not treated as
 *  fully primary-confirmed; the 4-step structure, both branch
 *  conditions, Table 5.1.3.2-1's full 93 values, and PDSCH/PUSCH using
 *  identical Steps 2-4 were all confirmed directly from the primary
 *  source's own text). §5.1.3.2's own text has no cross-reference
 *  telling callers to reuse Step 4's internal C in TS 38.212 §5.2.2
 *  segmentation -- this project's nr_seg_compute() (nr_sch.h)
 *  independently recomputing C from B=TBS+24 is confirmed correct,
 *  not a shortcut.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef TBS_H
#define TBS_H

/* TS 38.214 §5.1.3.2 Steps 2-4: given this codeword's N_RE*Qm product
 * (this project's existing "max_dbits"/"E") and its target code rate R,
 * returns the standard-compliant TBS in bits (excludes the outer
 * CRC24A this project always adds separately via B=TBS+24). Always
 * returns >=24 (Table 5.1.3.2-1's own minimum entry, or Step 4's
 * N_info'>=3840 floor) -- callers do not need their own "if(tbsz<1)"
 * floor guard any more. */
int nr_determine_tbs(int n_re_qm, double code_rate);

#endif
