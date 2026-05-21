#ifndef CHANNEL_H
#define CHANNEL_H

#include "utils.h"

class AWGNChannel {
public:
    AWGNChannel(double snrDb);

    // Add AWGN noise to signal
    // nfft: FFT size for OFDM mode. When > 0, noise is scaled so that
    //       per-subcarrier SNR = Es/N0 (assuming unit-power QAM symbols).
    //       When 0, noise is based on measured signal power (non-OFDM mode).
    ComplexVec addNoise(const ComplexVec& signal, int nfft = 0);

    void setSnr(double snrDb);

private:
    double snrDb_;
    std::mt19937 gen_;
};

// Flat Rayleigh block-fading channel.
// Each call to apply() draws a fresh CN(0,1) coefficient that is applied
// uniformly to all subcarriers (constant over one OFDM symbol duration).
class FlatFadingChannel {
public:
    explicit FlatFadingChannel(double snrDb);

    // Apply h * txSymbols + AWGN.  h ~ CN(0,1), same for all symbols.
    // If trueH is non-null, the actual channel coefficient is written to it.
    ComplexVec apply(const ComplexVec& txSymbols, Complex* trueH = nullptr);

    void setSnr(double snrDb);

private:
    double snrDb_;
    std::mt19937 gen_;
};

#endif
