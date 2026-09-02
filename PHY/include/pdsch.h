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
void run_pdsch_sm4x4_simulation(const L1Config *cfg);
void run_pdsch_sm4x4_tdl_simulation(const L1Config *cfg);
void run_pdsch_sm4x4_harq_simulation(const L1Config *cfg);
void run_pdsch_simo_mrc_tdl_harq_simulation(const L1Config *cfg);
void run_pdsch_cl_4port_simulation(const L1Config *cfg);
void run_pdsch_cl_4port_tdl_simulation(const L1Config *cfg);
void run_pdsch_cl_4port_harq_simulation(const L1Config *cfg);
void run_pdsch_cl_8port_simulation(const L1Config *cfg);
void run_pdsch_cl_8port_tdl_simulation(const L1Config *cfg);
void run_pdsch_cl_8port_harq_simulation(const L1Config *cfg);
void run_pdsch_cl_32port_simulation(const L1Config *cfg);
void run_pdsch_eigen_16port_simulation(const L1Config *cfg);
void run_pdsch_eigen_16port_tdl_simulation(const L1Config *cfg);
void run_pdsch_eigen_16port_subband_simulation(const L1Config *cfg);
void run_pdsch_cl_32port_tdl_simulation(const L1Config *cfg);
void run_pdsch_cl_32port_harq_simulation(const L1Config *cfg);
void run_pdsch_olla_simulation(const L1Config *cfg);
void run_pdsch_olla_simo_mrc_simulation(const L1Config *cfg);
void run_pdsch_olla_sm2x2_simulation(const L1Config *cfg);
void run_pdsch_mumimo_simulation(const L1Config *cfg);
void run_pdsch_mumimo_tdl_simulation(const L1Config *cfg);
void run_pdsch_mumimo_harq_simulation(const L1Config *cfg);
void run_pdsch_beam_mgmt_simulation(const L1Config *cfg);
void run_pdsch_beam_mgmt_tdl_simulation(const L1Config *cfg);
void run_pdsch_beam_mgmt_harq_simulation(const L1Config *cfg);

#endif
