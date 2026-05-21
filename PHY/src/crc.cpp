#include "crc.h"

static uint32_t getPolynomial(CRCType type) {
    switch (type) {
        case CRCType::CRC24A: return 0x864CFB;  // x^24 + x^23 + x^18 + x^17 + ...
        case CRCType::CRC24C: return 0xB2B117;  // x^24 + x^23 + x^21 + x^20 + ...
        case CRCType::CRC16:  return 0x1021;     // x^16 + x^12 + x^5 + 1
        default: return 0;
    }
}

int getCRCLength(CRCType type) {
    switch (type) {
        case CRCType::CRC24A: return 24;
        case CRCType::CRC24C: return 24;
        case CRCType::CRC16:  return 16;
        default: return 0;
    }
}

std::vector<int> computeCRC(const std::vector<int>& data, CRCType type) {
    int crcLen = getCRCLength(type);
    uint32_t poly = getPolynomial(type);

    // Shift register implementation
    uint32_t reg = 0;
    uint32_t mask = (crcLen == 24) ? 0xFFFFFF : 0xFFFF;

    for (size_t i = 0; i < data.size(); i++) {
        uint32_t msb = (reg >> (crcLen - 1)) & 1;
        reg = ((reg << 1) | data[i]) & mask;
        if (msb) {
            reg ^= poly;
        }
    }

    // Process crcLen zero bits
    for (int i = 0; i < crcLen; i++) {
        uint32_t msb = (reg >> (crcLen - 1)) & 1;
        reg = (reg << 1) & mask;
        if (msb) {
            reg ^= poly;
        }
    }

    // Convert to bit vector (MSB first)
    std::vector<int> crcBits(crcLen);
    for (int i = 0; i < crcLen; i++) {
        crcBits[i] = (reg >> (crcLen - 1 - i)) & 1;
    }
    return crcBits;
}

std::vector<int> attachCRC(const std::vector<int>& data, CRCType type) {
    std::vector<int> crcBits = computeCRC(data, type);
    std::vector<int> result = data;
    result.insert(result.end(), crcBits.begin(), crcBits.end());
    return result;
}

bool checkCRC(const std::vector<int>& dataWithCRC, CRCType type) {
    int crcLen = getCRCLength(type);
    if ((int)dataWithCRC.size() <= crcLen) return false;

    int dataLen = dataWithCRC.size() - crcLen;
    std::vector<int> data(dataWithCRC.begin(), dataWithCRC.begin() + dataLen);
    std::vector<int> receivedCRC(dataWithCRC.begin() + dataLen, dataWithCRC.end());
    std::vector<int> computedCRC = computeCRC(data, type);

    for (int i = 0; i < crcLen; i++) {
        if (receivedCRC[i] != computedCRC[i]) return false;
    }
    return true;
}

std::vector<int> attachCRCWithRNTI(const std::vector<int>& data, CRCType type, uint16_t rnti) {
    std::vector<int> crcBits = computeCRC(data, type);
    int crcLen = getCRCLength(type);

    // XOR RNTI (16 bits) with the last 16 bits of CRC
    for (int i = 0; i < 16; i++) {
        int rntiBit = (rnti >> (15 - i)) & 1;
        crcBits[crcLen - 16 + i] ^= rntiBit;
    }

    std::vector<int> result = data;
    result.insert(result.end(), crcBits.begin(), crcBits.end());
    return result;
}

bool checkCRCWithRNTI(const std::vector<int>& dataWithCRC, CRCType type, uint16_t rnti) {
    int crcLen = getCRCLength(type);
    if ((int)dataWithCRC.size() <= crcLen) return false;

    int dataLen = dataWithCRC.size() - crcLen;
    std::vector<int> data(dataWithCRC.begin(), dataWithCRC.begin() + dataLen);
    std::vector<int> receivedCRC(dataWithCRC.begin() + dataLen, dataWithCRC.end());
    std::vector<int> computedCRC = computeCRC(data, type);

    // XOR RNTI with the last 16 bits of computed CRC before comparison
    for (int i = 0; i < 16; i++) {
        int rntiBit = (rnti >> (15 - i)) & 1;
        computedCRC[crcLen - 16 + i] ^= rntiBit;
    }

    for (int i = 0; i < crcLen; i++) {
        if (receivedCRC[i] != computedCRC[i]) return false;
    }
    return true;
}
