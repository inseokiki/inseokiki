#ifndef TDL_MIMO_H
#define TDL_MIMO_H
#include "tdl.h"
/* Research model: real exponential Tx/Rx correlation, four ports each.
 * Not a standardized correlation preset or polarized array model.
 * Apply to diffuse TDL-A/B/C taps; caller handles profile validation. */
int tdl_mimo_correlate_4x4(cx_t taps[4][4][TDL_MAX_TAPS], int num_taps,
                          double rho_tx, double rho_rx);
#endif
