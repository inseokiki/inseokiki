#ifndef UTILS_H
#define UTILS_H

#include <complex>
#include <vector>
#include <random>

using Complex = std::complex<double>;
using ComplexVec = std::vector<Complex>;

// Random bit generator
std::vector<int> generateRandomBits(int numBits);

// BER calculation
double calculateBER(const std::vector<int>& txBits, const std::vector<int>& rxBits);

// Constants
constexpr double PI = 3.14159265358979323846;

#endif
