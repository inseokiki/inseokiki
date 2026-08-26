/* ================================================================
 *  ul_power_ctrl.c
 *  UL Closed-Loop Power Control simulation
 *  TS 38.213 §7.2.1 PUSCH Power Control
 *
 *  전력 공식 (TS 38.213 eq.7):
 *    P_PUSCH(i) = min(P_CMAX, P_0 + α·PL + f(i))  [dBm]
 *
 *    P_0  : 목표 수신 파워/RB (반정적, RRC/OAM 설정)
 *    α    : 경로손실 부분 보상 계수 (0 ≤ α ≤ 1)
 *    PL   : UE가 DL RS(SSB/CSI-RS)로 추정한 경로손실 [dB]
 *    f(i) : closed-loop TPC 누산값 [dB]  f(i) = f(i-1) + δ_TPC
 *
 *    단순화(문서화):
 *      M 항(RB 수 10log10(M)) 및 ΔTF(MCS 오프셋)는 P_0에 흡수
 *      → P_0 = 목표 수신 파워 + MCS 오프셋 (흡수 후 단일 값)
 *
 *  TPC 명령 결정 규칙 (inner-loop, genie-aided):
 *    SINR_err = SINR_target − SINR_meas
 *    δ = +3 dB  if SINR_err >  3.0 dB  (큰 부족 → 강한 증폭)
 *    δ = +1 dB  if SINR_err >  0.5 dB  (소폭 부족)
 *    δ =  0 dB  if |SINR_err| ≤ 0.5 dB (데드밴드 — 수렴 판정)
 *    δ = -1 dB  if SINR_err < -0.5 dB  (초과 → 전력 감소)
 *
 *    ★ δ ∈ {−1, 0, +1, +3} dB : TS 38.213 Table 7.2.1-1 그대로
 *
 *  비교 시나리오:
 *    OL-only : f(i) = 0 고정 (P_0 + α·PL만 적용)
 *    CL      : f(i) 누산 (inner-loop CLPC 적용)
 *
 *  Part 1 시변 PL (채널 페이딩/이동성, UL_PC_PL_VAR_STD_DB > 0일 때):
 *    정상상태 Gauss-Markov(AR(1)) 프로세스로 PL(sf)을 UL_PC_PL_DB 주변에서
 *    변동시킨다 — Gudmundson(1991) 그림자페이딩 자기상관 모델의 구현 정의
 *    근사(특정 UE 속도/상관거리에 대응시키지 않음):
 *      PL(sf) = PL_mean + corr·(PL(sf-1) − PL_mean)
 *               + sqrt(1 − corr²)·std·randn()
 *    OL/CL 전력식 모두 매 SF의 순간 PL(sf)로 재계산되므로(스펙상 UE가 매
 *    SF 최신 DL RS로 PL을 다시 추정) α·PL 항 자체는 두 계열 모두 즉시
 *    추종한다 — 차이는 f(i)뿐이다: CL은 SINR 오차 기반 이산 TPC 누산이라
 *    PL이 계속 바뀌면 정착(settle)할 시간이 부족해질 수 있고, 이 지연이
 *    (1-α)·PL 잔여 보상의 실제 한계를 드러낸다. UL_PC_PL_VAR_STD_DB=0이면
 *    PL(sf)=UL_PC_PL_DB 상수로 고정되어 이 확장 이전과 동일하게 동작한다.
 *
 *  노이즈 바닥 (gNB 수신단):
 *    N_floor [dBm] = −174 + 10·log10(12·SCS_Hz) + NF
 *    (열잡음 + gNB 잡음지수, 단일 RB 기준)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "ul_power_ctrl.h"
#include "utils.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* ── TPC 결정 (inner-loop) ────────────────────────────────────────────
 *
 * SINR_err = SINR_target − SINR_meas 에 따라 δ 반환.
 * δ ∈ {−1, 0, +1, +3} dB  (TS 38.213 Table 7.2.1-1 accumulated)
 * ─────────────────────────────────────────────────────────────────── */
static double tpc_decide(double sinr_err) {
    if      (sinr_err >  3.0) return  3.0;
    else if (sinr_err >  0.5) return  1.0;
    else if (sinr_err < -0.5) return -1.0;
    else                      return  0.0;
}

/* ════════════════════════════════════════════════════════════════════
 * UL CLPC 시뮬레이션
 *
 * Part 1 — 시계열 (고정 PL = cfg->ulpcPlDb):
 *   서브프레임별 P_tx / P_rx / SINR / f(i) / TPC 명령 출력
 *   OL과 CL 두 계열을 나란히 비교
 *
 * Part 2 — PL 스윕 (snrStart ~ snrEnd ~ snrStep을 PL 구간으로 재사용):
 *   각 PL에서 ulpcNumSf 서브프레임 시뮬레이션 후 정상상태 평균 출력
 *   OL vs CL 비교 + P_CMAX 클램핑 여부 표시
 * ════════════════════════════════════════════════════════════════════ */
void run_ulpc_simulation(const L1Config *cfg) {
    double P0        = cfg->ulpcP0Dbm;
    double PCMAX     = cfg->ulpcPcmaxDbm;
    double alpha     = cfg->ulpcAlpha;
    double NF        = cfg->ulpcNfDb;
    double SINR_tgt  = cfg->ulpcSinrTargetDb;
    double PL_fix    = cfg->ulpcPlDb;
    int    nsf       = cfg->ulpcNumSf;
    double plVarStd  = cfg->ulpcPlVarStdDb;
    double plVarCorr = cfg->ulpcPlVarCorr;

    /* gNB 수신단 1-RB 노이즈 바닥 */
    double bw_hz    = 12.0 * cfg->scsKHz * 1e3;
    double N_floor  = -174.0 + 10.0 * log10(bw_hz) + NF;

    printf("=== UL Closed-Loop Power Control (TS 38.213 §7.2.1) ===\n");
    printf("P_0             : %.1f dBm/RB\n",  P0);
    printf("P_CMAX          : %.1f dBm\n",      PCMAX);
    printf("alpha (α)       : %.2f\n",           alpha);
    printf("gNB NF          : %.1f dB\n",       NF);
    printf("Noise floor     : %.1f dBm/RB  (kTB·NF, SCS=%d kHz)\n",
           N_floor, cfg->scsKHz);
    printf("SINR target     : %.1f dB\n",       SINR_tgt);
    printf("TPC 명령 집합   : {-1, 0, +1, +3} dB\n");
    printf("OL잔류 (α=%.2f) : (1-α)·PL = %.2f·PL  [CL이 보상해야 할 양]\n\n",
           alpha, 1.0 - alpha);

    /* ================================================================
     * Part 1 — 시계열 (PL_VAR_STD_DB=0이면 고정 PL, 아니면 Gauss-Markov 시변)
     * ================================================================ */
    if (plVarStd > 0.0) {
        printf("--- Part 1: 시계열 수렴 (시변 PL, 평균 %.1f dB, std %.1f dB, corr %.2f) ---\n",
               PL_fix, plVarStd, plVarCorr);
    } else {
        printf("--- Part 1: 시계열 수렴 (고정 PL = %.1f dB) ---\n", PL_fix);
    }
    printf("OL 전력(평균 PL 기준): P0 + α·PL = %.1f + %.2f·%.1f = %.1f dBm\n",
           P0, alpha, PL_fix, P0 + alpha * PL_fix);
    printf("OL SINR(평균 PL 기준): %.1f - %.1f = %.1f dB  (목표 %.1f dB, 잔류 부족 %.1f dB)\n",
           P0 + alpha * PL_fix - PL_fix, N_floor,
           P0 + alpha * PL_fix - PL_fix - N_floor,
           SINR_tgt,
           SINR_tgt - (P0 + alpha * PL_fix - PL_fix - N_floor));

    printf("\n%-5s  %-8s  %-11s  %-11s  %-10s  %-10s  %-10s  %s\n",
           "SF", "PL(t)", "P_tx_CL", "P_tx_OL", "SINR_CL", "SINR_OL", "f(i)", "TPC");
    printf("%-5s  %-8s  %-11s  %-11s  %-10s  %-10s  %-10s  %s\n",
           "", "(dB)", "(dBm)", "(dBm)", "(dB)", "(dB)", "(dB)", "(dB)");
    for (int i = 0; i < 85; i++) printf("-");
    printf("\n");

    double f_acc = 0.0;
    double pl_t  = PL_fix;
    for (int sf = 0; sf < nsf; sf++) {
        /* Gauss-Markov 시변 PL 갱신 (plVarStd=0이면 pl_t는 PL_fix로 고정 유지) */
        if (sf > 0 && plVarStd > 0.0) {
            pl_t = PL_fix + plVarCorr * (pl_t - PL_fix)
                 + sqrt(1.0 - plVarCorr * plVarCorr) * plVarStd * randn();
        }
        double p_ol_now = P0 + alpha * pl_t;   /* 개루프 전력 (f=0 기준, 순간 PL) */

        /* OL 전력 (f=0 고정, 순간 PL 반영) */
        double ptx_ol  = fmin(PCMAX, p_ol_now);
        double prx_ol  = ptx_ol  - pl_t;
        double sinr_ol = prx_ol  - N_floor;

        /* CL 전력 (f(i) 누산, 순간 PL 반영) */
        double ptx_cl  = fmin(PCMAX, p_ol_now + f_acc);
        double prx_cl  = ptx_cl  - pl_t;
        double sinr_cl = prx_cl  - N_floor;

        /* TPC 결정 후 f(i) 업데이트 (다음 SF에 반영) */
        double sinr_err = SINR_tgt - sinr_cl;
        double delta    = tpc_decide(sinr_err);

        printf("%-5d  %+7.2f  %+8.2f dBm  %+8.2f dBm  %+7.2f dB  %+7.2f dB  %+7.2f dB  %+.0f\n",
               sf, pl_t, ptx_cl, ptx_ol, sinr_cl, sinr_ol, f_acc, delta);

        f_acc += delta;
        /* f(i) 클램프: TS 38.213 §7.2.1 — 구현 정의 (±30 dB 사용) */
        if (f_acc >  30.0) f_acc =  30.0;
        if (f_acc < -30.0) f_acc = -30.0;
    }

    double p_ol_last  = P0 + alpha * pl_t;
    double sinr_cl_ss = fmin(PCMAX, p_ol_last + f_acc) - pl_t - N_floor;
    printf("\n정상 상태 (SF=%d): PL=%.1f dB, f(i)=%.1f dB, P_tx=%.1f dBm, SINR_CL=%.1f dB\n",
           nsf, pl_t, f_acc, fmin(PCMAX, p_ol_last + f_acc), sinr_cl_ss);
    if (fmin(PCMAX, p_ol_last + f_acc) >= PCMAX - 0.01)
        printf("★ P_CMAX 클램핑 발생 — 이 PL에서 SINR 목표 달성 불가\n");

    /* ================================================================
     * Part 2 — PL 스윕 (정상상태 비교)
     * ================================================================ */
    printf("\n--- Part 2: PL 스윕 (정상상태 OL vs CL 비교) ---\n");
    printf("PL 범위: %.1f ~ %.1f dB (step %.1f)\n",
           cfg->snrStart, cfg->snrEnd, cfg->snrStep);
    printf("정상상태 = 마지막 %d SF 평균\n\n", nsf / 4);

    printf("%-9s  %-12s  %-12s  %-11s  %-11s  %-9s  %s\n",
           "PL(dB)", "P_tx_OL", "P_tx_CL", "SINR_OL", "SINR_CL", "f_ss", "Clamped?");
    printf("%-9s  %-12s  %-12s  %-11s  %-11s  %-9s  %s\n",
           "", "(dBm)", "(dBm)", "(dB)", "(dB)", "(dB)", "");
    for (int i = 0; i < 82; i++) printf("-");
    printf("\n");

    for (double pl = cfg->snrStart; pl <= cfg->snrEnd + 1e-6; pl += cfg->snrStep) {
        double p_ol_pl = P0 + alpha * pl;   /* OL 전력 */
        double ptx_ol  = fmin(PCMAX, p_ol_pl);
        double sinr_ol = ptx_ol - pl - N_floor;

        /* CL 정상상태: nsf SF 시뮬레이션 후 마지막 nsf/4 평균 */
        double fa = 0.0;
        double ptx_cl_sum = 0.0, sinr_cl_sum = 0.0;
        int    avg_start  = nsf - nsf / 4;
        for (int sf = 0; sf < nsf; sf++) {
            double ptx_cl  = fmin(PCMAX, p_ol_pl + fa);
            double sinr_cl = ptx_cl - pl - N_floor;
            if (sf >= avg_start) {
                ptx_cl_sum  += ptx_cl;
                sinr_cl_sum += sinr_cl;
            }
            double err = SINR_tgt - sinr_cl;
            fa += tpc_decide(err);
            if (fa >  30.0) fa =  30.0;
            if (fa < -30.0) fa = -30.0;
        }
        int avg_cnt = nsf - avg_start;
        double ptx_cl_ss  = ptx_cl_sum  / avg_cnt;
        double sinr_cl_ss_pl = sinr_cl_sum / avg_cnt;
        int    clamped    = (ptx_cl_ss >= PCMAX - 0.1);

        printf("%-9.1f  %+9.2f dBm  %+9.2f dBm  %+8.2f dB  %+8.2f dB  %+6.1f dB  %s\n",
               pl, ptx_ol, ptx_cl_ss, sinr_ol, sinr_cl_ss_pl, fa,
               clamped ? "YES ★" : "no");
    }

    /* ================================================================
     * Part 3 — α 효과 요약
     * ================================================================ */
    printf("\n--- Part 3: α=%.2f 설계 함의 ---\n", alpha);
    printf("OL 잔류 경로손실 미보상 비율  : %.0f%%\n", (1.0 - alpha) * 100.0);
    printf("CL이 보상해야 할 f_ss (PL=%.0fdB): %.1f dB\n",
           PL_fix, (1.0 - alpha) * PL_fix);
    /* P_CMAX 클램핑 한계 PL:
     *   gNB 목표 SINR 달성을 위한 P_tx = PL + SINR_target + N_floor
     *   클램핑 조건: PL + SINR_target + N_floor > P_CMAX
     *   ∴ PL_crit = P_CMAX − SINR_target − N_floor                     */
    double pl_edge = PCMAX - SINR_tgt - N_floor;
    printf("P_CMAX 한계 PL                 : %.1f dB  (이상이면 클램핑)\n", pl_edge);
    printf("  계산: P_CMAX(%.0f dBm) − SINR_tgt(%.0f dB) − N_floor(%.1f dBm) = %.1f dB\n",
           PCMAX, SINR_tgt, N_floor, pl_edge);
    printf("  → PL < %.1fdB : CL이 SINR 목표 %.1fdB 달성 가능\n",
           pl_edge, SINR_tgt);
    printf("  → PL ≥ %.1fdB : P_CMAX 클램핑, SINR 부족 (셀 엣지)\n", pl_edge);
    printf("\nα<1의 설계 목적: 셀 엣지 UE가 full PL을 보상하지 않아\n");
    printf("  이웃 셀 간섭을 줄임 (Fractional Power Control)\n");
    printf("  대신 SINR 손실은 CL f(i) 누산으로 부분 회복\n");

    printf("\nUL CLPC simulation complete.\n");
}
