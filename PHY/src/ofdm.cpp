#include "ofdm.h"
#include <cmath>

OFDM::OFDM(int nfft, int cpLen) : nfft_(nfft), cpLen_(cpLen) {}

ComplexVec OFDM::modulate(const ComplexVec& freqSymbols) {
    // IFFT
    ComplexVec timeSignal = ifft(freqSymbols);

    // Add cyclic prefix
    ComplexVec output(cpLen_ + nfft_);
    for (int i = 0; i < cpLen_; i++) {
        output[i] = timeSignal[nfft_ - cpLen_ + i];
    }
    for (int i = 0; i < nfft_; i++) {
        output[cpLen_ + i] = timeSignal[i];
    }
    return output;
}

ComplexVec OFDM::demodulate(const ComplexVec& timeSignal) {
    // Remove cyclic prefix
    ComplexVec noCp(nfft_);
    for (int i = 0; i < nfft_; i++) {
        noCp[i] = timeSignal[cpLen_ + i];
    }
    // FFT
    return fft(noCp);
}

ComplexVec OFDM::ifft(const ComplexVec& input) {
    int N = nfft_;
    ComplexVec output(N);

    for (int n = 0; n < N; n++) {
        Complex sum(0.0, 0.0);
        for (int k = 0; k < N; k++) {
            double angle = 2.0 * PI * k * n / N;
            sum += input[k] * Complex(std::cos(angle), std::sin(angle));
        }
        output[n] = sum / static_cast<double>(N);
    }
    return output;
}

ComplexVec OFDM::fft(const ComplexVec& input) {
    int N = nfft_;
    ComplexVec output(N);

    for (int k = 0; k < N; k++) {
        Complex sum(0.0, 0.0);
        for (int n = 0; n < N; n++) {
            double angle = -2.0 * PI * k * n / N;
            sum += input[n] * Complex(std::cos(angle), std::sin(angle));
        }
        output[k] = sum;
    }
    return output;
}
