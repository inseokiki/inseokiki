/* Noisy UL FDM pilot component fixture. Tests estimation noise and edge
 * bias, not standard DMRS allocation or the driver's inline LS loop. */
#include "dmrs.h"
#include "channel_estimation.h"
#include "utils.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    const int trials = 20000;
    const double noises[] = {0.02, 0.5};
    int failures = 0, cases = 0;
    double worst_relative_error = 0.0;
    for (int rank = 1; rank <= 4; rank++) {
        for (int layer = 0; layer < rank; layer++) {
            int np = (6 + rank - 1 - layer)/rank, pos[6];
            cx_t pilot[6];
            for (int p = 0; p < np; p++) pos[p] = 2*(p*rank+layer);
            dmrs_sequence(0x87654321u+(unsigned)layer*0x10203u, np, pilot);
            for (int n = 0; n < 2; n++) {
                double N0 = noises[n], sigma = sqrt(N0/2.0);
                double sums[12] = {0}, mean_mse = 0.0;
                cx_t h0 = CX_MAKE(0.8, -0.3), slope = CX_MAKE(0.02, -0.01);
                rng_seed(12345+(unsigned)(rank*100+layer*10+n));
                for (int t = 0; t < trials; t++) {
                    cx_t rx[6], hp[6], full[12], avg = 0.0;
                    for (int p = 0; p < np; p++)
                        rx[p] = (h0+slope*pos[p])*pilot[p]
                              + CX_MAKE(sigma*randn(), sigma*randn());
                    ls_estimate(rx, pilot, np, hp);
                    interpolate_channel(hp, np, pos, 12, full);
                    for (int k = 0; k < 12; k++)
                        sums[k] += CX_NORM(full[k]-(h0+slope*k));
                    /* Remove the known slope to isolate flat pilot averaging. */
                    for (int p = 0; p < np; p++) avg += hp[p]-slope*pos[p];
                    mean_mse += CX_NORM(avg/np-h0);
                }
                double expected_mean = 0.0;
                for (int p = 0; p < np; p++) expected_mean += N0/CX_NORM(pilot[p])/(np*np);
                double rel = fabs(mean_mse/trials/expected_mean-1.0);
                if (rel > worst_relative_error) worst_relative_error = rel;
                if (!isfinite(rel) || rel > 0.05) failures++;
                for (int k = 0; k < 12; k++) {
                    /* Independent elementary variance: independent pilot errors
                     * add with squared interpolation weights; edge hold adds
                     * deterministic bias for this known affine channel. */
                    int left = 0, right = np-1;
                    for (int p = 0; p < np; p++) {
                        if (pos[p] <= k) left = p;
                        if (pos[p] >= k) { right = p; break; }
                    }
                    double expected;
                    if (k <= pos[0] || k >= pos[np-1]) {
                        int p = k <= pos[0] ? 0 : np-1;
                        expected = N0/CX_NORM(pilot[p])+CX_NORM(slope*(pos[p]-k));
                    } else {
                        double b = left == right ? 0.0 :
                            (double)(k-pos[left])/(pos[right]-pos[left]);
                        expected = (1-b)*(1-b)*N0/CX_NORM(pilot[left]);
                        if (left != right) expected += b*b*N0/CX_NORM(pilot[right]);
                    }
                    rel = fabs(sums[k]/trials/expected-1.0);
                    if (rel > worst_relative_error) worst_relative_error = rel;
                    if (!isfinite(rel) || rel > 0.05) {
                        fprintf(stderr,"FAIL rank=%d layer=%d N0=%g RE=%d relative_error=%g\n", rank,layer,N0,k,rel);
                        failures++;
                    }
                }
                cases++;
            }
        }
    }
    printf("UL pilot noise: %d cases, %d draws/case, worst MSE relative error %.4f (limit 0.05): %s\n",
           cases,trials,worst_relative_error,failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
