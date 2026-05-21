# 5G NR PHY Link Level Simulator

A C++ implementation of a 5G NR Physical Layer Link Level Simulator for educational purposes.

## Overview

This simulator implements key components of the 5G NR physical layer based on 3GPP specifications (TS 38.211, 38.212, 38.213). It supports OFDM modulation, various modulation schemes, and channel coding (LDPC/Polar).

## Directory Structure

```
Deveolp/
├── README.md                 # This file
├── build.sh                  # Build script
├── config/
│   └── sim_config.txt        # L1 configuration parameters
├── include/                  # Header files
│   ├── channel.h             # AWGN channel model
│   ├── config.h              # Legacy config (enum definitions)
│   ├── config_parser.h       # Configuration file parser
│   ├── ldpc.h                # LDPC encoder/decoder
│   ├── modulation.h          # QPSK modulation
│   ├── ofdm.h                # OFDM modulator/demodulator
│   ├── polar.h               # Polar encoder/decoder
│   └── utils.h               # Common utilities
└── src/                      # Source files
    ├── channel.cpp
    ├── config_parser.cpp
    ├── ldpc.cpp
    ├── main.cpp              # Main simulation entry point
    ├── modulation.cpp
    ├── ofdm.cpp
    ├── polar.cpp
    └── utils.cpp
```

## Features

### Physical Layer Components

| Component | Description | 3GPP Reference |
|-----------|-------------|----------------|
| OFDM | IFFT/FFT with Cyclic Prefix | TS 38.211 Sec 5.3 |
| Modulation | QPSK (2 bits/symbol) | TS 38.211 Sec 5.1 |
| LDPC Coding | Low-Density Parity-Check for data channels | TS 38.212 Sec 5.3.2 |
| Polar Coding | For control channels | TS 38.212 Sec 5.3.1 |
| AWGN Channel | Additive White Gaussian Noise | - |

### Supported Parameters

| Parameter | Supported Values |
|-----------|------------------|
| Bandwidth | 5, 10, 15, 20, 25, 30, 40, 50, 60, 80, 100 MHz |
| SCS | 15, 30, 60, 120 kHz |
| FFT Size | 128, 256, 512, 1024, 2048, 4096 |
| Modulation | QPSK |
| Coding | None, LDPC, Polar |
| Code Rate | 0.5, 0.67, 0.75, 0.83 |

## Build Instructions

### Requirements
- C++17 compatible compiler (g++ 7+ or clang++ 5+)
- Linux/WSL environment

### Build
```bash
cd Deveolp
./build.sh
```

Or manually:
```bash
g++ -std=c++17 -O2 -Wall -o lls_sim src/*.cpp -I include
```

## Usage

### Run with Default Configuration
```bash
./lls_sim
```

### Run with Custom Configuration
```bash
./lls_sim config/my_config.txt
```

## Configuration File

Edit `config/sim_config.txt` to change simulation parameters:

```ini
# Bandwidth and SCS
BANDWIDTH_MHZ = 100
SCS_KHZ = 30

# Resource Blocks (0 = auto)
NUM_RB = 273

# FFT Size (0 = auto)
NFFT = 4096

# Cyclic Prefix (0 = auto per 3GPP TS 38.211)
CP_LENGTH_FIRST = 0    # Symbol 0, 7
CP_LENGTH_NORMAL = 0   # Symbol 1-6, 8-13

# Modulation and Coding
MODULATION = QPSK
CODING = LDPC
CODE_RATE = 0.5

# Simulation
NUM_OFDM_SYMBOLS = 50
SNR_START = -6
SNR_END = 10
SNR_STEP = 2

# Channel
CHANNEL_MODEL = AWGN
```

## Technical Details

### OFDM Signal Processing

```
TX Chain:
  Info Bits → Encoder → QPSK Mod → IFFT → Add CP → Channel

RX Chain:
  Channel → Remove CP → FFT → QPSK Demod → Decoder → Info Bits
```

### Cyclic Prefix (3GPP TS 38.211)

Normal CP length in samples:
- Symbol 0, 7: `160 × NFFT / 2048`
- Symbol 1-6, 8-13: `144 × NFFT / 2048`

| NFFT | CP First | CP Normal |
|------|----------|-----------|
| 1024 | 80 | 72 |
| 2048 | 160 | 144 |
| 4096 | 320 | 288 |

### Resource Block Table (3GPP TS 38.101)

| Bandwidth | SCS 15kHz | SCS 30kHz | SCS 60kHz |
|-----------|-----------|-----------|-----------|
| 5 MHz | 25 | 11 | - |
| 10 MHz | 52 | 24 | 11 |
| 20 MHz | 106 | 51 | 24 |
| 50 MHz | 270 | 133 | 65 |
| 100 MHz | - | 273 | 135 |

### LDPC Coding

- Simplified belief propagation decoder
- Configurable code rate
- Soft-decision decoding using LLR

### Polar Coding

- Successive Cancellation (SC) decoder
- Bhattacharyya parameter based frozen bit selection
- Used for control channel simulation

## Output

The simulator outputs BER (Bit Error Rate) and BLER (Block Error Rate) vs SNR:

```
=== L1 Configuration ===
Bandwidth    : 100 MHz
SCS          : 30 kHz
NFFT         : 4096
CP (sym 0,7) : 320
CP (normal)  : 288
...

  SNR (dB)            BER           BLER
----------------------------------------
        -6     3.1250e-04         0.0400
   -4.0000     0.0000e+00         0.0000
   ...
```

## Limitations

- Simple DFT implementation (O(N²)) - consider FFTW for large FFT sizes
- Single antenna (no MIMO)
- AWGN channel only (no fading)
- QPSK modulation only (16QAM/64QAM/256QAM not implemented)
- Polar decoder may have performance issues

## Future Extensions

- [ ] Add FFTW library for fast FFT
- [ ] Implement 16QAM, 64QAM, 256QAM
- [ ] Add TDL/CDL fading channel models
- [ ] MIMO support (2x2, 4x4)
- [ ] DMRS and PTRS reference signals
- [ ] Rate matching

## References

- 3GPP TS 38.211: Physical channels and modulation
- 3GPP TS 38.212: Multiplexing and channel coding
- 3GPP TS 38.213: Physical layer procedures
- 3GPP TS 38.101: User Equipment radio transmission and reception

## License

Educational use only.
