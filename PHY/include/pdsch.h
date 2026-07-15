/* ================================================================
 *  pdsch.h
 *  PDSCH simulation loop (DMRS, MIMO, HARQ, TDL combinations)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef PDSCH_H
#define PDSCH_H

#include "config_parser.h"

void run_pdsch_simulation(const L1Config *cfg);
void run_pdsch_dmrs_simulation(const L1Config *cfg);
void run_pdsch_simo_mrc_simulation(const L1Config *cfg);
void run_pdsch_sm2x2_simulation(const L1Config *cfg);
void run_pdsch_harq_simulation(const L1Config *cfg);
void run_pdsch_tdl_simulation(const L1Config *cfg);
void run_pdsch_simo_mrc_tdl_simulation(const L1Config *cfg);
void run_pdsch_sm2x2_tdl_simulation(const L1Config *cfg);
void run_pdsch_sm2x2_tdl_harq_simulation(const L1Config *cfg);
void run_pdsch_simo_mrc_tdl_harq_simulation(const L1Config *cfg);

#endif
