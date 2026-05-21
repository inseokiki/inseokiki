#include "utils.h"

std::vector<int> generateRandomBits(int numBits) {
    std::vector<int> bits(numBits);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 1);

    for (int i = 0; i < numBits; i++) {
        bits[i] = dis(gen);
    }
    return bits;
}

double calculateBER(const std::vector<int>& txBits, const std::vector<int>& rxBits) {
    if (txBits.size() != rxBits.size()) return -1.0;

    int errors = 0;
    for (size_t i = 0; i < txBits.size(); i++) {
        if (txBits[i] != rxBits[i]) errors++;
    }
    return static_cast<double>(errors) / txBits.size();
}
