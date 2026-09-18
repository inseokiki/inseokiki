#include "tdl_mimo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failed;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failed++;}}while(0)
int main(void) {
    cx_t taps[4][4][TDL_MAX_TAPS]={{{0}}},copy[4][4][TDL_MAX_TAPS];
    taps[0][0][0]=1+I;memcpy(copy,taps,sizeof(taps));
    CHECK(tdl_mimo_correlate_4x4(taps,1,0,0)==0);
    CHECK(memcmp(taps,copy,sizeof(taps))==0);
    CHECK(tdl_mimo_correlate_4x4(taps,1,.5,.3)==0);
    for(int r=0;r<4;r++) for(int t=0;t<4;t++)
        CHECK(cabs(taps[r][t][0]-(1+I)*pow(.3,r)*pow(.5,t))<1e-12);
    CHECK(tdl_mimo_correlate_4x4(taps,1,1,0)==-1);
    CHECK(tdl_mimo_correlate_4x4(taps,1,NAN,0)==-1);
    CHECK(tdl_mimo_correlate_4x4(taps,0,0,0)==-1);
    const int draws=12000;
    const double settings[][2]={{.7,0},{0,.6},{.7,.6}};
    for(int setting=0;setting<3;setting++) {
        double tx=settings[setting][0],rx=settings[setting][1];
        cx_t cov[16][16]={{0}};
        rng_seed(901+setting);
        for(int n=0;n<draws;n++) {
            for(int r=0;r<4;r++) for(int t=0;t<4;t++)
                taps[r][t][0]=CX_MAKE(randn(),randn())/sqrt(2.0);
            CHECK(tdl_mimo_correlate_4x4(taps,1,tx,rx)==0);
            for(int i=0;i<16;i++) for(int j=0;j<16;j++)
                cov[i][j]+=taps[i/4][i%4][0]*conj(taps[j/4][j%4][0]);
        }
        for(int i=0;i<16;i++) for(int j=0;j<16;j++) {
            double expected=pow(rx,abs(i/4-j/4))*pow(tx,abs(i%4-j%4));
            CHECK(cabs(cov[i][j]/draws-expected)<.05);
        }
    }
    puts(failed?"TDL MIMO FAIL":"TDL MIMO no-op, impulse and full 16x16 covariance PASS");
    return failed?1:0;
}
