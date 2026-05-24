#!/bin/bash

INC_DIR="include"
CXX="g++"
CXXFLAGS="-std=c++17 -O2 -Wall"

LLS_SRCS=$(ls src/*.cpp | grep -v src/ber_sim.cpp | tr '\n' ' ')
BER_SRCS="src/ber_sim.cpp src/utils.cpp src/modulation.cpp src/channel.cpp src/config_parser.cpp"

build_lls() {
    echo "Building lls_sim..."
    $CXX $CXXFLAGS -o lls_sim $LLS_SRCS -I $INC_DIR
    if [ $? -ne 0 ]; then echo "lls_sim build failed!"; exit 1; fi
    echo "  -> lls_sim OK"
}

build_ber() {
    echo "Building ber_sim..."
    $CXX $CXXFLAGS -o ber_sim $BER_SRCS -I $INC_DIR
    if [ $? -ne 0 ]; then echo "ber_sim build failed!"; exit 1; fi
    echo "  -> ber_sim OK"
}

case "$1" in
    lls) build_lls ;;
    ber) build_ber ;;
    *)   build_lls; build_ber ;;
esac

echo ""
echo "Done. Usage:"
echo "  ./lls_sim [config/sim_config.txt]"
echo "  ./ber_sim [config/ber_config.txt]"
