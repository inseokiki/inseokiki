/* ================================================================
 *  pbch.h
 *  PBCH simulation loop
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef PBCH_H
#define PBCH_H

#include "config_parser.h"

void run_pbch_simulation(const L1Config *cfg);
void run_pbch_fading_simulation(const L1Config *cfg);

#endif
