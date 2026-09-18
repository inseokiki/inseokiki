#include "tdl_mimo.h"
#include <math.h>

/* Closed-form factor of R[i,j]=rho^abs(i-j), including rho=0 identity. */
static void exponential_factor(double rho, double L[4][4]) {
    double scale=sqrt(1.0-rho*rho);
    for(int i=0;i<4;i++) {
        L[i][0]=pow(rho,i);
        for(int j=1;j<4;j++) L[i][j]=j<=i ? pow(rho,i-j)*scale : 0.0;
    }
}
int tdl_mimo_correlate_4x4(cx_t taps[4][4][TDL_MAX_TAPS], int count,
                          double tx, double rx) {
    if(!taps || count<1 || count>TDL_MAX_TAPS || !isfinite(tx) ||
       !isfinite(rx) || tx<0 || tx>=1 || rx<0 || rx>=1) return -1;
    if(tx==0.0 && rx==0.0) return 0;
    double Lt[4][4],Lr[4][4];
    exponential_factor(tx,Lt);exponential_factor(rx,Lr);
    for(int l=0;l<count;l++) {
        cx_t temp[4][4]={{0}},result[4][4]={{0}};
        for(int r=0;r<4;r++)
            for(int t=0;t<4;t++)
                for(int j=0;j<4;j++) temp[r][t]+=Lr[r][j]*taps[j][t][l];
        for(int r=0;r<4;r++)
            for(int t=0;t<4;t++)
                for(int j=0;j<4;j++) result[r][t]+=temp[r][j]*Lt[t][j];
        for(int r=0;r<4;r++)
            for(int t=0;t<4;t++) taps[r][t][l]=result[r][t];
    }
    return 0;
}
