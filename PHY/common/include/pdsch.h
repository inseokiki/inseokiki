#ifndef PDSCH_H
#define PDSCH_H

#include "config_parser.h"

void run_pdsch_simulation(const L1Config *cfg);
void run_pdsch_dmrs_simulation(const L1Config *cfg);
void run_pdsch_tdl_harq_simulation(const L1Config *cfg);

#endif
