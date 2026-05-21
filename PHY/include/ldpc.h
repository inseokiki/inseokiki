#ifndef LDPC_H
#define LDPC_H

#include "utils.h"
#include <vector>

// Simplified LDPC Encoder/Decoder for 5G NR
// Base graph 1 (BG1) for large block sizes
// Base graph 2 (BG2) for small block sizes

class LDPCCodec {
public:
    // code_rate: 1/3, 1/2, 2/3, 3/4, 5/6
    LDPCCodec(int blockSize, double codeRate);

    // Encode information bits
    std::vector<int> encode(const std::vector<int>& infoBits);

    // Decode using belief propagation (soft decision)
    std::vector<int> decode(const std::vector<double>& llr, int maxIter = 25);

    int getCodedSize() const { return codedSize_; }
    int getInfoSize() const { return infoSize_; }

private:
    int infoSize_;
    int codedSize_;
    double codeRate_;
    int numParityBits_;

    // H in check-node view: H_[c] = list of variable indices connected to check c
    std::vector<std::vector<int>> H_;

    // H in variable-node view: Ht_[v] = list of (checkIdx, posInCheck) pairs
    struct VarLink { int checkIdx; int posInCheck; };
    std::vector<std::vector<VarLink>> Ht_;

    void buildParityCheckMatrix();
};

#endif
