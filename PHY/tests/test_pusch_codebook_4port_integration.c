/* Integration contract test: execute the production driver with test-only
 * seams for fixed rank, controlled CRC rejection, and observations at the LLR/HARQ
 * boundaries. Channel, estimator, detector, demapper, and LDPC stay real.
 * This checks wiring/state, not independent PHY reference performance. */
#include "pusch.h"
#include "pusch_codebook_4port.h"
#include "modulation.h"
#include "nr_rate_matching.h"
#include "crc.h"
#include "nr_sch.h"
#include "tdl_time.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, fixed_rank, tdl_case, chase_case, expected_attempts;
static int demap_calls, combine_calls, varied_calls;
static int cb_count = 1, ack_attempt; /* 0: force NACK, otherwise real CRC */
static double *saved_soft[16];
static double *buffer_addr[16];
static int saved_size[16];
static int sequence_mode, tb_seen, trial_rank, sequence_attempts;
static void finish_tb(void);

static void observe_seg(int B, double rate, NRSegInfo *seg) {
    if (sequence_mode && tb_seen) finish_tb();
    if (sequence_mode) {
        const int ack_schedule[] = {0, 1, 2, 1};
        ack_attempt = sequence_mode == 1 ? ack_schedule[tb_seen % 4] : 0;
        expected_attempts = ack_attempt ? ack_attempt : 4;
        trial_rank = fixed_rank;
        demap_calls = combine_calls = varied_calls = 0;
        tb_seen++;
    }
    nr_seg_compute(B, rate, seg);
    cb_count = seg->C;
    if (cb_count > 16) abort();
}
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; \
} } while (0)

static void fixed_select(const cx_t H[4][4], double N0, int *rank, int *tpmi) {
    (void)H; (void)N0;
    if (sequence_mode) {
        const int ranks[] = {4, 1, 4, 2};
        fixed_rank = ranks[tb_seen % 4];
    }
    *rank = fixed_rank; *tpmi = 0;
}
static int force_nack(const int *bits, int n, CRCType type) {
    if (!ack_attempt || combine_calls <= (ack_attempt - 1) * cb_count) return 0;
    return check_crc(bits, n, type);
}
static void observe_demap(const cx_t *syms, int n, const char *mod,
                          const double *nv, double *llr) {
    int varied = 0;
    for (int i = 0; i < n; i++) {
        REQUIRE(isfinite(nv[i]) && nv[i] > 0.0);
        if (fabs(nv[i] - nv[0]) > 1e-8 * fmax(nv[i], nv[0])) varied = 1;
    }
    if (varied) varied_calls++;
    if (!tdl_case) REQUIRE(!varied);
    demap_calls++;
    qam_demap_llr_re(syms, n, mod, nv, llr);
}
static void observe_combine(double *buf, int Ncb, int bg, int Zc,
                            int start, int end, int rv, int e, int Qm,
                            const double *llr) {
    const int ir[] = {0, 2, 3, 1};
    int cb = combine_calls % cb_count;
    int attempt = combine_calls / cb_count;
    REQUIRE(attempt < expected_attempts);
    REQUIRE(rv == (chase_case ? 0 : ir[attempt % 4]));
    if (attempt == 0) {
        for (int i = 0; i < Ncb; i++) REQUIRE(buf[i] == 0.0);
        buffer_addr[cb] = buf;
        for (int c = 0; c < cb; c++) REQUIRE(buffer_addr[c] != buf);
        saved_soft[cb] = malloc((size_t)Ncb * sizeof(double));
        saved_size[cb] = Ncb;
    } else {
        REQUIRE(buffer_addr[cb] == buf);
        REQUIRE(saved_size[cb] == Ncb);
        REQUIRE(memcmp(buf, saved_soft[cb], (size_t)Ncb * sizeof(double)) == 0);
    }
    nr_sch_rate_match_combine(buf, Ncb, bg, Zc, start, end, rv, e, Qm, llr);
    for (int i=0; i<2*Zc; i++) REQUIRE(buf[i] == 0.0);
    memcpy(saved_soft[cb], buf, (size_t)Ncb * sizeof(double));
    combine_calls++;
}

static int time_init_calls, time_sample_calls;
static int observe_time_init(TDLTimeState *state, const TDLChannel *ch, double fd) {
    time_init_calls++;
    return tdl_time_init(state,ch,fd);
}
static int observe_time_sample(const TDLTimeState *state, double time_s, cx_t *taps) {
    /* 16 antenna pairs each attempt, four attempts per forced-NACK TB. */
    REQUIRE(fabs(time_s-((time_sample_calls/16)%4)*0.001)<1e-15);
    time_sample_calls++;
    return tdl_time_sample(state,time_s,taps);
}

/* Compile the actual driver with local boundary observers. No production
 * hooks or linker-specific --wrap dependency, so this also runs on macOS. */
#define tdl_time_init observe_time_init
#define tdl_time_sample observe_time_sample
#define nr_seg_compute observe_seg
#define ul_cb4_select fixed_select
#define check_crc force_nack
#define qam_demap_llr_re observe_demap
#define nr_sch_rate_match_combine observe_combine
#include "../src/pusch_codebook_4port_sim.c"
#undef tdl_time_init
#undef tdl_time_sample
#undef nr_seg_compute
#undef ul_cb4_select
#undef check_crc
#undef qam_demap_llr_re
#undef nr_sch_rate_match_combine

static void release_snapshots(void) {
    for (int c = 0; c < 16; c++) {
        free(saved_soft[c]); saved_soft[c] = NULL;
        buffer_addr[c] = NULL;
    }
}

static void finish_tb(void) {
    REQUIRE(combine_calls == expected_attempts * cb_count);
    REQUIRE(demap_calls == trial_rank * expected_attempts);
    if (tdl_case) REQUIRE(varied_calls == demap_calls);
    if (sequence_mode == 1) REQUIRE(trial_rank == 4 ? cb_count > 1 : cb_count == 1);
    sequence_attempts += combine_calls / cb_count;
    release_snapshots();
}

/* Component composition fixture, independent of the stochastic driver:
 * known diagonal physical channel -> actual UL precoder -> noiseless FDM
 * pilots -> interpolation -> the driver's actual rank-specific detector.
 * This is not a standard DMRS allocation/conformance reference. */
static void test_precoded_detection(void) {
    int candidates = 0;
    for (int rank = 1; rank <= 4; rank++) {
        for (int tpmi = 0; tpmi < ul_cb4_tpmi_count(rank); tpmi++) {
            cx_t W[4][4], h[4][4] = {{0}};
            REQUIRE(ul_cb4_precoder(rank, tpmi, W) == 0);
            double beta = ul_cb4_unit_power_beta((const cx_t (*)[4])W, rank);
            for (int r = 0; r < 4; r++)
                for (int l = 0; l < rank; l++)
                    h[r][l] = CX_MAKE(0.7 + 0.4*r, 0.2 - 0.3*r) * W[r][l] * beta;
            /* One RB includes unequal pilot counts (2,2,1,1) for rank 4,
             * and checks nearest-pilot extrapolation at grid boundaries. */
            int pp[6], dp[6];
            dmrs_pilot_indices(1, pp); dmrs_data_indices(1, dp);
            cx_t estimated[4][4][12] = {{{0}}};
            for (int l = 0; l < rank; l++) {
                int np = (6 + rank - 1 - l) / rank, positions[6];
                cx_t pilots[6], hp[6];
                dmrs_sequence(0x87654321u + (unsigned)l*0x10203u, np, pilots);
                for (int p = 0; p < np; p++) positions[p] = pp[p*rank+l];
                for (int r = 0; r < 4; r++) {
                    for (int p = 0; p < np; p++) hp[p] = h[r][l]*pilots[p]/pilots[p];
                    interpolate_channel(hp, np, positions, 12, estimated[r][l]);
                    for (int k = 0; k < 12; k++) REQUIRE(cabs(estimated[r][l][k]-h[r][l]) < 1e-12);
                }
            }
            for (int d = 0; d < 6; d++) {
                cx_t tx[4], y[4] = {0}, hh[4][4] = {{0}}, xh[4]; double nv[4];
                for (int l = 0; l < rank; l++) tx[l] = CX_MAKE((l+d)%2 ? -0.7 : 0.7, l%2 ? 0.7 : -0.7);
                for (int r = 0; r < 4; r++)
                    for (int l = 0; l < rank; l++) {
                        y[r] += h[r][l]*tx[l]; hh[r][l] = estimated[r][l][dp[d]];
                    }
                detect_re(rank, hh, y, 1e-10, xh, nv);
                for (int l = 0; l < rank; l++) REQUIRE(cabs(xh[l]-tx[l]) < 1e-7);
            }
            /* Measure the detector's linear response D with receiver basis
             * vectors. For independent unit-energy layers, output residual
             * energy is sum_{j!=l}|(D H)[l,j]|² + N0 sum_r|D[l,r]|².
             * No production inverse or alpha formula is reused here. */
            const double noises[] = {0.001, 0.1, 10.0};
            for (int n = 0; n < 3; n++) {
                cx_t D[4][4] = {{0}}, xh[4]; double nv[4];
                for (int r = 0; r < 4; r++) {
                    cx_t basis[4] = {0}; basis[r] = 1.0;
                    detect_re(rank, h, basis, noises[n], xh, nv);
                    for (int l = 0; l < rank; l++) D[l][r] = xh[l];
                }
                for (int l = 0; l < rank; l++) {
                    double energy = 0.0;
                    for (int r = 0; r < 4; r++) energy += noises[n]*CX_NORM(D[l][r]);
                    for (int j = 0; j < rank; j++) {
                        cx_t gain = 0.0;
                        for (int r = 0; r < 4; r++) gain += D[l][r]*h[r][j];
                        if (j == l) REQUIRE(cabs(gain-1.0) < 1e-9);
                        else energy += CX_NORM(gain);
                    }
                    REQUIRE(isfinite(nv[l]) && nv[l] > 0.0);
                    REQUIRE(fabs(nv[l]-energy) < 1e-9*fmax(1.0, energy));
                }
            }
            candidates++;
        }
    }
    REQUIRE(candidates == 62);
    puts("UL receiver: 62 TPMIs, noiseless pilot recovery and residual-energy checks complete");
}

int main(void) {
    test_precoded_detection();
    for (fixed_rank = 1; fixed_rank <= 4; fixed_rank++) {
        for (tdl_case = 0; tdl_case <= 1; tdl_case++) {
            for (int mode = 0; mode < 3; mode++) {
                L1Config cfg = {0};
                cfg.numRB = 4; cfg.scsKHz = 30;
                cfg.numTrials = 1; cfg.mcsIndex = 5;
                cfg.snrStart = cfg.snrEnd = 10.0; cfg.snrStep = 1.0;
                cfg.harqEnable = mode != 0; cfg.harqMaxRetx = 4;
                cfg.tdlDelaySpreadNs = 300.0;
                strcpy(cfg.mcsTableType, "TABLE1");
                strcpy(cfg.channelModel, tdl_case ? "TDL" : "FLAT_FADING");
                strcpy(cfg.tdlProfile, "A");
                strcpy(cfg.harqRvSeq, mode == 2 ? "CHASE" : "IR");
                chase_case = mode == 2;
                expected_attempts = mode == 0 ? 1 : 4;
                demap_calls = combine_calls = varied_calls = 0;
                rng_seed(12345);
                run_pusch_ul_cb_4port_extended_simulation(&cfg);
                REQUIRE(cb_count == 1);
                release_snapshots();
                /* Small TB ensures exactly one CB per attempt. */
                REQUIRE(combine_calls == expected_attempts);
                REQUIRE(demap_calls == fixed_rank * expected_attempts);
                if (tdl_case) REQUIRE(varied_calls == demap_calls);
            }
        }
    }
    /* Real CRC at high SNR verifies actual payload reconstruction and ACK
     * early exit, including after a deliberately rejected first attempt.
     * Large TBS selects multiple CBs through the real segmentation API. */
    for (int multi = 0; multi < 2; multi++) {
        for (ack_attempt = 1; ack_attempt <= 2; ack_attempt++) {
            for (chase_case = 0; chase_case <= 1; chase_case++) {
                L1Config cfg = {0};
                fixed_rank = 4; tdl_case = 0;
                cfg.numRB = multi ? 100 : 4; cfg.scsKHz = 30;
                cfg.numTrials = 1; cfg.mcsIndex = multi ? 27 : 5;
                cfg.snrStart = cfg.snrEnd = 60.0; cfg.snrStep = 1.0;
                cfg.harqEnable = 1; cfg.harqMaxRetx = 4;
                strcpy(cfg.mcsTableType, "TABLE1");
                strcpy(cfg.channelModel, "FLAT_FADING");
                strcpy(cfg.harqRvSeq, chase_case ? "CHASE" : "IR");
                expected_attempts = ack_attempt;
                demap_calls = combine_calls = varied_calls = 0;
                rng_seed(12345);
                run_pusch_ul_cb_4port_extended_simulation(&cfg);
                REQUIRE(multi ? cb_count > 1 : cb_count == 1);
                REQUIRE(combine_calls == expected_attempts * cb_count);
                REQUIRE(demap_calls == fixed_rank * expected_attempts);
                release_snapshots();
            }
        }
    }
    /* One driver invocation now spans multiple TB lifetimes. Changing rank
     * also changes E, TBS, CB count and allocated buffer sizes. */
    for (sequence_mode = 1; sequence_mode <= 3; sequence_mode++) {
        for (chase_case = 0; chase_case <= 1; chase_case++) {
            L1Config cfg = {0};
            tdl_case = sequence_mode >= 2;
            cfg.numRB = tdl_case ? 4 : 100;
            cfg.mcsIndex = tdl_case ? 5 : 27;
            cfg.scsKHz = 30; cfg.numTrials = 4;
            cfg.snrStart = cfg.snrEnd = tdl_case ? 10.0 : 60.0;
            cfg.snrStep = 1.0; cfg.harqEnable = 1; cfg.harqMaxRetx = 4;
            cfg.tdlDelaySpreadNs = 300.0;
            cfg.tdlTimeCorrelation = sequence_mode == 3;
            cfg.tdlSpatialCorrTx = sequence_mode == 3 ? .7 : 0;
            cfg.tdlSpatialCorrRx = sequence_mode == 3 ? .6 : 0;
            cfg.tdlMaxDopplerHz = 100.0; cfg.tdlHarqIntervalMs = 1.0;
            strcpy(cfg.mcsTableType, "TABLE1");
            strcpy(cfg.channelModel, tdl_case ? "TDL" : "FLAT_FADING");
            strcpy(cfg.tdlProfile, "A");
            strcpy(cfg.harqRvSeq, chase_case ? "CHASE" : "IR");
            tb_seen = sequence_attempts = 0;
            time_init_calls = time_sample_calls = 0;
            rng_seed(12345);
            run_pusch_ul_cb_4port_extended_simulation(&cfg);
            finish_tb();
            REQUIRE(tb_seen == cfg.numTrials);
            REQUIRE(time_init_calls == (sequence_mode==3 ? 64 : 0));
            REQUIRE(time_sample_calls == (sequence_mode==3 ? 256 : 0));
            REQUIRE(sequence_attempts == (tdl_case ? 16 : 8));
        }
    }
    sequence_mode = 0;
    if (failures) fprintf(stderr, "%d UL pipeline checks failed\n", failures);
    else puts("UL pipeline: 32 single-TB cases + 6 four-TB sequences PASS");
    return failures ? 1 : 0;
}
