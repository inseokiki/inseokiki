#ifndef MCS_TABLE_H
#define MCS_TABLE_H

typedef enum {
    MCS_TABLE1 = 0,
    MCS_TABLE2 = 1
} MCSTableType;

typedef struct {
    int    index;
    char   modulation[8];   /* "QPSK","16QAM","64QAM","256QAM" */
    int    modulationOrder; /* Qm: 2,4,6,8 */
    double targetCodeRate;  /* R x 1024 */
    double spectralEff;     /* Qm * R */
} MCSEntry;

MCSEntry get_mcs_entry(int index, MCSTableType table);
double   get_code_rate(const MCSEntry *e);
int      get_max_mcs_index(MCSTableType table);

#endif
