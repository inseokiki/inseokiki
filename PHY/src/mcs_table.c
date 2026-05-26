#include "mcs_table.h"
#include <string.h>

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

static const char *mod_order_name(int qm) {
    switch (qm) {
        case 2: return "QPSK";
        case 4: return "16QAM";
        case 6: return "64QAM";
        case 8: return "256QAM";
        default: return "QPSK";
    }
}

MCSEntry get_mcs_entry(int index, MCSTableType table) {
    const int (*tbl)[3] = (table == MCS_TABLE2) ? table2 : table1;
    int sz               = (table == MCS_TABLE2) ? table2_size : table1_size;
    MCSEntry e;
    if (index < 0) index = 0;
    if (index >= sz) index = sz - 1;
    e.index          = tbl[index][0];
    e.modulationOrder = tbl[index][1];
    e.targetCodeRate  = tbl[index][2];
    strncpy(e.modulation, mod_order_name(e.modulationOrder), sizeof(e.modulation)-1);
    e.modulation[sizeof(e.modulation)-1] = '\0';
    e.spectralEff = e.modulationOrder * (e.targetCodeRate / 1024.0);
    return e;
}

double get_code_rate(const MCSEntry *e) { return e->targetCodeRate / 1024.0; }

int get_max_mcs_index(MCSTableType table) {
    return (table == MCS_TABLE2) ? table2_size - 1 : table1_size - 1;
}
