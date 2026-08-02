/* ================================================================
 *  pusch.h
 *  PUSCH (uplink data) simulation -- CP-OFDM/DFT-s-OFDM, TDL, DFE/turbo eq.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef PUSCH_H
#define PUSCH_H

#include "config_parser.h"

void run_pusch_simulation(const L1Config *cfg);
void run_pusch_tdl_simulation(const L1Config *cfg);
void run_pusch_tdl_dfe_simulation(const L1Config *cfg);
void run_pusch_tdl_turbo_simulation(const L1Config *cfg);
void run_pusch_harq_simulation(const L1Config *cfg);

#endif
