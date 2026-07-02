#ifndef MCS_TABLE_H
#define MCS_TABLE_H

/* TS 38.214 Table 5.1.3.1-1/2/3 */
typedef enum {
    MCS_TABLE1 = 0,  /* Table 5.1.3.1-1: QPSK/16QAM/64QAM              */
    MCS_TABLE2 = 1,  /* Table 5.1.3.1-2: QPSK/16QAM/64QAM/256QAM       */
    MCS_TABLE3 = 2   /* Table 5.1.3.1-3: QPSK/16QAM/64QAM (low SE)     */
} MCSTableType;

typedef struct {
    int    index;
    char   modulation[8];   /* "QPSK","16QAM","64QAM","256QAM" */
    int    modulationOrder; /* Qm: 2,4,6,8 */
    double targetCodeRate;  /* R x 1024 */
    double spectralEff;     /* Qm * (R x 1024) / 1024 */
} MCSEntry;

MCSTableType mcs_table_from_str(const char *s);
MCSEntry     get_mcs_entry(int index, MCSTableType table);
double       get_code_rate(const MCSEntry *e);
int          get_max_mcs_index(MCSTableType table);

#endif
