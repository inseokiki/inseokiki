/* ================================================================
 *  srs.h
 *  SRS channel sounding simulation
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef SRS_H
#define SRS_H

#include "utils.h"
#include "config_parser.h"

typedef struct {
    int mSRS_b;
    int combSize;
    int combOffset;
    int cyclicShift;
    int seqGroupU;
    int seqNumV;
} SRSParams;

/* ZC-based SRS sequence.  out must have (mSRS_b*12/combSize) elements. */
void srs_sequence(const SRSParams *p, cx_t *out);

/* Comb subcarrier positions, returns count.  out[] must be large enough. */
int srs_subcarrier_indices(int num_active_sc,
                           int mSRS_b, int comb_size, int comb_offset,
                           int *out);

void run_srs_simulation(const L1Config *cfg);

#endif
