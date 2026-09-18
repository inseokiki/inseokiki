/* Independent position vectors cover puncture, RV, filler, Qm permutation,
 * repetition, feedback and HARQ scatter. No Python dependency at test time. */
#include "nr_rate_matching.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "reference_vectors/sch_rate_matching_reference_vectors.h"

int main(void) {
    int failures=0;
    double ir_buffer[2][4][2][544]={0}, ir_expected[2][4][2][544]={0};
    for (size_t v=0;v<sizeof(sch_refs)/sizeof(sch_refs[0]);v++) {
        int n=sch_refs[v].n, e=sch_refs[v].e;
        int *coded=malloc(n*sizeof(int)), *wire=malloc(e*sizeof(int));
        double *feedback=malloc(n*sizeof(double)), *soft=malloc(e*sizeof(double));
        double *llr=malloc(e*sizeof(double));
        double *buffer=calloc(n,sizeof(double)), *expected=calloc(n,sizeof(double));
        for (int i=0;i<n;i++) {
            coded[i]=((i*37)^(i>>2)^(i>>5))&1;
            feedback[i]=i+0.25;
        }
        for (int j=0;j<e;j++) {
            llr[j]=(j%2 ? -1 : 1)*(j+1)*0.125;
            expected[sch_refs[v].positions[j]]+=llr[j];
        }
        nr_sch_rate_match_select(coded,n,sch_refs[v].bg,sch_refs[v].z,
            sch_refs[v].start,sch_refs[v].end,sch_refs[v].rv,e,sch_refs[v].qm,wire);
        nr_sch_rate_match_select_soft(feedback,n,sch_refs[v].bg,sch_refs[v].z,
            sch_refs[v].start,sch_refs[v].end,sch_refs[v].rv,e,sch_refs[v].qm,soft);
        int good=1;
        for (int j=0;j<e;j++) {
            int idx=sch_refs[v].positions[j];
            if (wire[j]!=coded[idx] || soft[j]!=feedback[idx]) good=0;
        }
        for (int attempt=1;attempt<=2;attempt++) {
            nr_sch_rate_match_combine(buffer,n,sch_refs[v].bg,sch_refs[v].z,
                sch_refs[v].start,sch_refs[v].end,sch_refs[v].rv,e,sch_refs[v].qm,llr);
            for (int i=0;i<n;i++) if (buffer[i]!=attempt*expected[i]) good=0;
        }
        double *ir=ir_buffer[sch_refs[v].bg-1][sch_refs[v].qm/2-1][v%2];
        double *ref=ir_expected[sch_refs[v].bg-1][sch_refs[v].qm/2-1][v%2];
        for (int j=0;j<e;j++) ref[sch_refs[v].positions[j]]+=llr[j];
        nr_sch_rate_match_combine(ir,n,sch_refs[v].bg,sch_refs[v].z,
            sch_refs[v].start,sch_refs[v].end,sch_refs[v].rv,e,sch_refs[v].qm,llr);
        for (int i=0;i<n;i++) if (ir[i]!=ref[i]) good=0;
        if (!good) { fprintf(stderr,"FAIL external SCH vector %zu\n",v); failures++; }
        free(coded);free(wire);free(feedback);free(soft);free(llr);free(buffer);free(expected);
    }
    const int *refs[]={sch_er_qm2,sch_er_qm4,sch_er_qm6,sch_er_qm8};
    for (int q=0;q<4;q++) {
        int qm=2*(q+1), lengths[3];
        nr_ldpc_er_alloc(101*qm,1,qm,3,lengths);
        for (int c=0;c<3;c++) if(lengths[c]!=refs[q][c]) failures++;
    }
    printf("SCH external position vectors: 64; unequal-CB allocation: 4; failures: %d\n",failures);
    return failures ? 1 : 0;
}
