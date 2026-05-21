#ifndef OFDM_H
#define OFDM_H

#include "utils.h"

class OFDM {
public:
    OFDM(int nfft, int cpLen);

    // Modulator: frequency domain -> time domain + CP
    ComplexVec modulate(const ComplexVec& freqSymbols);

    // Demodulator: remove CP + time domain -> frequency domain
    ComplexVec demodulate(const ComplexVec& timeSignal);

private:
    int nfft_;    // FFT size
    int cpLen_;   // Cyclic prefix length

    // Simple DFT/IDFT (no external library)
    ComplexVec ifft(const ComplexVec& input);
    ComplexVec fft(const ComplexVec& input);
};

#endif
