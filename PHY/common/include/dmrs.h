#ifndef DMRS_H
#define DMRS_H

#include "utils.h"
#include <stdint.h>

/* Gold sequence (TS 38.211 §5.2.1).  out[length] must be pre-allocated. */
void gold_sequence(uint32_t c_init, int length, int *out);

/* DMRS complex sequence: out[num_pilots] */
void dmrs_sequence(uint32_t c_init, int num_pilots, cx_t *out);

/* DMRS Type-1 pilot subcarrier indices (even within each RB).
   out must have 6*num_rb elements. */
void dmrs_pilot_indices(int num_rb, int *out);

/* DMRS data subcarrier indices (odd within each RB).
   out must have 6*num_rb elements. */
void dmrs_data_indices(int num_rb, int *out);

#endif
