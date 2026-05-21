cmake_minimum_required(VERSION 3.14)
project(5G_PHY_LLS VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(lls_sim
    src/main.cpp
    src/utils.cpp
    src/modulation.cpp
    src/ofdm.cpp
    src/channel.cpp
    src/ldpc.cpp
    src/polar.cpp
    src/crc.cpp
    src/mcs_table.cpp
    src/polar_rate_match.cpp
    src/pbch.cpp
    src/pdcch.cpp
    src/pdsch.cpp
    src/dmrs.cpp
    src/channel_estimation.cpp
    src/csi_rs.cpp
    src/srs.cpp
)

target_include_directories(lls_sim PRIVATE include)
