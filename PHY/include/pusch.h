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
void run_pusch_sm2x2_simulation(const L1Config *cfg);
void run_pusch_sm2x2_tdl_simulation(const L1Config *cfg);
void run_pusch_sm2x2_tdl_harq_simulation(const L1Config *cfg);
void run_pusch_ul_eigen_bf_simulation(const L1Config *cfg);
void run_pusch_ul_eigen_bf_tdl_simulation(const L1Config *cfg);
void run_pusch_ul_eigen_bf_harq_simulation(const L1Config *cfg);
void run_pusch_ul_eigen_bf_2tx_simulation(const L1Config *cfg);
void run_pusch_ul_eigen_bf_2tx_tdl_simulation(const L1Config *cfg);
void run_pusch_ul_eigen_bf_2tx_harq_simulation(const L1Config *cfg);
void run_pusch_ul_eigen_bf_4tx_simulation(const L1Config *cfg);
void run_pusch_ul_eigen_bf_4tx_tdl_simulation(const L1Config *cfg);
void run_pusch_ul_eigen_bf_4tx_harq_simulation(const L1Config *cfg);

#endif
