#include "mcs_table.h"

// TS 38.214 Table 5.1.3.1-1: MCS index table 1 for PDSCH (up to 64QAM)
// Columns: MCS index, Modulation order Qm, Target code rate R x 1024
static const int table1[][3] = {
    { 0, 2,  120},  // QPSK
    { 1, 2,  157},
    { 2, 2,  193},
    { 3, 2,  251},
    { 4, 2,  308},
    { 5, 2,  379},
    { 6, 2,  449},
    { 7, 2,  526},
    { 8, 2,  602},
    { 9, 2,  679},
    {10, 4,  340},  // 16QAM
    {11, 4,  378},
    {12, 4,  434},
    {13, 4,  490},
    {14, 4,  553},
    {15, 4,  616},
    {16, 4,  658},
    {17, 6,  438},  // 64QAM
    {18, 6,  466},
    {19, 6,  517},
    {20, 6,  567},
    {21, 6,  616},
    {22, 6,  666},
    {23, 6,  719},
    {24, 6,  772},
    {25, 6,  822},
    {26, 6,  873},
    {27, 6,  910},
    {28, 6,  948},
};
static const int table1_size = 29;

// TS 38.214 Table 5.1.3.1-2: MCS index table 2 for PDSCH (up to 256QAM)
static const int table2[][3] = {
    { 0, 2,  120},  // QPSK
    { 1, 2,  193},
    { 2, 2,  308},
    { 3, 2,  449},
    { 4, 2,  602},
    { 5, 4,  378},  // 16QAM
    { 6, 4,  434},
    { 7, 4,  490},
    { 8, 4,  553},
    { 9, 4,  616},
    {10, 4,  658},
    {11, 6,  466},  // 64QAM
    {12, 6,  517},
    {13, 6,  567},
    {14, 6,  616},
    {15, 6,  666},
    {16, 6,  719},
    {17, 6,  772},
    {18, 6,  822},
    {19, 6,  873},
    {20, 8,  682},  // 256QAM
    {21, 8,  711},
    {22, 8,  754},
    {23, 8,  797},
    {24, 8,  841},
    {25, 8,  885},
    {26, 8,  916},
    {27, 8,  948},
};
static const int table2_size = 28;

static std::string modOrderToName(int qm) {
    switch (qm) {
        case 2: return "QPSK";
        case 4: return "16QAM";
        case 6: return "64QAM";
        case 8: return "256QAM";
        default: return "QPSK";
    }
}

MCSEntry getMCSEntry(int index, MCSTableType table) {
    MCSEntry entry;

    const int (*tbl)[3] = nullptr;
    int tblSize = 0;

    if (table == MCSTableType::TABLE1) {
        tbl = table1;
        tblSize = table1_size;
    } else {
        tbl = table2;
        tblSize = table2_size;
    }

    // Clamp index
    if (index < 0) index = 0;
    if (index >= tblSize) index = tblSize - 1;

    entry.index = tbl[index][0];
    entry.modulationOrder = tbl[index][1];
    entry.targetCodeRate = tbl[index][2];
    entry.modulation = modOrderToName(entry.modulationOrder);
    entry.spectralEfficiency = entry.modulationOrder * (entry.targetCodeRate / 1024.0);

    return entry;
}

double getCodeRate(const MCSEntry& entry) {
    return entry.targetCodeRate / 1024.0;
}

int getMaxMCSIndex(MCSTableType table) {
    if (table == MCSTableType::TABLE1) return table1_size - 1;
    return table2_size - 1;
}
