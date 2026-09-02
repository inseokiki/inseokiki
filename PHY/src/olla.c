/* ================================================================
 *  olla.c
 *  Outer Loop Link Adaptation
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "olla.h"
#include <math.h>

void olla_init(OLLAState *s, double bler_target, double step_down_db) {
    s->offset_db    = 0.0;
    s->bler_target  = bler_target;
    s->step_down_db = step_down_db;
    s->step_up_db   = step_down_db * bler_target / (1.0 - bler_target);
}

void olla_update(OLLAState *s, int ack) {
    if (ack) s->offset_db += s->step_up_db;
    else     s->offset_db -= s->step_down_db;
}

int olla_select_mcs(double effective_snr_db, double gap_db, MCSTableType table) {
    /* 전 MCS 전수 스캔(최대 29개, 무시할 만한 비용) — TS 38.214
     * Table 5.1.3.1-1(MCS_TABLE1)은 변조차수 전환 경계(MCS16→17,
     * 16QAM→64QAM)에서 스펙트럼 효율이 아주 미세하게 감소하는 지점이
     * 있어(2026-09-01, 직접 덤프해 확인 — 실제 스펙 수치, 버그 아님)
     * "첫 미달 지점에서 조기 종료" 최적화는 안전하지 않다. */
    int max_idx = get_max_mcs_index(table);
    int best = 0;
    for (int m = 0; m <= max_idx; m++) {
        MCSEntry e = get_mcs_entry(m, table);
        double eta = e.spectralEff;
        if (eta <= 0.0) continue;
        double snr_req_lin = pow(2.0, eta) - 1.0;
        double snr_req_db  = 10.0 * log10(snr_req_lin) + gap_db;
        if (snr_req_db <= effective_snr_db) best = m;
    }
    return best;
}
