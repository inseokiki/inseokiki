#ifndef CRC_H
#define CRC_H

#include <vector>
#include <cstdint>

enum class CRCType {
    CRC24A,  // PDSCH TB: polynomial 0x864CFB, 24 bits
    CRC24C,  // PBCH/PDCCH: polynomial 0xB2B117, 24 bits
    CRC16    // PDSCH large TB: polynomial 0x1021, 16 bits
};

// Get the CRC length in bits for a given type
int getCRCLength(CRCType type);

// Compute CRC and return the CRC bits
std::vector<int> computeCRC(const std::vector<int>& data, CRCType type);

// Attach CRC bits to the end of data, returns data + CRC
std::vector<int> attachCRC(const std::vector<int>& data, CRCType type);

// Check CRC: returns true if CRC is valid (data includes CRC at the end)
bool checkCRC(const std::vector<int>& dataWithCRC, CRCType type);

// Attach CRC with RNTI masking (for PDCCH)
// XORs the RNTI (16 bits) with the last 16 bits of the 24-bit CRC
std::vector<int> attachCRCWithRNTI(const std::vector<int>& data, CRCType type, uint16_t rnti);

// Check CRC with RNTI de-masking (for PDCCH blind decoding)
bool checkCRCWithRNTI(const std::vector<int>& dataWithCRC, CRCType type, uint16_t rnti);

#endif
