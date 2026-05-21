#include "srs.h"
#include "channel_estimation.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <random>

// Primality test for ZC sequence length selection
static bool isPrime(int n) {
    if (n < 2) return false;
    if (n == 2) return true;
    if (n % 2 == 0) return false;
    for (int i = 3; i * i <= n; i += 2) {
        if (n % i == 0) return false;
    }
    return true;
}

// Largest prime <= n
static int largestPrimeBelow(int n) {
    while (n >= 2 && !isPrime(n)) n--;
    return (n >= 2) ? n : 2;
}

ComplexVec srsSequence(const SRSParams& p) {
    int M = (p.mSRS_b * 12) / p.combSize;  // Sequence length after comb

    // Find largest prime <= M for ZC root (TS 38.211 §5.2.2.2)
    int N_ZC = largestPrimeBelow(M);

    // Root index q per TS 38.211 §5.2.2.2
    // q_bar = N_ZC * (u + 1) / 31
    // q = floor(q_bar + 0.5) + v * (-1)^floor(2*q_bar)
    double q_bar = static_cast<double>(N_ZC) * (p.seqGroupU + 1) / 31.0;
    int sign     = ((static_cast<int>(std::floor(2.0 * q_bar)) % 2) == 0) ? 1 : -1;
    int q        = static_cast<int>(std::floor(q_bar + 0.5)) + p.seqNumV * sign;

    // ZC base sequence x_q(m) = e^(-j*pi*q*m*(m+1)/N_ZC)
    // Extended to M by r(n) = x_q(n mod N_ZC)
    ComplexVec r(M);
    for (int n = 0; n < M; n++) {
        int m     = n % N_ZC;
        double ph = -M_PI * q * static_cast<double>(m) * (m + 1) / N_ZC;
        r[n] = Complex(std::cos(ph), std::sin(ph));
    }

    // Cyclic shift alpha = 2*pi*n_CS / N_ap (TS 38.211 §6.4.1.4.2)
    // N_ap = 8 for comb-2, 12 for comb-4
    int N_ap   = (p.combSize == 2) ? 8 : 12;
    double alpha = 2.0 * M_PI * p.cyclicShift / N_ap;
    for (int n = 0; n < M; n++) {
        r[n] *= Complex(std::cos(alpha * n), std::sin(alpha * n));
    }

    return r;
}

std::vector<int> srsSubcarrierIndices(int numActiveSubcarriers,
                                      int mSRS_b, int combSize, int combOffset) {
    int srsBandwidthSC = mSRS_b * 12;  // Total SRS bandwidth in subcarriers
    if (srsBandwidthSC > numActiveSubcarriers)
        srsBandwidthSC = numActiveSubcarriers;

    // k = k_bar_TC + K_TC * n', n' = 0,...,M-1
    int M = srsBandwidthSC / combSize;
    std::vector<int> indices;
    indices.reserve(M);
    for (int n = 0; n < M; n++) {
        int k = combOffset + combSize * n;
        if (k < numActiveSubcarriers)
            indices.push_back(k);
    }
    return indices;
}

void runSrsSimulation(const L1Config& cfg) {
    int numActiveSubcarriers = cfg.numRB * 12;
    bool useRayleigh         = (cfg.channelModel != "AWGN");

    SRSParams srsP;
    srsP.mSRS_b      = cfg.srsBandwidthRB;
    srsP.combSize    = cfg.srsCombSize;
    srsP.combOffset  = cfg.srsCombOffset;
    srsP.cyclicShift = cfg.srsCyclicShift;
    srsP.seqGroupU   = cfg.srsSeqGroupU;
    srsP.seqNumV     = cfg.srsSeqNumV;

    // Clamp SRS bandwidth to active bandwidth
    if (srsP.mSRS_b * 12 > numActiveSubcarriers)
        srsP.mSRS_b = numActiveSubcarriers / 12;

    int srsBandwidthSC = srsP.mSRS_b * 12;
    int seqLen         = srsBandwidthSC / srsP.combSize;  // Sequence length

    // Validate comb offset
    srsP.combOffset = srsP.combOffset % srsP.combSize;

    std::vector<int> pilotPos = srsSubcarrierIndices(numActiveSubcarriers,
                                                     srsP.mSRS_b, srsP.combSize,
                                                     srsP.combOffset);
    int numPilots = static_cast<int>(pilotPos.size());

    ComplexVec txPilots = srsSequence(srsP);
    // Ensure length matches pilotPos size
    if (static_cast<int>(txPilots.size()) > numPilots)
        txPilots.resize(numPilots);

    double pilotOverhead = static_cast<double>(numPilots) / numActiveSubcarriers;

    // Find N_ZC used for this sequence
    int N_ZC = largestPrimeBelow(seqLen);

    std::cout << "=== SRS Channel Sounding Simulation ===" << std::endl;
    std::cout << "SRS BW (RB)  : " << srsP.mSRS_b
              << "  (" << srsBandwidthSC << " subcarriers)" << std::endl;
    std::cout << "Comb size    : " << srsP.combSize
              << "  (K_TC=" << srsP.combSize << ", offset=" << srsP.combOffset << ")" << std::endl;
    std::cout << "Seq length M : " << seqLen
              << "  (ZC root N_ZC=" << N_ZC << ")" << std::endl;
    std::cout << "Cyclic shift : " << srsP.cyclicShift << std::endl;
    std::cout << "Seq group u  : " << srsP.seqGroupU
              << "  v=" << srsP.seqNumV << std::endl;
    std::cout << "Pilot REs    : " << numPilots
              << "  (overhead " << std::fixed << std::setprecision(1)
              << pilotOverhead * 100.0 << "%)" << std::endl;
    std::cout << "Channel      : " << (useRayleigh ? "Flat Rayleigh" : "AWGN") << std::endl;
    std::cout << "Trials/SNR   : " << cfg.numTrials << std::endl;
    std::cout << std::endl;

    // SNR range
    std::vector<double> snrRange;
    for (double s = cfg.snrStart; s <= cfg.snrEnd + 1e-6; s += cfg.snrStep)
        snrRange.push_back(s);

    std::cout << std::setw(12) << "SNR (dB)"
              << std::setw(18) << "MSE@Pilots (dB)"
              << std::setw(18) << "MSE@All SC (dB)"
              << std::setw(18) << "Theory (dB)" << std::endl;
    std::cout << std::string(66, '-') << std::endl;

    // RNG for direct noise injection (N0 = 1/SNR, independent of grid sparsity)
    std::mt19937 rng(std::random_device{}());
    std::normal_distribution<double> hDist(0.0, 1.0 / std::sqrt(2.0));

    for (double snr : snrRange) {
        double snrLinear = std::pow(10.0, snr / 10.0);
        double N0        = 1.0 / snrLinear;
        std::normal_distribution<double> nDist(0.0, std::sqrt(N0 / 2.0));

        double sumMsePilot = 0.0;
        double sumMseAll   = 0.0;

        for (int trial = 0; trial < cfg.numTrials; trial++) {
            // Flat fading coefficient per trial; h=1 for AWGN
            Complex hTrue = useRayleigh ? Complex(hDist(rng), hDist(rng)) : Complex(1.0, 0.0);

            // Apply channel directly to pilot symbols with fixed N0 noise
            ComplexVec rxPilots(numPilots);
            for (int p = 0; p < numPilots; p++) {
                rxPilots[p] = hTrue * txPilots[p] + Complex(nDist(rng), nDist(rng));
            }

            // LS estimate: h_est[p] = rx[p] / x[p]
            ComplexVec hAtPilots = lsEstimate(rxPilots, txPilots);

            // MSE at pilot positions
            double msePilot = 0.0;
            for (int p = 0; p < numPilots; p++) {
                Complex err = hAtPilots[p] - hTrue;
                msePilot += std::norm(err);
            }
            msePilot /= numPilots;
            sumMsePilot += msePilot;

            // Interpolate to full bandwidth
            ComplexVec hFull = interpolateChannel(hAtPilots, pilotPos, numActiveSubcarriers);

            // MSE at all active subcarriers
            double mseAll = 0.0;
            for (int k = 0; k < numActiveSubcarriers; k++) {
                Complex err = hFull[k] - hTrue;
                mseAll += std::norm(err);
            }
            mseAll /= numActiveSubcarriers;
            sumMseAll += mseAll;
        }

        double msePilotAvg = sumMsePilot / cfg.numTrials;
        double mseAllAvg   = sumMseAll   / cfg.numTrials;

        if (msePilotAvg < 1e-15) msePilotAvg = 1e-15;
        if (mseAllAvg   < 1e-15) mseAllAvg   = 1e-15;

        double msePilotDb = 10.0 * std::log10(msePilotAvg);
        double mseAllDb   = 10.0 * std::log10(mseAllAvg);

        // Theory: LS NMSE = N0 / (pilot_overhead * numPilots) per pilot
        // Simplified: E[|n/x|^2] = N0, MSE@pilot = N0 = 1/SNR
        double theoryDb = -snr;  // MSE = N0 = 1/SNR → MSE_dB = -SNR_dB

        std::cout << std::setw(12) << std::fixed << std::setprecision(1) << snr
                  << std::setw(18) << std::fixed << std::setprecision(2) << msePilotDb
                  << std::setw(18) << std::fixed << std::setprecision(2) << mseAllDb
                  << std::setw(18) << std::fixed << std::setprecision(2) << theoryDb
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "ZC sequence: constant-envelope (|x(n)|=1), PAPR=0dB, flat PSD." << std::endl;
    std::cout << "Theory: MSE@pilots ≈ -SNR_dB  (LS estimator, unit-power pilots)" << std::endl;
    std::cout << "SRS simulation complete." << std::endl;
}
