/* ================================================================
 *  prach.h
 *  PRACH random access -- preamble detection + timing advance estimation
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef PRACH_H
#define PRACH_H

#include "config_parser.h"

void run_prach_simulation(const L1Config *cfg);
void run_prach_tdl_simulation(const L1Config *cfg);

#endif
