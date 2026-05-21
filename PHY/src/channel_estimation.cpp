#include "channel_estimation.h"
#include <algorithm>
#include <cmath>

ComplexVec lsEstimate(const ComplexVec& rxPilots, const ComplexVec& txPilots) {
    ComplexVec h(rxPilots.size());
    for (size_t k = 0; k < rxPilots.size(); k++) {
        h[k] = rxPilots[k] / txPilots[k];
    }
    return h;
}

ComplexVec interpolateChannel(const ComplexVec& hPilots,
                               const std::vector<int>& pilotPositions,
                               int numActiveSubcarriers) {
    ComplexVec hFull(numActiveSubcarriers);
    int numPilots = static_cast<int>(hPilots.size());
    if (numPilots == 0) return hFull;

    for (int k = 0; k < numActiveSubcarriers; k++) {
        // Binary search for first pilot position >= k
        auto it = std::lower_bound(pilotPositions.begin(), pilotPositions.end(), k);

        if (it == pilotPositions.end()) {
            // k is past the last pilot: hold last value
            hFull[k] = hPilots[numPilots - 1];
        } else if (*it == k) {
            // k is exactly a pilot
            hFull[k] = hPilots[static_cast<int>(it - pilotPositions.begin())];
        } else {
            int rIdx = static_cast<int>(it - pilotPositions.begin());
            if (rIdx == 0) {
                // k is before the first pilot: hold first value
                hFull[k] = hPilots[0];
            } else {
                // Linear interpolation between pilots at lIdx and rIdx
                int lIdx = rIdx - 1;
                int kL = pilotPositions[lIdx];
                int kR = pilotPositions[rIdx];
                double alpha = static_cast<double>(k - kL) / (kR - kL);
                hFull[k] = hPilots[lIdx] * (1.0 - alpha) + hPilots[rIdx] * alpha;
            }
        }
    }
    return hFull;
}

ComplexVec zfEqualize(const ComplexVec& rxData, const ComplexVec& hData) {
    ComplexVec eq(rxData.size());
    for (size_t k = 0; k < rxData.size(); k++) {
        double hPow = std::norm(hData[k]);
        if (hPow < 1e-10) {
            eq[k] = rxData[k];
        } else {
            eq[k] = rxData[k] / hData[k];
        }
    }
    return eq;
}

ComplexVec mmseEqualize(const ComplexVec& rxData, const ComplexVec& hData,
                         double N0, std::vector<double>* alphaOut) {
    ComplexVec eq(rxData.size());
    if (alphaOut) alphaOut->resize(rxData.size());

    for (size_t k = 0; k < rxData.size(); k++) {
        double hPow = std::norm(hData[k]);   // |h|^2
        double denom = hPow + N0;
        Complex w = std::conj(hData[k]) / denom;  // MMSE weight
        eq[k] = w * rxData[k];
        if (alphaOut) (*alphaOut)[k] = hPow / denom;  // alpha = |h|^2/(|h|^2+N0)
    }
    return eq;
}
