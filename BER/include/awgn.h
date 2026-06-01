#ifndef AWGN_H
#define AWGN_H

#include "qam.h"

/* Add complex AWGN to unit-power symbols.
 * Noise std per component: sigma = sqrt(1 / (2 * qm * eb_n0_lin))
 * so that Es/N0 = qm * Eb/N0 and E[|noise|^2] = 1/(qm*Eb_N0). */
void awgn_add(const cx_t *in, int n, int qm, double eb_n0_lin, cx_t *out);

#endif
