#ifndef POLAR_H
#define POLAR_H

#include "utils.h"
#include <vector>
#include <set>

class PolarCodec {
public:
    PolarCodec(int N, int K);

    std::vector<int> encode(const std::vector<int>& infoBits);
    std::vector<int> decode(const std::vector<double>& llr);

    int getCodedSize() const { return N_; }
    int getInfoSize() const { return K_; }
    const std::vector<int>& getInfoBitIndices() const { return infoBitIndices_; }

private:
    int N_;
    int K_;

    std::vector<int> frozenBits_;
    std::vector<int> infoBitIndices_;
    std::set<int> frozenSet_;

    void generateFrozenBits();
    std::vector<int> polarTransform(const std::vector<int>& u);

    double f_neg(double a, double b);
    double f_pos(double a, double b, int u);

    // Core SC decoder recursive function
    void recursiveSCDecode(std::vector<std::vector<double>>& L,
                           std::vector<std::vector<int>>& C,
                           int stage, int offset);

    // Legacy stubs (kept for ABI compatibility)
    double computeLLR(std::vector<double>& alpha, std::vector<int>& beta, int idx, int n);
    void partialEncode(std::vector<int>& beta, int idx, int n);
    void calcLLR(std::vector<std::vector<double>>& L,
                 std::vector<std::vector<int>>& C, int n, int phi);
    void updateC(std::vector<std::vector<int>>& C, int n, int phi);
    void scDecode(std::vector<double>& L, std::vector<int>& u, int start, int len);
    void recursiveCalcLLR(std::vector<std::vector<double>>& L,
                          std::vector<std::vector<int>>& B, int stage, int bitIdx);
    void recursiveUpdateB(std::vector<std::vector<int>>& B, int stage, int bitIdx);
    void recursivelyCalcP(std::vector<std::vector<double>>& llrArr,
                          std::vector<std::vector<int>>& bitArr, int layer, int phi);
    void recursivelyUpdateC(std::vector<std::vector<int>>& bitArr, int layer, int phi);
    void scRecursive(std::vector<std::vector<double>>& L,
                     std::vector<std::vector<int>>& ucap, int layer, int phi);
};

#endif
