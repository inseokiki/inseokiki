/* ================================================================
 *  mimo.c
 *  SU-MIMO channel model + MRC/ZF/MMSE detection (2x2 and 4x4)
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "mimo.h"
#include <math.h>

void mimo_channel_init(MIMOChannel *ch, double snr_db) { ch->snr_db = snr_db; }

void mimo_channel_draw_1x2(cx_t h[2]) {
    double inv_sq2 = 1.0 / sqrt(2.0);
    for (int r = 0; r < 2; r++)
        h[r] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
}

void mimo_channel_draw_2x2(cx_t h[2][2]) {
    double inv_sq2 = 1.0 / sqrt(2.0);
    for (int r = 0; r < 2; r++)
        for (int t = 0; t < 2; t++)
            h[r][t] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
}

void mimo_channel_apply_1x2(const MIMOChannel *ch, const cx_t h[2],
                            cx_t tx, cx_t y[2]) {
    double snrlin = pow(10.0, ch->snr_db / 10.0);
    double sigma  = sqrt(1.0 / (2.0 * snrlin));
    for (int r = 0; r < 2; r++)
        y[r] = h[r] * tx + CX_MAKE(randn() * sigma, randn() * sigma);
}

void mimo_channel_apply_2x2(const MIMOChannel *ch, cx_t h[2][2],
                            const cx_t tx[2], cx_t y[2]) {
    double snrlin = pow(10.0, ch->snr_db / 10.0);
    double sigma  = sqrt(1.0 / (2.0 * snrlin));
    for (int r = 0; r < 2; r++) {
        cx_t s = h[r][0] * tx[0] + h[r][1] * tx[1];
        y[r] = s + CX_MAKE(randn() * sigma, randn() * sigma);
    }
}

void mrc_combine(const cx_t h[2], const cx_t y[2], double N0,
                 cx_t *x_hat, double *noise_var) {
    double hp = CX_NORM(h[0]) + CX_NORM(h[1]);
    if (hp < 1e-10) { *x_hat = CX_ZERO; *noise_var = N0; return; }
    cx_t num = conj(h[0]) * y[0] + conj(h[1]) * y[1];
    *x_hat     = num / hp;
    *noise_var = N0 / hp;
}

/* Shared 2x2 complex inverse: [[a,b],[c,d]]^-1 */
static void inv2x2(cx_t a, cx_t b, cx_t c, cx_t d, cx_t inv[2][2]) {
    cx_t det = a * d - b * c;
    if (cabs(det) < 1e-12) det = CX_MAKE(1e-12, 0.0);
    cx_t id = 1.0 / det;
    inv[0][0] =  d * id; inv[0][1] = -b * id;
    inv[1][0] = -c * id; inv[1][1] =  a * id;
}

void mimo_zf_detect(cx_t h[2][2], const cx_t y[2], double N0,
                    cx_t x_hat[2], double noise_var[2]) {
    cx_t hinv[2][2];
    inv2x2(h[0][0], h[0][1], h[1][0], h[1][1], hinv);
    x_hat[0] = hinv[0][0] * y[0] + hinv[0][1] * y[1];
    x_hat[1] = hinv[1][0] * y[0] + hinv[1][1] * y[1];
    noise_var[0] = N0 * (CX_NORM(hinv[0][0]) + CX_NORM(hinv[0][1]));
    noise_var[1] = N0 * (CX_NORM(hinv[1][0]) + CX_NORM(hinv[1][1]));
}

void mimo_mmse_detect(cx_t h[2][2], const cx_t y[2], double N0,
                      cx_t x_hat[2], double noise_var[2]) {
    cx_t h00 = h[0][0], h01 = h[0][1], h10 = h[1][0], h11 = h[1][1];

    /* A = H^H H + N0*I */
    cx_t a = conj(h00) * h00 + conj(h10) * h10 + N0;
    cx_t b = conj(h00) * h01 + conj(h10) * h11;
    cx_t c = conj(h01) * h00 + conj(h11) * h10;
    cx_t d = conj(h01) * h01 + conj(h11) * h11 + N0;
    cx_t Ainv[2][2];
    inv2x2(a, b, c, d, Ainv);

    /* W = Ainv * H^H */
    cx_t Hh00 = conj(h00), Hh01 = conj(h10);
    cx_t Hh10 = conj(h01), Hh11 = conj(h11);
    cx_t W00 = Ainv[0][0] * Hh00 + Ainv[0][1] * Hh10;
    cx_t W01 = Ainv[0][0] * Hh01 + Ainv[0][1] * Hh11;
    cx_t W10 = Ainv[1][0] * Hh00 + Ainv[1][1] * Hh10;
    cx_t W11 = Ainv[1][0] * Hh01 + Ainv[1][1] * Hh11;

    cx_t xs0 = W00 * y[0] + W01 * y[1];
    cx_t xs1 = W10 * y[0] + W11 * y[1];

    /* alpha_t = Re{(W H)_tt}: effective (biased) gain applied to x_t */
    double a0 = creal(W00 * h00 + W01 * h10);
    double a1 = creal(W10 * h01 + W11 * h11);
    if (a0 < 1e-6) a0 = 1e-6;
    if (a1 < 1e-6) a1 = 1e-6;

    x_hat[0] = xs0 / a0;
    x_hat[1] = xs1 / a1;
    noise_var[0] = (1.0 - a0) / a0;
    noise_var[1] = (1.0 - a1) / a1;
}

/* ── 4×4 헬퍼: Gauss-Jordan 역행렬 ─────────────────────────────────────────
 *
 * 입력 행렬 M을 변형하지 않고 [M | I] 형태의 증가 행렬에서 역행렬을 계산.
 * 리턴 -1: 특이 행렬 (pivot 절댓값 < 1e-14).
 *
 * 수식: [M | I] → [I | M⁻¹]  (부분 피벗팅 적용)
 * ─────────────────────────────────────────────────────────────────────────── */
static int inv4x4(const cx_t M[4][4], cx_t inv[4][4]) {
    cx_t aug[4][8];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) aug[r][c] = M[r][c];
        for (int c = 0; c < 4; c++) aug[r][c + 4] = (r == c) ? 1.0 : 0.0;
    }

    for (int col = 0; col < 4; col++) {
        /* 부분 피벗: 가장 큰 원소가 있는 행을 현재 행으로 교환 */
        int    pivot_row = col;
        double max_abs   = cabs(aug[col][col]);
        for (int r = col + 1; r < 4; r++) {
            double v = cabs(aug[r][col]);
            if (v > max_abs) { max_abs = v; pivot_row = r; }
        }
        if (max_abs < 1e-14) return -1;   /* 특이 행렬 */
        if (pivot_row != col)
            for (int c = 0; c < 8; c++) {
                cx_t tmp = aug[col][c]; aug[col][c] = aug[pivot_row][c]; aug[pivot_row][c] = tmp;
            }

        /* pivot 행 정규화 (대각 원소를 1로) */
        cx_t piv_inv = 1.0 / aug[col][col];
        for (int c = 0; c < 8; c++) aug[col][c] *= piv_inv;

        /* 다른 모든 행의 col 열을 0으로 소거 */
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            cx_t factor = aug[r][col];
            for (int c = 0; c < 8; c++) aug[r][c] -= factor * aug[col][c];
        }
    }

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            inv[r][c] = aug[r][c + 4];
    return 0;
}

void mimo_channel_draw_4x4(cx_t h[4][4]) {
    double inv_sq2 = 1.0 / sqrt(2.0);
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 4; t++)
            h[r][t] = CX_MAKE(randn() * inv_sq2, randn() * inv_sq2);
}

void mimo_channel_apply_4x4(const MIMOChannel *ch, cx_t h[4][4],
                             const cx_t tx[4], cx_t y[4]) {
    double snrlin = pow(10.0, ch->snr_db / 10.0);
    double sigma  = sqrt(1.0 / (2.0 * snrlin));
    for (int r = 0; r < 4; r++) {
        cx_t s = 0;
        for (int t = 0; t < 4; t++) s += h[r][t] * tx[t];
        y[r] = s + CX_MAKE(randn() * sigma, randn() * sigma);
    }
}

/* ── 4×4 ZF 검출: x_hat = H⁻¹ y ──────────────────────────────────────────
 *
 * noise_var[t] = N0 · Σ_r |H⁻¹[t][r]|²  (t번째 행의 노이즈 증폭)
 * ─────────────────────────────────────────────────────────────────────────── */
void mimo_zf_detect_4x4(cx_t h[4][4], const cx_t y[4], double N0,
                          cx_t x_hat[4], double noise_var[4]) {
    cx_t hinv[4][4];
    if (inv4x4(h, hinv) < 0) {
        for (int t = 0; t < 4; t++) { x_hat[t] = CX_ZERO; noise_var[t] = N0; }
        return;
    }
    for (int t = 0; t < 4; t++) {
        cx_t s   = 0;
        double nv = 0;
        for (int r = 0; r < 4; r++) {
            s  += hinv[t][r] * y[r];
            nv += CX_NORM(hinv[t][r]);
        }
        x_hat[t]    = s;
        noise_var[t] = N0 * nv;
    }
}

/* ── 4×4 MMSE 검출: W = (H^H H + N0 I)⁻¹ H^H ──────────────────────────────
 *
 * A      = H^H H + N0·I  (4×4 Gramian, 양정치 에르미트 행렬)
 * b      = H^H y          (4×1)
 * x_biased = A⁻¹ b       (편향 추정)
 * WH     = A⁻¹(A - N0·I) = I - N0·A⁻¹
 * α_t    = 1 - N0·Re{(A⁻¹)_tt}  (편향 계수)
 * x_hat[t]      = x_biased[t] / α_t
 * noise_var[t]  = (1 - α_t) / α_t
 * ─────────────────────────────────────────────────────────────────────────── */
/* ── 4-Rx MRC 결합 (Rank-1 프리코더 적용 후 유효 채널 h_eff = H·W) ─────────
 *
 * h[r]  : Rx 안테나 r의 유효 채널 계수 (h_eff = H·W, r = 0..3)
 * y[r]  : Rx 안테나 r의 수신 신호
 *
 * 결합기: x_hat = (Σ_r h[r]^* y[r]) / Σ_r |h[r]|²  (최대 SNR 결합)
 * noise_var = N0 / Σ_r |h[r]|²                     (후-결합 등가 노이즈 분산)
 * ─────────────────────────────────────────────────────────────────────────── */
void mrc_combine_4rx(const cx_t h[4], const cx_t y[4], double N0,
                     cx_t *x_hat, double *noise_var) {
    double hp = 0.0;
    for (int r = 0; r < 4; r++) hp += CX_NORM(h[r]);
    if (hp < 1e-10) { *x_hat = CX_ZERO; *noise_var = N0; return; }
    cx_t num = 0.0;
    for (int r = 0; r < 4; r++) num += conj(h[r]) * y[r];
    *x_hat     = num / hp;
    *noise_var = N0 / hp;
}

/* ── 4Rx × 2Layer MMSE 검출 (Rank-2 프리코더 후 H_eff = H·W, 4×2) ────────
 *
 * 과결정(overdetermined) 2-레이어 시스템: 4 Rx, 2 데이터 레이어.
 *
 * 2×2 Gramian으로 축소해 기존 inv2x2 재사용:
 *   A = H_eff^H H_eff + N0·I  (2×2)
 *   b = H_eff^H y             (2×1)
 *   x_biased = A⁻¹ b
 *   α_l  = 1 - N0·Re{(A⁻¹)_ll}   (WH = I - N0·A⁻¹ 의 대각 원소)
 *   x_hat[l]     = x_biased[l] / α_l
 *   noise_var[l] = (1 - α_l) / α_l
 * ─────────────────────────────────────────────────────────────────────────── */
void mimo_mmse_detect_4rx2(const cx_t h_eff[4][2], const cx_t y[4], double N0,
                            cx_t x_hat[2], double noise_var[2]) {
    /* A = H_eff^H H_eff + N0·I  (2×2 Gramian) */
    cx_t A00 = (cx_t)N0, A01 = 0.0, A10 = 0.0, A11 = (cx_t)N0;
    for (int r = 0; r < 4; r++) {
        A00 += conj(h_eff[r][0]) * h_eff[r][0];
        A01 += conj(h_eff[r][0]) * h_eff[r][1];
        A10 += conj(h_eff[r][1]) * h_eff[r][0];
        A11 += conj(h_eff[r][1]) * h_eff[r][1];
    }
    cx_t Ainv[2][2];
    inv2x2(A00, A01, A10, A11, Ainv);

    /* b = H_eff^H y  (2×1) */
    cx_t b0 = 0.0, b1 = 0.0;
    for (int r = 0; r < 4; r++) {
        b0 += conj(h_eff[r][0]) * y[r];
        b1 += conj(h_eff[r][1]) * y[r];
    }

    /* x_biased = A⁻¹ b  (2×1) */
    cx_t xb0 = Ainv[0][0] * b0 + Ainv[0][1] * b1;
    cx_t xb1 = Ainv[1][0] * b0 + Ainv[1][1] * b1;

    /* α_l = 1 - N0·Re{(A⁻¹)_ll} */
    double a0 = 1.0 - N0 * creal(Ainv[0][0]);
    double a1 = 1.0 - N0 * creal(Ainv[1][1]);
    if (a0 < 1e-6) a0 = 1e-6;
    if (a1 < 1e-6) a1 = 1e-6;

    x_hat[0]     = xb0 / a0;
    x_hat[1]     = xb1 / a1;
    noise_var[0] = (1.0 - a0) / a0;
    noise_var[1] = (1.0 - a1) / a1;
}

void mimo_mmse_detect_4x4(cx_t h[4][4], const cx_t y[4], double N0,
                            cx_t x_hat[4], double noise_var[4]) {
    /* A = H^H H + N0·I */
    cx_t A[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            A[i][j] = (i == j) ? (cx_t)N0 : 0.0;
            for (int r = 0; r < 4; r++) A[i][j] += conj(h[r][i]) * h[r][j];
        }
    }

    /* A⁻¹ */
    cx_t Ainv[4][4];
    if (inv4x4(A, Ainv) < 0) {
        for (int t = 0; t < 4; t++) { x_hat[t] = CX_ZERO; noise_var[t] = N0; }
        return;
    }

    /* b = H^H y,  x_biased = A⁻¹ b */
    cx_t b[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++)
        for (int r = 0; r < 4; r++) b[i] += conj(h[r][i]) * y[r];

    cx_t x_biased[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) x_biased[i] += Ainv[i][j] * b[j];

    /* α_t = 1 - N0·Re{(A⁻¹)_tt}  (WH = I - N0·A⁻¹ 에서 대각 원소 추출) */
    for (int t = 0; t < 4; t++) {
        double alpha = 1.0 - N0 * creal(Ainv[t][t]);
        if (alpha < 1e-6) alpha = 1e-6;
        x_hat[t]     = x_biased[t] / alpha;
        noise_var[t] = (1.0 - alpha) / alpha;
    }
}
