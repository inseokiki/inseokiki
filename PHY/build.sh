#!/bin/bash

# 5G PHY Link Level Simulator Build Script

SRC_DIR="src"
INC_DIR="include"
OUT_NAME="lls_sim"
CXX="g++"
CXXFLAGS="-std=c++17 -O2 -Wall"

echo "Building 5G PHY Link Level Simulator..."

# Compile all source files
$CXX $CXXFLAGS -o $OUT_NAME $SRC_DIR/*.cpp -I $INC_DIR

if [ $? -eq 0 ]; then
    echo "Build successful: ./$OUT_NAME"
    echo "Run with: ./$OUT_NAME"
else
    echo "Build failed!"
    exit 1
fi
