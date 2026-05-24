#include "ofdm.h"
#include <cmath>
#include <algorithm>

OFDM::OFDM(int nfft, int cpLen) : nfft_(nfft), cpLen_(cpLen) {}

ComplexVec OFDM::modulate(const ComplexVec& freqSymbols) {
    ComplexVec timeSignal = ifft(freqSymbols);

    ComplexVec output(cpLen_ + nfft_);
    for (int i = 0; i < cpLen_; i++)
        output[i] = timeSignal[nfft_ - cpLen_ + i];
    for (int i = 0; i < nfft_; i++)
        output[cpLen_ + i] = timeSignal[i];
    return output;
}

ComplexVec OFDM::demodulate(const ComplexVec& timeSignal) {
    ComplexVec noCp(nfft_);
    for (int i = 0; i < nfft_; i++)
        noCp[i] = timeSignal[cpLen_ + i];
    return fft(noCp);
}

// In-place Radix-2 Cooley-Tukey FFT
// sign = -1 : forward FFT (DFT)
// sign = +1 : inverse FFT (before 1/N scaling)
static void radix2(ComplexVec& x, int sign) {
    int N = static_cast<int>(x.size());

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    // Butterfly stages
    for (int len = 2; len <= N; len <<= 1) {
        double angle = sign * 2.0 * PI / len;
        Complex wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < N; i += len) {
            Complex w(1.0, 0.0);
            for (int j = 0; j < len / 2; j++) {
                Complex u = x[i + j];
                Complex v = x[i + j + len / 2] * w;
                x[i + j]             = u + v;
                x[i + j + len / 2]   = u - v;
                w *= wlen;
            }
        }
    }
}

ComplexVec OFDM::fft(const ComplexVec& input) {
    ComplexVec x = input;
    radix2(x, -1);   // forward: e^{-j2π/N}
    return x;
}

ComplexVec OFDM::ifft(const ComplexVec& input) {
    ComplexVec x = input;
    radix2(x, +1);   // inverse: e^{+j2π/N}
    double invN = 1.0 / x.size();
    for (auto& s : x) s *= invN;
    return x;
}
