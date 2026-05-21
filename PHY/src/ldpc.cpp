#include "ldpc.h"
#include <cmath>
#include <algorithm>
#include <random>

LDPCCodec::LDPCCodec(int blockSize, double codeRate)
    : infoSize_(blockSize), codeRate_(codeRate) {

    numParityBits_ = static_cast<int>(infoSize_ * (1.0 / codeRate_ - 1.0));
    codedSize_ = infoSize_ + numParityBits_;

    buildParityCheckMatrix();
}

void LDPCCodec::buildParityCheckMatrix() {
    int numChecks = numParityBits_;
    H_.resize(numChecks);

    int colWeight = 3;  // each info bit participates in 3 check nodes

    for (int i = 0; i < infoSize_; i++) {
        for (int j = 0; j < colWeight; j++) {
            int checkIdx = (i * colWeight + j) % numChecks;
            H_[checkIdx].push_back(i);
        }
    }

    // Diagonal parity connections (systematic)
    for (int i = 0; i < numChecks; i++) {
        H_[i].push_back(infoSize_ + i);
    }

    // Build transpose (variable → check) adjacency for O(N) variable node update
    Ht_.resize(codedSize_);
    for (int c = 0; c < numChecks; c++) {
        for (int pos = 0; pos < static_cast<int>(H_[c].size()); pos++) {
            int v = H_[c][pos];
            Ht_[v].push_back({c, pos});
        }
    }
}

std::vector<int> LDPCCodec::encode(const std::vector<int>& infoBits) {
    std::vector<int> coded(codedSize_, 0);

    // Copy information bits (systematic)
    for (int i = 0; i < infoSize_; i++) {
        coded[i] = infoBits[i];
    }

    // Calculate parity bits
    for (int i = 0; i < numParityBits_; i++) {
        int parity = 0;
        for (int idx : H_[i]) {
            if (idx < infoSize_) {
                parity ^= infoBits[idx];
            }
        }
        coded[infoSize_ + i] = parity;
    }

    return coded;
}

std::vector<int> LDPCCodec::decode(const std::vector<double>& llr, int maxIter) {
    // Belief Propagation (Sum-Product) decoder
    int numChecks = numParityBits_;

    // Initialize messages
    std::vector<double> varToCheck(codedSize_);
    std::vector<std::vector<double>> checkToVar(numChecks);

    for (int i = 0; i < numChecks; i++) {
        checkToVar[i].resize(H_[i].size(), 0.0);
    }

    // Initialize variable node beliefs from channel LLR
    for (int i = 0; i < codedSize_; i++) {
        varToCheck[i] = llr[i];
    }

    // Iterative decoding
    for (int iter = 0; iter < maxIter; iter++) {
        // Check node update
        for (int c = 0; c < numChecks; c++) {
            for (size_t i = 0; i < H_[c].size(); i++) {
                double product = 1.0;
                for (size_t j = 0; j < H_[c].size(); j++) {
                    if (i != j) {
                        int varIdx = H_[c][j];
                        double val = varToCheck[varIdx];
                        product *= std::tanh(val / 2.0);
                    }
                }
                // Clamp to avoid numerical issues
                product = std::max(-0.9999, std::min(0.9999, product));
                checkToVar[c][i] = 2.0 * std::atanh(product);
            }
        }

        // Variable node update (O(N*d_v) using pre-built transpose)
        for (int v = 0; v < codedSize_; v++) {
            double sum = llr[v];
            for (const auto& link : Ht_[v]) {
                sum += checkToVar[link.checkIdx][link.posInCheck];
            }
            varToCheck[v] = sum;
        }
    }

    // Hard decision on information bits only
    std::vector<int> decoded(infoSize_);
    for (int i = 0; i < infoSize_; i++) {
        decoded[i] = (varToCheck[i] < 0) ? 1 : 0;
    }

    return decoded;
}
