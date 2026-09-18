#include "tdl_time.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failed;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); failed++; } } while (0)
int main(void) {
    TDLChannel ch; TDLTimeState state, again;
    cx_t a[TDL_MAX_TAPS],b[TDL_MAX_TAPS],c[TDL_MAX_TAPS];
    tdl_channel_init(&ch,'A',300,30000,10);
    CHECK(tdl_time_init(NULL,&ch,100)==-1);
    CHECK(tdl_time_init(&state,&ch,-1)==-1);
    CHECK(tdl_time_init(&state,&ch,NAN)==-1);
    rng_seed(901); CHECK(tdl_time_init(&state,&ch,100)==0);
    CHECK(tdl_time_sample(&state,-1,a)==-1);
    CHECK(tdl_time_sample(&state,INFINITY,a)==-1);
    CHECK(tdl_time_sample(&state,NAN,a)==-1);
    CHECK(tdl_time_sample(&state,.005,a)==0);
    CHECK(tdl_time_sample(&state,.001,b)==0);
    CHECK(tdl_time_sample(&state,.005,c)==0);
    CHECK(memcmp(a,c,ch.profile.num_taps*sizeof(cx_t))==0);
    double after_sample=randn();
    rng_seed(901); CHECK(tdl_time_init(&again,&ch,100)==0);
    CHECK(randn()==after_sample); /* sampling does not alter the RNG */
    CHECK(tdl_time_sample(&again,.005,c)==0);
    CHECK(memcmp(a,c,ch.profile.num_taps*sizeof(cx_t))==0);

    for (char profile='A'; profile<='E'; profile++) {
        tdl_channel_init(&ch,profile,300,30000,10);
        CHECK(tdl_time_init(&state,&ch,0)==0);
        CHECK(tdl_time_sample(&state,0,a)==0);
        CHECK(tdl_time_sample(&state,123,c)==0);
        CHECK(memcmp(a,c,ch.profile.num_taps*sizeof(cx_t))==0);
    }
    /* Pure LOS fixture isolates the specified peak from diffuse fading. */
    tdl_channel_init(&ch,'D',300,30000,10);
    for(int l=0;l<ch.profile.num_taps;l++) ch.profile.power_lin[l]=0;
    ch.profile.los_power_lin=1;
    CHECK(tdl_time_init(&state,&ch,100)==0);
    CHECK(tdl_time_sample(&state,0,a)==0);
    CHECK(tdl_time_sample(&state,1.0/280.0,b)==0);
    CHECK(cabs(b[0]-I*a[0])<1e-12);

    /* Ensemble, not one finite-oscillator realization's time average.
     * Independent Bessel values: J0(0.2*pi), J0(pi). */
    const double expected[]={0.9037126420924664,-0.3042421776440938};
    const int draws=4000;
    for(char profile='A';profile<='E';profile++) {
        tdl_channel_init(&ch,profile,300,30000,10);
        rng_seed(901+(unsigned)profile);
        double energy[TDL_MAX_TAPS]={0};
        cx_t cov[2]={0},mean=0;
        for(int n=0;n<draws;n++) {
            CHECK(tdl_time_init(&state,&ch,100)==0);
            CHECK(tdl_time_sample(&state,0,a)==0);
            CHECK(tdl_time_sample(&state,.001,b)==0);
            CHECK(tdl_time_sample(&state,.005,c)==0);
            for(int l=0;l<ch.profile.num_taps;l++) energy[l]+=CX_NORM(a[l]);
            /* tap 1 has no LOS in any supported profile. */
            cov[0]+=b[1]*conj(a[1]);cov[1]+=c[1]*conj(a[1]);mean+=a[1];
        }
        for(int l=0;l<ch.profile.num_taps;l++) {
            double target=ch.profile.power_lin[l]+((l==0&&ch.profile.has_los)?ch.profile.los_power_lin:0);
            CHECK(fabs(energy[l]/draws/target-1)<.08);
        }
        CHECK(cabs(mean/draws)/sqrt(ch.profile.power_lin[1])<.06);
        for(int j=0;j<2;j++) CHECK(cabs(cov[j]/draws/ch.profile.power_lin[1]-expected[j])<.07);
    }
    puts(failed ? "TDL time tests FAIL" : "TDL time: reproducibility, zero Doppler, LOS rotation, profile powers and covariance PASS");
    return failed?1:0;
}
