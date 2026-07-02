#include "ldpc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── 자료구조 설명 ────────────────────────────────────────────────────────
 * H (parity check matrix)는 CSR (Compressed Sparse Row) 형식으로 저장.
 *
 *   H_row_ptr[c] ~ H_row_ptr[c+1] : check node c 에 연결된 엣지 범위
 *   H_col[p]                       : 엣지 p 가 연결된 variable node 번호
 *   H_nnz                          : 총 엣지(비-zero 원소) 수
 *
 * Ht (H의 전치, variable → check 방향)
 *
 *   Ht_row_ptr[v] ~ Ht_row_ptr[v+1] : variable node v 에 연결된 엣지 범위
 *   Ht_links[k].check_idx            : 연결된 check node 번호
 *   Ht_links[k].pos_in_check         : 해당 check node 내 엣지 위치
 * ─────────────────────────────────────────────────────────────────────── */
static void build_H(LDPCCodec *ldpc) {
    int num_checks = ldpc->num_parity;   /* check node 수  = 패리티 비트 수 */
    int num_info   = ldpc->info_size;    /* variable node 중 정보 비트 수   */
    int num_vars   = ldpc->coded_size;   /* 전체 variable node 수 (info + parity) */
    int col_weight = 3;                  /* 정보 비트 하나가 연결되는 check node 수 */

    /* 각 check node의 연결 수(차수) 계산 */
    int *degree_count = (int *)calloc(num_checks, sizeof(int));

    /* 정보 비트 i는 check node (i*col_weight+j) % num_checks 에 연결 */
    for (int i = 0; i < num_info; i++)
        for (int j = 0; j < col_weight; j++)
            degree_count[(i * col_weight + j) % num_checks]++;

    /* 각 check node는 자신의 패리티 비트(대각선)와도 연결 */
    for (int c = 0; c < num_checks; c++)
        degree_count[c]++;

    /* CSR row pointer 구성: H_row_ptr[c]는 check c의 첫 엣지 위치 */
    ldpc->H_nnz = 0;
    ldpc->H_row_ptr = (int *)malloc((num_checks + 1) * sizeof(int));
    ldpc->H_row_ptr[0] = 0;
    for (int c = 0; c < num_checks; c++) {
        ldpc->H_row_ptr[c + 1] = ldpc->H_row_ptr[c] + degree_count[c];
        ldpc->H_nnz += degree_count[c];
    }
    ldpc->H_col = (int *)malloc(ldpc->H_nnz * sizeof(int));

    /* H_col 채우기: 각 엣지가 연결된 variable node 번호를 기록 */
    memset(degree_count, 0, num_checks * sizeof(int));
    for (int i = 0; i < num_info; i++) {
        for (int j = 0; j < col_weight; j++) {
            int check_idx = (i * col_weight + j) % num_checks;
            int edge_slot = ldpc->H_row_ptr[check_idx] + degree_count[check_idx]++;
            ldpc->H_col[edge_slot] = i;   /* variable node = 정보 비트 i */
        }
    }
    /* 각 check node의 마지막 엣지 = 해당 패리티 비트 (num_info + c) */
    for (int c = 0; c < num_checks; c++) {
        int edge_slot = ldpc->H_row_ptr[c] + degree_count[c]++;
        ldpc->H_col[edge_slot] = num_info + c;
    }
    free(degree_count);

    /* Ht 구성: variable → check 방향 역방향 인덱스 */
    int *var_degree = (int *)calloc(num_vars, sizeof(int));
    for (int c = 0; c < num_checks; c++)
        for (int p = ldpc->H_row_ptr[c]; p < ldpc->H_row_ptr[c + 1]; p++)
            var_degree[ldpc->H_col[p]]++;   /* 이 variable node의 차수 +1 */

    ldpc->Ht_row_ptr = (int *)malloc((num_vars + 1) * sizeof(int));
    ldpc->Ht_row_ptr[0] = 0;
    ldpc->Ht_nnz = 0;
    for (int v = 0; v < num_vars; v++) {
        ldpc->Ht_row_ptr[v + 1] = ldpc->Ht_row_ptr[v] + var_degree[v];
        ldpc->Ht_nnz += var_degree[v];
    }
    ldpc->Ht_links = (VarLink *)malloc(ldpc->Ht_nnz * sizeof(VarLink));

    /* Ht_links 채우기: 각 variable node가 어느 check node의 몇 번째 위치에 연결되는지 */
    memset(var_degree, 0, num_vars * sizeof(int));
    for (int c = 0; c < num_checks; c++) {
        for (int p = ldpc->H_row_ptr[c]; p < ldpc->H_row_ptr[c + 1]; p++) {
            int var_idx      = ldpc->H_col[p];
            int pos_in_check = p - ldpc->H_row_ptr[c];   /* check node 내 엣지 위치 */
            int slot         = ldpc->Ht_row_ptr[var_idx] + var_degree[var_idx]++;
            ldpc->Ht_links[slot].check_idx    = c;
            ldpc->Ht_links[slot].pos_in_check = pos_in_check;
        }
    }
    free(var_degree);
}

void ldpc_init(LDPCCodec *ldpc, int block_size, double code_rate) {
    ldpc->info_size  = block_size;
    ldpc->code_rate  = code_rate;
    ldpc->num_parity = (int)(block_size * (1.0 / code_rate - 1.0));
    ldpc->coded_size = block_size + ldpc->num_parity;
    build_H(ldpc);
}

void ldpc_free(LDPCCodec *ldpc) {
    free(ldpc->H_row_ptr);  ldpc->H_row_ptr  = NULL;
    free(ldpc->H_col);      ldpc->H_col      = NULL;
    free(ldpc->Ht_row_ptr); ldpc->Ht_row_ptr = NULL;
    free(ldpc->Ht_links);   ldpc->Ht_links   = NULL;
}

/* 조직적(systematic) 부호화: coded = [info | parity]
 * parity[c] = XOR of all info bits connected to check node c */
void ldpc_encode(const LDPCCodec *ldpc, const int *info, int *coded) {
    int num_info   = ldpc->info_size;
    int num_checks = ldpc->num_parity;

    /* 정보 비트를 그대로 앞에 복사 */
    for (int i = 0; i < num_info; i++)
        coded[i] = info[i];

    /* 각 check node에 연결된 정보 비트들을 XOR → 패리티 비트 */
    for (int c = 0; c < num_checks; c++) {
        int parity_bit = 0;
        for (int p = ldpc->H_row_ptr[c]; p < ldpc->H_row_ptr[c + 1]; p++) {
            int var_idx = ldpc->H_col[p];
            if (var_idx < num_info)
                parity_bit ^= info[var_idx];
        }
        coded[num_info + c] = parity_bit;
    }
}

/* ── Sum-Product (Belief Propagation) 디코더 ─────────────────────────────
 *
 * 메시지 정의:
 *   msg_v2c[v]    : variable node v → (모든 연결 check node) 의 LLR 신념값
 *                   이터레이션 초기값 = 채널 LLR
 *   msg_c2v[edge] : check node → variable node 방향의 외재적(extrinsic) LLR
 *
 * Check node 업데이트 (sum-product 공식):
 *   msg_c2v[edge i] = 2 · atanh( ∏_{j ≠ i} tanh(msg_v2c[j] / 2) )
 *   → 연결된 다른 모든 variable node 메시지의 tanh 곱을 atanh로 역변환
 *   → clamp to ±0.9999 to avoid atanh(±1) = ±∞ numerical overflow
 *
 * Variable node 업데이트:
 *   msg_v2c[v] = channel_llr[v] + Σ_{모든 연결 check node} msg_c2v[incoming edge]
 * ─────────────────────────────────────────────────────────────────────── */
int ldpc_decode(const LDPCCodec *ldpc, const double *channel_llr,
                int max_iter, int *decoded) {
    int num_checks = ldpc->num_parity;
    int num_vars   = ldpc->coded_size;

    /* msg_v2c[v]    : variable → check 메시지 (초기값 = 채널 LLR) */
    double *msg_v2c = (double *)malloc(num_vars * sizeof(double));
    /* msg_c2v[edge] : check → variable 외재 메시지 (초기값 = 0) */
    double *msg_c2v = (double *)calloc(ldpc->H_nnz, sizeof(double));

    for (int v = 0; v < num_vars; v++)
        msg_v2c[v] = channel_llr[v];

    for (int iter = 0; iter < max_iter; iter++) {

        /* ── Check node 업데이트 ───────────────────────────────────────── */
        for (int c = 0; c < num_checks; c++) {
            int edge_start  = ldpc->H_row_ptr[c];
            int edge_end    = ldpc->H_row_ptr[c + 1];
            int check_degree = edge_end - edge_start;   /* 이 check node의 차수 */

            for (int i = 0; i < check_degree; i++) {
                /* i번 엣지를 제외한 나머지 tanh(msg/2) 의 곱 */
                double tanh_product = 1.0;
                for (int j = 0; j < check_degree; j++) {
                    if (i == j) continue;
                    double neighbor_msg = msg_v2c[ldpc->H_col[edge_start + j]];
                    tanh_product *= tanh(neighbor_msg / 2.0);
                }
                /* atanh(±1) = ±∞ 방지를 위해 클램프 */
                if (tanh_product >  0.9999) tanh_product =  0.9999;
                if (tanh_product < -0.9999) tanh_product = -0.9999;

                msg_c2v[edge_start + i] = 2.0 * atanh(tanh_product);
            }
        }

        /* ── Variable node 업데이트 ────────────────────────────────────── */
        for (int v = 0; v < num_vars; v++) {
            /* 채널 LLR + 모든 연결 check node 로부터의 외재 메시지 합산 */
            double belief = channel_llr[v];
            for (int k = ldpc->Ht_row_ptr[v]; k < ldpc->Ht_row_ptr[v + 1]; k++) {
                int check_idx    = ldpc->Ht_links[k].check_idx;
                int pos_in_check = ldpc->Ht_links[k].pos_in_check;
                belief += msg_c2v[ldpc->H_row_ptr[check_idx] + pos_in_check];
            }
            msg_v2c[v] = belief;
        }
    }

    /* 최종 판정: belief < 0 이면 bit=1, belief >= 0 이면 bit=0 */
    for (int i = 0; i < ldpc->info_size; i++)
        decoded[i] = (msg_v2c[i] < 0) ? 1 : 0;

    free(msg_v2c);
    free(msg_c2v);
    return 0;
}
