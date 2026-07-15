/* ================================================================
 *  mcs_table.c
 *  TS 38.214 MCS index -> modulation/code-rate tables (3 variants)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "mcs_table.h"
#include <string.h>

/* TS 38.214 Table 5.1.3.1-1: PDSCH MCS index table 1 (QPSK/16QAM/64QAM)
 * Format: { MCS_index, Qm, target_code_rate_x1024 } */
static const int table1[][3] = {
    { 0, 2,  120}, { 1, 2,  157}, { 2, 2,  193}, { 3, 2,  251},
    { 4, 2,  308}, { 5, 2,  379}, { 6, 2,  449}, { 7, 2,  526},
    { 8, 2,  602}, { 9, 2,  679},
    {10, 4,  340}, {11, 4,  378}, {12, 4,  434}, {13, 4,  490},
    {14, 4,  553}, {15, 4,  616}, {16, 4,  658},
    {17, 6,  438}, {18, 6,  466}, {19, 6,  517}, {20, 6,  567},
    {21, 6,  616}, {22, 6,  666}, {23, 6,  719}, {24, 6,  772},
    {25, 6,  822}, {26, 6,  873}, {27, 6,  910}, {28, 6,  948},
};
static const int table1_size = 29;

/* TS 38.214 Table 5.1.3.1-2: PDSCH MCS index table 2 (QPSK/16QAM/64QAM/256QAM)
 * Used when higher-layer parameter mcs-Table = 'qam256' */
static const int table2[][3] = {
    { 0, 2,  120}, { 1, 2,  193}, { 2, 2,  308}, { 3, 2,  449},
    { 4, 2,  602},
    { 5, 4,  378}, { 6, 4,  434}, { 7, 4,  490}, { 8, 4,  553},
    { 9, 4,  616}, {10, 4,  658},
    {11, 6,  466}, {12, 6,  517}, {13, 6,  567}, {14, 6,  616},
    {15, 6,  666}, {16, 6,  719}, {17, 6,  772}, {18, 6,  822},
    {19, 6,  873},
    {20, 8,  682}, {21, 8,  711}, {22, 8,  754}, {23, 8,  797},
    {24, 8,  841}, {25, 8,  885}, {26, 8,  916}, {27, 8,  948},
};
static const int table2_size = 28;

/* TS 38.214 Table 5.1.3.1-3: PDSCH MCS index table 3 (QPSK/16QAM/64QAM, low SE)
 * Used for scenarios requiring lower spectral efficiency (e.g., RedCap) */
static const int table3[][3] = {
    { 0, 2,   30}, { 1, 2,   40}, { 2, 2,   50}, { 3, 2,   64},
    { 4, 2,   84}, { 5, 2,  109}, { 6, 2,  120}, { 7, 2,  157},
    { 8, 2,  193}, { 9, 2,  251}, {10, 2,  308}, {11, 2,  379},
    {12, 2,  449}, {13, 2,  526}, {14, 2,  602},
    {15, 4,  340}, {16, 4,  378}, {17, 4,  434}, {18, 4,  490},
    {19, 4,  553}, {20, 4,  616},
    {21, 6,  438}, {22, 6,  466}, {23, 6,  517}, {24, 6,  567},
    {25, 6,  616}, {26, 6,  666}, {27, 6,  719}, {28, 6,  772},
};
static const int table3_size = 29;

static const char *mod_order_name(int qm) {
    switch (qm) {
        case 2: return "QPSK";
        case 4: return "16QAM";
        case 6: return "64QAM";
        case 8: return "256QAM";
        default: return "QPSK";
    }
}

MCSTableType mcs_table_from_str(const char *s) {
    if (!s)                        return MCS_TABLE1;
    if (strcmp(s, "TABLE2") == 0)  return MCS_TABLE2;
    if (strcmp(s, "TABLE3") == 0)  return MCS_TABLE3;
    return MCS_TABLE1;
}

MCSEntry get_mcs_entry(int index, MCSTableType table) {
    const int (*tbl)[3];
    int sz;
    switch (table) {
        case MCS_TABLE2: tbl = table2; sz = table2_size; break;
        case MCS_TABLE3: tbl = table3; sz = table3_size; break;
        default:         tbl = table1; sz = table1_size; break;
    }
    if (index < 0)   index = 0;
    if (index >= sz) index = sz - 1;

    MCSEntry e;
    e.index           = tbl[index][0];
    e.modulationOrder = tbl[index][1];
    e.targetCodeRate  = tbl[index][2];
    strncpy(e.modulation, mod_order_name(e.modulationOrder), sizeof(e.modulation) - 1);
    e.modulation[sizeof(e.modulation) - 1] = '\0';
    e.spectralEff = e.modulationOrder * (e.targetCodeRate / 1024.0);
    return e;
}

double get_code_rate(const MCSEntry *e) { return e->targetCodeRate / 1024.0; }

int get_max_mcs_index(MCSTableType table) {
    switch (table) {
        case MCS_TABLE2: return table2_size - 1;
        case MCS_TABLE3: return table3_size - 1;
        default:         return table1_size - 1;
    }
}
