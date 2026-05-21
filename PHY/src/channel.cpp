#include "channel.h"
#include <cmath>
#include <random>

AWGNChannel::AWGNChannel(double snrDb) : gen_(std::random_device{}()) {
    setSnr(snrDb);
}

void AWGNChannel::setSnr(double snrDb) {
    snrDb_ = snrDb;
}

ComplexVec AWGNChannel::addNoise(const ComplexVec& signal, int nfft) {
    double snrLinear = std::pow(10.0, snrDb_ / 10.0);
    double noisePower;

    if (nfft > 0) {
        // OFDM mode: set noise so that per-subcarrier SNR = Es/N0.
        //
        // QAM symbols have unit average power (Es = 1).
        // After IFFT (1/N scaling), signal power per sample = K*Es/N^2.
        // After FFT, noise per subcarrier = N * sigma^2_time.
        // We want: Es / (N * sigma^2_time) = SNR
        //   => sigma^2_time = 1 / (N * SNR)
        noisePower = 1.0 / (nfft * snrLinear);
    } else {
        // Non-OFDM mode: noise based on measured signal power
        double signalPower = 0.0;
        for (const auto& s : signal) {
            signalPower += std::norm(s);
        }
        signalPower /= signal.size();
        noisePower = signalPower / snrLinear;
    }

    ComplexVec output(signal.size());
    std::normal_distribution<double> dist(0.0, std::sqrt(noisePower / 2.0));

    for (size_t i = 0; i < signal.size(); i++) {
        double noiseI = dist(gen_);
        double noiseQ = dist(gen_);
        output[i] = signal[i] + Complex(noiseI, noiseQ);
    }
    return output;
}

FlatFadingChannel::FlatFadingChannel(double snrDb)
    : snrDb_(snrDb), gen_(std::random_device{}()) {}

void FlatFadingChannel::setSnr(double snrDb) { snrDb_ = snrDb; }

ComplexVec FlatFadingChannel::apply(const ComplexVec& txSymbols, Complex* trueH) {
    // Draw flat fading coefficient h ~ CN(0, 1)
    std::normal_distribution<double> rayleigh(0.0, 1.0 / std::sqrt(2.0));
    Complex h(rayleigh(gen_), rayleigh(gen_));
    if (trueH) *trueH = h;

    // Noise variance: N0 = Es / SNR = 1 / SNR (unit-power symbols assumed)
    double snrLinear = std::pow(10.0, snrDb_ / 10.0);
    double noiseVar = 1.0 / snrLinear;
    std::normal_distribution<double> noise(0.0, std::sqrt(noiseVar / 2.0));

    ComplexVec output(txSymbols.size());
    for (size_t i = 0; i < txSymbols.size(); i++) {
        output[i] = h * txSymbols[i] + Complex(noise(gen_), noise(gen_));
    }
    return output;
}
