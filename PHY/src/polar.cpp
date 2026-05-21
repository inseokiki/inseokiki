#include "polar.h"
#include <cmath>
#include <algorithm>

PolarCodec::PolarCodec(int N, int K) : N_(N), K_(K) {
    generateFrozenBits();
}

void PolarCodec::generateFrozenBits() {
    // Bhattacharyya parameter construction
    std::vector<double> z(N_);
    z[0] = 0.5;

    int n = static_cast<int>(std::log2(N_));

    for (int i = 0; i < n; i++) {
        int curSize = 1 << i;
        std::vector<double> newZ(2 * curSize);
        for (int j = 0; j < curSize; j++) {
            newZ[2*j] = 2*z[j] - z[j]*z[j];
            newZ[2*j+1] = z[j]*z[j];
        }
        for (int j = 0; j < 2 * curSize; j++) {
            z[j] = newZ[j];
        }
    }

    std::vector<std::pair<double, int>> order(N_);
    for (int i = 0; i < N_; i++) {
        order[i] = {z[i], i};
    }
    std::sort(order.begin(), order.end());

    frozenBits_.clear();
    infoBitIndices_.clear();

    for (int i = 0; i < K_; i++) {
        infoBitIndices_.push_back(order[i].second);
    }
    for (int i = K_; i < N_; i++) {
        frozenBits_.push_back(order[i].second);
    }

    std::sort(infoBitIndices_.begin(), infoBitIndices_.end());

    frozenSet_.clear();
    for (int idx : frozenBits_) {
        frozenSet_.insert(idx);
    }
}

std::vector<int> PolarCodec::polarTransform(const std::vector<int>& u) {
    std::vector<int> x = u;
    for (int m = 1; m < N_; m *= 2) {
        for (int i = 0; i < N_; i += 2*m) {
            for (int j = 0; j < m; j++) {
                x[i+j] ^= x[i+j+m];
            }
        }
    }
    return x;
}

std::vector<int> PolarCodec::encode(const std::vector<int>& infoBits) {
    std::vector<int> u(N_, 0);
    for (int i = 0; i < K_; i++) {
        u[infoBitIndices_[i]] = infoBits[i];
    }
    return polarTransform(u);
}

double PolarCodec::f_neg(double a, double b) {
    double sign = ((a >= 0) == (b >= 0)) ? 1.0 : -1.0;
    return sign * std::min(std::abs(a), std::abs(b));
}

double PolarCodec::f_pos(double a, double b, int u) {
    return (1 - 2*u) * a + b;
}

// Standard recursive SC decoder
// This is the Arikan SC decoder using a recursive approach.
// L[s][j] = LLR at stage s, position j
// C[s][j] = partial sum at stage s, position j
// stages: 0 (bit level) .. n (channel level)
// We process bits phi = 0, 1, ..., N-1 sequentially.

std::vector<int> PolarCodec::decode(const std::vector<double>& llr) {
    int n = static_cast<int>(std::log2(N_));

    // Allocate LLR and partial-sum arrays
    // L[s] has N entries, C[s] has N entries
    // s ranges from 0 to n
    std::vector<std::vector<double>> L(n + 1, std::vector<double>(N_, 0.0));
    std::vector<std::vector<int>> C(n + 1, std::vector<int>(N_, 0));

    // Channel LLRs at stage n
    for (int i = 0; i < N_; i++) {
        L[n][i] = llr[i];
    }

    // Recursively decode
    recursiveSCDecode(L, C, n, 0);

    // Collect decoded bits from stage 0
    std::vector<int> decoded(K_);
    for (int i = 0; i < K_; i++) {
        decoded[i] = C[0][infoBitIndices_[i]];
    }
    return decoded;
}

void PolarCodec::recursiveSCDecode(std::vector<std::vector<double>>& L,
                                    std::vector<std::vector<int>>& C,
                                    int stage, int offset) {
    if (stage == 0) {
        // Leaf: make hard decision
        if (frozenSet_.count(offset)) {
            C[0][offset] = 0;
        } else {
            C[0][offset] = (L[0][offset] < 0) ? 1 : 0;
        }
        return;
    }

    int half = 1 << (stage - 1);

    // Step 1: Compute f-function LLRs for left child
    for (int i = 0; i < half; i++) {
        L[stage - 1][offset + i] = f_neg(L[stage][offset + i],
                                          L[stage][offset + half + i]);
    }

    // Step 2: Recursively decode left half
    recursiveSCDecode(L, C, stage - 1, offset);

    // Step 3: Compute g-function LLRs for right child using left partial sums
    for (int i = 0; i < half; i++) {
        L[stage - 1][offset + half + i] = f_pos(L[stage][offset + i],
                                                  L[stage][offset + half + i],
                                                  C[stage - 1][offset + i]);
    }

    // Step 4: Recursively decode right half
    recursiveSCDecode(L, C, stage - 1, offset + half);

    // Step 5: Combine partial sums for parent stage
    for (int i = 0; i < half; i++) {
        C[stage][offset + i] = C[stage - 1][offset + i] ^ C[stage - 1][offset + half + i];
        C[stage][offset + half + i] = C[stage - 1][offset + half + i];
    }
}

// Stubs for legacy interface
double PolarCodec::computeLLR(std::vector<double>&, std::vector<int>&, int, int) { return 0; }
void PolarCodec::partialEncode(std::vector<int>&, int, int) {}
void PolarCodec::calcLLR(std::vector<std::vector<double>>&, std::vector<std::vector<int>>&, int, int) {}
void PolarCodec::updateC(std::vector<std::vector<int>>&, int, int) {}
void PolarCodec::scDecode(std::vector<double>&, std::vector<int>&, int, int) {}
void PolarCodec::recursiveCalcLLR(std::vector<std::vector<double>>&, std::vector<std::vector<int>>&, int, int) {}
void PolarCodec::recursiveUpdateB(std::vector<std::vector<int>>&, int, int) {}
void PolarCodec::recursivelyCalcP(std::vector<std::vector<double>>&, std::vector<std::vector<int>>&, int, int) {}
void PolarCodec::recursivelyUpdateC(std::vector<std::vector<int>>&, int, int) {}
void PolarCodec::scRecursive(std::vector<std::vector<double>>&, std::vector<std::vector<int>>&, int, int) {}
