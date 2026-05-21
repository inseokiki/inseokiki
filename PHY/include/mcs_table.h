#ifndef MCS_TABLE_H
#define MCS_TABLE_H

#include <string>

enum class MCSTableType {
    TABLE1,  // TS 38.214 Table 5.1.3.1-1 (64QAM, MCS 0-28)
    TABLE2   // TS 38.214 Table 5.1.3.1-2 (256QAM, MCS 0-27)
};

struct MCSEntry {
    int index;
    std::string modulation;     // "QPSK", "16QAM", "64QAM", "256QAM"
    int modulationOrder;        // Qm: 2, 4, 6, 8
    double targetCodeRate;      // R x 1024
    double spectralEfficiency;  // Qm * R
};

// Get MCS entry for given index and table
// Returns default entry if index is out of range
MCSEntry getMCSEntry(int index, MCSTableType table);

// Get actual code rate (R) from target code rate (R x 1024)
double getCodeRate(const MCSEntry& entry);

// Get max MCS index for a table
int getMaxMCSIndex(MCSTableType table);

#endif
