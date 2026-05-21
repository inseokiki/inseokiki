#ifndef CHANNEL_ESTIMATION_H
#define CHANNEL_ESTIMATION_H

#include "utils.h"
#include <vector>

// Least-squares channel estimate at pilot positions.
// h_LS[k] = rxPilots[k] / txPilots[k]
ComplexVec lsEstimate(const ComplexVec& rxPilots, const ComplexVec& txPilots);

// Linear frequency-domain interpolation from pilot estimates to all active subcarriers.
// pilotPositions must be sorted in ascending order.
ComplexVec interpolateChannel(const ComplexVec& hPilots,
                               const std::vector<int>& pilotPositions,
                               int numActiveSubcarriers);

// Zero-forcing equalization: y_eq[k] = rx[k] / h[k]
// Small |h| values are clamped to avoid noise amplification.
ComplexVec zfEqualize(const ComplexVec& rxData, const ComplexVec& hData);

// MMSE equalization: w[k] = h*[k] / (|h[k]|^2 + N0)
// Returns biased output y_eq[k] = w[k] * rx[k]  (NOT divided by alpha).
// If alphaOut is non-null, writes per-subcarrier bias alpha[k] = |h[k]|^2/(|h[k]|^2+N0).
ComplexVec mmseEqualize(const ComplexVec& rxData, const ComplexVec& hData,
                         double N0, std::vector<double>* alphaOut = nullptr);

#endif
