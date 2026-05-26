#ifndef CSI_RS_H
#define CSI_RS_H

#include "utils.h"
#include "config_parser.h"
#include <stdint.h>

typedef struct {
    int      row;
    uint32_t scramblingID;
    int      slotIdx;
    int      symbolIdx;
    int      subcarrierOffset;
} CSIRSMapConfig;

/* CSI-RS complex sequence, out[length] */
void csirs_sequence(uint32_t c_init, int length, cx_t *out);

int csirs_pilots_per_rb(int row);

/* Subcarrier indices within numRB active RBs.
   out must have at least csirs_pilots_per_rb(row)*numRB elements.
   Returns actual count. */
int csirs_subcarrier_indices(int num_rb, const CSIRSMapConfig *cfg, int *out);

void run_csirs_simulation(const L1Config *cfg);

#endif
