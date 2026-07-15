/* ================================================================
 *  pucch.h
 *  PUCCH (uplink control) formats 0-3, with TDL and HARQ variants
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef PUCCH_H
#define PUCCH_H

#include "config_parser.h"

void run_pucch_format0_simulation(const L1Config *cfg);
void run_pucch_format1_simulation(const L1Config *cfg);
void run_pucch_format2_simulation(const L1Config *cfg);
void run_pucch_format3_simulation(const L1Config *cfg);
void run_pucch_format1_tdl_simulation(const L1Config *cfg);
void run_pucch_format3_tdl_simulation(const L1Config *cfg);
void run_pucch_format1_tdl_harq_simulation(const L1Config *cfg);
void run_pucch_format3_tdl_harq_simulation(const L1Config *cfg);

#endif
