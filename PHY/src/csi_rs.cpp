#include "csi_rs.h"
#include "dmrs.h"
#include "channel_estimation.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <random>

ComplexVec csirsSequence(uint32_t cInit, int length) {
    std::vector<int> c = goldSequence(cInit, 2 * length);
    ComplexVec r(length);
    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    for (int m = 0; m < length; m++) {
        r[m] = Complex(inv_sqrt2 * (1 - 2 * c[2*m]),
                       inv_sqrt2 * (1 - 2 * c[2*m + 1]));
    }
    return r;
}

int csirsPilotsPerRB(int row) {
    switch (row) {
        case 1: return 3;   // k ∈ {0,4,8}
        case 2: return 1;   // k = k_0
        case 3: return 2;   // k ∈ {k_0, k_0+2}
        case 4: return 4;   // k ∈ {k_0, k_0+2, k_0+4, k_0+6}
        default: return 1;
    }
}

std::vector<int> csirsSubcarrierIndices(int numRB, const CSIRSMapConfig& cfg) {
    std::vector<int> indices;
    int k0 = cfg.subcarrierOffset % 12;

    for (int rb = 0; rb < numRB; rb++) {
        int base = rb * 12;
        switch (cfg.row) {
            case 1:
                // density=3, 1 port: fixed at {0,4,8} per RB (TS 38.211 Table 7.4.1.5.3-1 Row 1)
                indices.push_back(base + 0);
                indices.push_back(base + 4);
                indices.push_back(base + 8);
                break;
            case 2:
                // density=1, 1 port: k = k_0
                indices.push_back(base + k0);
                break;
            case 3:
                // density=1, 2 ports: k ∈ {k_0, k_0+2}
                indices.push_back(base + k0);
                indices.push_back(base + k0 + 2);
                break;
            case 4:
                // density=1, 4 ports: k ∈ {k_0, k_0+2, k_0+4, k_0+6}
                indices.push_back(base + k0);
                indices.push_back(base + k0 + 2);
                indices.push_back(base + k0 + 4);
                indices.push_back(base + k0 + 6);
                break;
            default:
                indices.push_back(base + k0);
                break;
        }
    }
    return indices;
}

void runCsirsSimulation(const L1Config& cfg) {
    int numRB            = cfg.numRB;
    int activeSubcarriers = numRB * 12;
    bool useRayleigh     = (cfg.channelModel != "AWGN");

    // Build CSI-RS mapping config
    CSIRSMapConfig mapCfg;
    mapCfg.row             = cfg.csirsRow;
    mapCfg.scramblingID    = static_cast<uint32_t>(cfg.csirsScramID);
    mapCfg.slotIdx         = 0;
    mapCfg.symbolIdx       = cfg.csirsSymbol;
    mapCfg.subcarrierOffset = cfg.csirsK0;

    // TS 38.211 §7.4.1.5.2: c_init = 2^10*(14*n_s+l+1)*(2*N_ID+1) + 2*N_ID
    uint32_t cInit = (uint32_t)(
        ((uint64_t)(14u * mapCfg.slotIdx + mapCfg.symbolIdx + 1u)
         * (2u * mapCfg.scramblingID + 1u) << 10u)
        + 2u * mapCfg.scramblingID) & 0x7FFFFFFFu;

    std::vector<int> pilotPos  = csirsSubcarrierIndices(numRB, mapCfg);
    int numPilots              = static_cast<int>(pilotPos.size());
    ComplexVec txPilots        = csirsSequence(cInit, numPilots);

    // Non-pilot positions for interpolation check
    std::vector<bool> isPilot(activeSubcarriers, false);
    for (int p : pilotPos) isPilot[p] = true;
    std::vector<int> dataPos;
    for (int k = 0; k < activeSubcarriers; k++) {
        if (!isPilot[k]) dataPos.push_back(k);
    }

    double pilotOverhead = static_cast<double>(numPilots) / activeSubcarriers;

    std::cout << "=== CSI-RS Channel Estimation Simulation ===" << std::endl;
    std::cout << "Row          : " << cfg.csirsRow
              << "  (density=" << csirsPilotsPerRB(cfg.csirsRow) << " RE/RB, "
              << csirsPilotsPerRB(cfg.csirsRow)
              << (cfg.csirsRow == 1 ? " port" : " ports") << ")" << std::endl;
    std::cout << "Num RB       : " << numRB << std::endl;
    std::cout << "Active SC    : " << activeSubcarriers << std::endl;
    std::cout << "Pilot REs    : " << numPilots
              << "  (overhead " << std::fixed << std::setprecision(1)
              << pilotOverhead * 100.0 << "%)" << std::endl;
    std::cout << "Scrambling ID: " << cfg.csirsScramID << std::endl;
    std::cout << "Symbol l0    : " << cfg.csirsSymbol << std::endl;
    std::cout << "Channel      : " << (useRayleigh ? "Flat Rayleigh" : "AWGN") << std::endl;
    std::cout << "Trials/SNR   : " << cfg.numTrials << std::endl;
    std::cout << std::endl;

    // SNR range
    std::vector<double> snrRange;
    for (double s = cfg.snrStart; s <= cfg.snrEnd + 1e-6; s += cfg.snrStep)
        snrRange.push_back(s);

    std::cout << std::setw(12) << "SNR (dB)"
              << std::setw(18) << "MSE@Pilots (dB)"
              << std::setw(18) << "MSE@All SC (dB)" << std::endl;
    std::cout << std::string(48, '-') << std::endl;

    // RNG for direct noise injection (N0 = 1/SNR, independent of grid sparsity)
    std::mt19937 rng(std::random_device{}());
    std::normal_distribution<double> hDist(0.0, 1.0 / std::sqrt(2.0));  // CN(0,1) per component

    for (double snr : snrRange) {
        double snrLinear = std::pow(10.0, snr / 10.0);
        double N0        = 1.0 / snrLinear;
        std::normal_distribution<double> nDist(0.0, std::sqrt(N0 / 2.0));

        double sumMsePilot = 0.0;
        double sumMseAll   = 0.0;

        for (int trial = 0; trial < cfg.numTrials; trial++) {
            // Flat fading coefficient: CN(0,1) per trial; h=1 for AWGN
            Complex hTrue = useRayleigh ? Complex(hDist(rng), hDist(rng)) : Complex(1.0, 0.0);

            // Apply channel directly to pilot symbols with fixed-variance noise (no auto-scaling)
            ComplexVec rxPilots(numPilots);
            for (int p = 0; p < numPilots; p++) {
                rxPilots[p] = hTrue * txPilots[p] + Complex(nDist(rng), nDist(rng));
            }

            // LS estimate: h_est[p] = rx[p] / tx[p]
            ComplexVec hAtPilots = lsEstimate(rxPilots, txPilots);

            // MSE at pilot positions: E[|h_ls[p] - h_true|^2]
            double msePilot = 0.0;
            for (int p = 0; p < numPilots; p++) {
                Complex err = hAtPilots[p] - hTrue;
                msePilot += std::norm(err);
            }
            msePilot /= numPilots;
            sumMsePilot += msePilot;

            // Interpolate to all subcarriers and compute MSE
            ComplexVec hFull = interpolateChannel(hAtPilots, pilotPos, activeSubcarriers);
            double mseAll = 0.0;
            for (int k = 0; k < activeSubcarriers; k++) {
                Complex err = hFull[k] - hTrue;
                mseAll += std::norm(err);
            }
            mseAll /= activeSubcarriers;
            sumMseAll += mseAll;
        }

        double msePilotAvg = sumMsePilot / cfg.numTrials;
        double mseAllAvg   = sumMseAll   / cfg.numTrials;

        // Clamp to avoid log(0)
        if (msePilotAvg < 1e-15) msePilotAvg = 1e-15;
        if (mseAllAvg   < 1e-15) mseAllAvg   = 1e-15;

        double msePilotDb = 10.0 * std::log10(msePilotAvg);
        double mseAllDb   = 10.0 * std::log10(mseAllAvg);

        std::cout << std::setw(12) << std::fixed << std::setprecision(1) << snr
                  << std::setw(18) << std::fixed << std::setprecision(2) << msePilotDb
                  << std::setw(18) << std::fixed << std::setprecision(2) << mseAllDb
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Theory (LS): MSE@Pilots ≈ -SNR_dB  (flat channel, unit-power pilots)" << std::endl;
    std::cout << "CSI-RS simulation complete." << std::endl;
}
