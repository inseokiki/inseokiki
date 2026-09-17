/* Integration contract test: execute the production driver with test-only
 * seams for fixed rank, controlled CRC rejection, and observations at the LLR/HARQ
 * boundaries. Channel, estimator, detector, demapper, and LDPC stay real.
 * This checks wiring/state, not independent PHY reference performance. */
#include "pusch.h"
#include "ul_codebook_4port.h"
#include "modulation.h"
#include "nr_rate_matching.h"
#include "crc.h"
#include "nr_sch.h"
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

static void observe_seg(int B, double rate, NRSegInfo *seg) {
    nr_seg_compute(B, rate, seg);
    cb_count = seg->C;
    if (cb_count > 16) abort();
}
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; \
} } while (0)

static void fixed_select(const cx_t H[4][4], double N0, int *rank, int *tpmi) {
    (void)H; (void)N0;
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
                            int start, int end, int rv, int e,
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
    nr_ldpc_rate_match_combine(buf, Ncb, bg, Zc, start, end, rv, e, llr);
    memcpy(saved_soft[cb], buf, (size_t)Ncb * sizeof(double));
    combine_calls++;
}

/* Compile the actual driver with local boundary observers. No production
 * hooks or linker-specific --wrap dependency, so this also runs on macOS. */
#define nr_seg_compute observe_seg
#define ul_cb4_select fixed_select
#define check_crc force_nack
#define qam_demap_llr_re observe_demap
#define nr_ldpc_rate_match_combine observe_combine
#include "../src/pusch_ul_cb_4port.c"
#undef nr_seg_compute
#undef ul_cb4_select
#undef check_crc
#undef qam_demap_llr_re
#undef nr_ldpc_rate_match_combine

static void release_snapshots(void) {
    for (int c = 0; c < 16; c++) {
        free(saved_soft[c]); saved_soft[c] = NULL;
        buffer_addr[c] = NULL;
    }
}

int main(void) {
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
    if (failures) fprintf(stderr, "%d UL pipeline checks failed\n", failures);
    else puts("UL pipeline: 32 rank/channel/HARQ/ACK/segmentation combinations PASS");
    return failures ? 1 : 0;
}
