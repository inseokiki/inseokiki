#include "polar.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Bhattacharyya 파라미터를 이용한 frozen bit 선택 ──────────────────────
 *
 * Bhattacharyya 파라미터 Z[i] : 채널 i의 신뢰도 (작을수록 좋은 채널)
 * 재귀 업데이트 공식 (polar code 채널 분극):
 *   Z_upper = 2·Z - Z²     (두 채널을 XOR로 합성한 상위 채널, 더 나쁨)
 *   Z_lower = Z²            (하위 채널, 더 좋음)
 *
 * K개의 가장 신뢰도 높은(Z가 작은) 채널 → 정보 비트
 * 나머지 N-K개 채널                    → frozen 비트 (0으로 고정)
 * ─────────────────────────────────────────────────────────────────────── */
typedef struct { double val; int idx; } ChannelReliability;

static int cmp_reliability(const void *a, const void *b) {
    const ChannelReliability *ca = (const ChannelReliability *)a;
    const ChannelReliability *cb = (const ChannelReliability *)b;
    return (ca->val > cb->val) - (ca->val < cb->val);   /* 오름차순: 낮을수록 좋은 채널 */
}

static void generate_frozen_bits(PolarCodec *pc) {
    int N    = pc->N;
    int log2_N = (int)(log2((double)N) + 0.5);   /* 트리 깊이 = log2(N) */

    /* 초기 단일 채널의 Bhattacharyya 파라미터 = 0.5 (BPSK over AWGN 가정) */
    double *bhatt_param = (double *)malloc(N * sizeof(double));
    bhatt_param[0] = 0.5;

    /* log2_N 단계에 걸쳐 채널을 두 배씩 분극 */
    for (int stage = 0; stage < log2_N; stage++) {
        int cur_size = 1 << stage;   /* 현재 단계의 채널 수 */

        double *updated = (double *)malloc(2 * cur_size * sizeof(double));
        for (int j = 0; j < cur_size; j++) {
            double z = bhatt_param[j];
            updated[2*j]     = 2*z - z*z;   /* 상위 채널 (XOR합성, 더 나쁨) */
            updated[2*j + 1] = z*z;          /* 하위 채널 (더 좋음)          */
        }
        for (int j = 0; j < 2 * cur_size; j++)
            bhatt_param[j] = updated[j];
        free(updated);
    }

    /* Z 값이 낮은 순서로 정렬해 신뢰도 높은 채널 K개를 정보 비트로 선택 */
    ChannelReliability *sorted_channels = (ChannelReliability *)malloc(N * sizeof(ChannelReliability));
    for (int i = 0; i < N; i++) {
        sorted_channels[i].val = bhatt_param[i];
        sorted_channels[i].idx = i;
    }
    qsort(sorted_channels, N, sizeof(ChannelReliability), cmp_reliability);

    /* 가장 좋은 K개 → 정보 채널 인덱스 */
    for (int i = 0; i < pc->K; i++)
        pc->info_indices[i] = sorted_channels[i].idx;

    /* info_indices를 오름차순 정렬 (인코더에서 순서대로 비트를 넣기 위해) */
    for (int i = 0; i < pc->K - 1; i++) {
        for (int j = i + 1; j < pc->K; j++) {
            if (pc->info_indices[j] < pc->info_indices[i]) {
                int tmp              = pc->info_indices[i];
                pc->info_indices[i]  = pc->info_indices[j];
                pc->info_indices[j]  = tmp;
            }
        }
    }

    /* frozen_mask[i] = 1 이면 채널 i는 frozen (고정 0), 0 이면 정보 비트 */
    memset(pc->frozen_mask, 0, N * sizeof(int));
    for (int i = pc->K; i < N; i++)
        pc->frozen_mask[sorted_channels[i].idx] = 1;

    free(bhatt_param);
    free(sorted_channels);
}

void polar_init(PolarCodec *pc, int N, int K) {
    pc->N            = N;
    pc->K            = K;
    pc->info_indices = (int *)malloc(K * sizeof(int));
    pc->frozen_mask  = (int *)malloc(N * sizeof(int));
    generate_frozen_bits(pc);
}

void polar_free(PolarCodec *pc) {
    free(pc->info_indices); pc->info_indices = NULL;
    free(pc->frozen_mask);  pc->frozen_mask  = NULL;
}

/* ── Polar 변환 (Kronecker 곱 F^{⊗n}) ───────────────────────────────────
 * 버터플라이 연산을 반복: 인접 두 원소 중 하나를 XOR로 업데이트.
 * stride m 으로 N/2m 회 반복 → in-place 변환. */
static void polar_transform(int *x, int N) {
    for (int stride = 1; stride < N; stride *= 2)
        for (int i = 0; i < N; i += 2 * stride)
            for (int j = 0; j < stride; j++)
                x[i + j] ^= x[i + j + stride];
}

void polar_encode(const PolarCodec *pc, const int *info, int *coded) {
    int *u = (int *)calloc(pc->N, sizeof(int));

    /* 정보 비트를 신뢰도 높은 채널 위치에 배치, frozen 위치는 0 유지 */
    for (int i = 0; i < pc->K; i++)
        u[pc->info_indices[i]] = info[i];

    polar_transform(u, pc->N);
    memcpy(coded, u, pc->N * sizeof(int));
    free(u);
}

/* ── SC (Successive Cancellation) 디코더 ─────────────────────────────────
 *
 * 2D 배열을 1D로 저장:
 *   L[stage * N + bit] : stage 단계에서 bit 위치의 LLR 메시지
 *   C[stage * N + bit] : stage 단계에서 bit 위치의 부분 복호 결과
 *
 *   stage = n     : 채널 출력 LLR (루트)
 *   stage = 0     : 최종 비트 결정 (리프)
 *
 * f-function (상위 브랜치, XOR 합성 채널):
 *   f(a, b) = sign(a)·sign(b)·min(|a|, |b|)   ← min-star 근사
 *
 * g-function (하위 브랜치, u가 이미 결정된 후):
 *   g(a, b, u) = (1 - 2u)·a + b
 * ─────────────────────────────────────────────────────────────────────── */

/* f-function: 상위(XOR) 브랜치 LLR 계산 */
static double f_func(double llr_left, double llr_right) {
    /* sign(a)·sign(b)·min(|a|,|b|) */
    double sign      = ((llr_left >= 0) == (llr_right >= 0)) ? 1.0 : -1.0;
    double abs_left  = llr_left  < 0 ? -llr_left  : llr_left;
    double abs_right = llr_right < 0 ? -llr_right : llr_right;
    return sign * (abs_left < abs_right ? abs_left : abs_right);
}

/* g-function: 하위 브랜치 LLR 계산 (u = 이미 복호된 상위 비트) */
static double g_func(double llr_left, double llr_right, int decoded_upper_bit) {
    return (1 - 2 * decoded_upper_bit) * llr_left + llr_right;
}

static void sc_recurse(double *L, int *C, int N, int stage, int offset,
                        const int *frozen_mask) {
    /* 리프: 단일 비트 결정 */
    if (stage == 0) {
        C[offset] = frozen_mask[offset] ? 0 : (L[offset] < 0 ? 1 : 0);
        return;
    }

    int half_width = 1 << (stage - 1);   /* 이 단계에서 처리하는 절반 폭 */

    /* 상위 브랜치 LLR 계산 (f-function) → 왼쪽 자식 재귀 */
    for (int i = 0; i < half_width; i++) {
        double lhs = L[stage * N + offset + i];
        double rhs = L[stage * N + offset + half_width + i];
        L[(stage - 1) * N + offset + i] = f_func(lhs, rhs);
    }
    sc_recurse(L, C, N, stage - 1, offset, frozen_mask);

    /* 하위 브랜치 LLR 계산 (g-function, 상위 복호 결과 사용) → 오른쪽 자식 재귀 */
    for (int i = 0; i < half_width; i++) {
        double lhs              = L[stage * N + offset + i];
        double rhs              = L[stage * N + offset + half_width + i];
        int    upper_bit        = C[(stage - 1) * N + offset + i];   /* 이미 복호된 상위 비트 */
        L[(stage - 1) * N + offset + half_width + i] = g_func(lhs, rhs, upper_bit);
    }
    sc_recurse(L, C, N, stage - 1, offset + half_width, frozen_mask);

    /* 부분 합 결합: XOR로 이 단계의 복호 결과 생성 */
    for (int i = 0; i < half_width; i++) {
        int left_bit  = C[(stage - 1) * N + offset + i];
        int right_bit = C[(stage - 1) * N + offset + half_width + i];
        C[stage * N + offset + i]              = left_bit ^ right_bit;
        C[stage * N + offset + half_width + i] = right_bit;
    }
}

void polar_decode(const PolarCodec *pc, const double *llr, int *decoded) {
    int N      = pc->N;
    int log2_N = (int)(log2((double)N) + 0.5);

    /* L, C 배열 초기화: (log2_N + 1) × N */
    double *L = (double *)calloc((log2_N + 1) * N, sizeof(double));
    int    *C = (int    *)calloc((log2_N + 1) * N, sizeof(int));

    /* 루트 단계(stage = log2_N)에 채널 출력 LLR 세팅 */
    for (int i = 0; i < N; i++)
        L[log2_N * N + i] = llr[i];

    sc_recurse(L, C, N, log2_N, 0, pc->frozen_mask);

    /* stage 0의 복호 결과에서 정보 비트만 추출 */
    for (int i = 0; i < pc->K; i++)
        decoded[i] = C[pc->info_indices[i]];

    free(L);
    free(C);
}
