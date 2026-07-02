#include "modulation.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdio.h>

/* Power-normalisation scale so that average symbol energy = 1 */
static double norm_factor(int bits_per_sym) {
    switch (bits_per_sym) {
        case 2: return 1.0 / sqrt(2.0);    /* QPSK   */
        case 4: return 1.0 / sqrt(10.0);   /* 16QAM  */
        case 6: return 1.0 / sqrt(42.0);   /* 64QAM  */
        case 8: return 1.0 / sqrt(170.0);  /* 256QAM */
        default: return 1.0;
    }
}

/* Convert n Gray-coded bits (MSB first) to an unnormalized 1D PAM level.
 *   n=1 (QPSK)  : bit 0 → +1, bit 1 → -1
 *   n=2 (16QAM) : 00→+1, 01→+3, 10→-1, 11→-3
 *   n=3 (64QAM) : 8-level PAM, similarly Gray-coded
 */
static double compute_pam(const int *bits, int n) {
    if (n == 1) {
        return (double)(1 - 2 * bits[0]);
    }
    double pam_level = 2.0 - (1 - 2 * bits[n - 1]);
    for (int k = n - 2; k >= 1; k--) {
        double step_size = (double)(1 << (n - k));
        pam_level = step_size - (1 - 2 * bits[k]) * pam_level;
    }
    return (1 - 2 * bits[0]) * pam_level;
}

/* Look-up table: one PAM constellation point and its Gray-coded bit index */
typedef struct { double level; int bits; } PamEntry;

static void build_pam_table(PamEntry *table, int bits_per_dim) {
    int num_levels = 1 << bits_per_dim;
    for (int idx = 0; idx < num_levels; idx++) {
        int bit_arr[4];
        for (int k = 0; k < bits_per_dim; k++)
            bit_arr[k] = (idx >> (bits_per_dim - 1 - k)) & 1;
        table[idx].level = compute_pam(bit_arr, bits_per_dim);
        table[idx].bits  = idx;
    }
}

int get_bits_per_symbol(const char *mod) {
    if (strcmp(mod, "QPSK")   == 0) return 2;
    if (strcmp(mod, "16QAM")  == 0) return 4;
    if (strcmp(mod, "64QAM")  == 0) return 6;
    if (strcmp(mod, "256QAM") == 0) return 8;
    return 2;
}

void qam_modulate(const int *bits, int num_bits, const char *mod, cx_t *syms) {
    int    bits_per_sym = get_bits_per_symbol(mod);
    int    bits_per_dim = bits_per_sym / 2;   /* bits for I or Q dimension separately */
    double scale        = norm_factor(bits_per_sym);
    int    num_syms     = num_bits / bits_per_sym;

    for (int sym_idx = 0; sym_idx < num_syms; sym_idx++) {
        int bit_offset = sym_idx * bits_per_sym;

        /* Interleaved bit layout: even positions are I bits, odd positions are Q bits.
         * bit_offset+0,2,4,... → I;   bit_offset+1,3,5,... → Q */
        int i_bits[4], q_bits[4];
        for (int b = 0; b < bits_per_dim; b++) {
            i_bits[b] = bits[bit_offset + 2*b];
            q_bits[b] = bits[bit_offset + 2*b + 1];
        }

        double I_level = compute_pam(i_bits, bits_per_dim) * scale;
        double Q_level = compute_pam(q_bits, bits_per_dim) * scale;
        syms[sym_idx]  = CX_MAKE(I_level, Q_level);
    }
}

void qam_demodulate(const cx_t *syms, int num_syms, const char *mod, int *bits) {
    int    bits_per_sym = get_bits_per_symbol(mod);
    int    bits_per_dim = bits_per_sym / 2;
    double scale        = norm_factor(bits_per_sym);
    int    num_levels   = 1 << bits_per_dim;

    PamEntry table[16];
    build_pam_table(table, bits_per_dim);

    for (int i = 0; i < num_syms; i++) {
        double rx_I = creal(syms[i]);
        double rx_Q = cimag(syms[i]);

        double min_dist_I = DBL_MAX,  min_dist_Q = DBL_MAX;
        int    best_idx_I = 0,        best_idx_Q = 0;

        for (int j = 0; j < num_levels; j++) {
            double level_scaled = table[j].level * scale;

            double diff_I    = rx_I - level_scaled;
            double diff_Q    = rx_Q - level_scaled;
            double sq_dist_I = diff_I * diff_I;
            double sq_dist_Q = diff_Q * diff_Q;

            if (sq_dist_I < min_dist_I) { min_dist_I = sq_dist_I; best_idx_I = j; }
            if (sq_dist_Q < min_dist_Q) { min_dist_Q = sq_dist_Q; best_idx_Q = j; }
        }

        int bit_offset = i * bits_per_sym;
        for (int b = 0; b < bits_per_dim; b++) {
            bits[bit_offset + 2*b]     = (best_idx_I >> (bits_per_dim - 1 - b)) & 1;
            bits[bit_offset + 2*b + 1] = (best_idx_Q >> (bits_per_dim - 1 - b)) & 1;
        }
    }
}

void qam_demap_llr(const cx_t *syms, int num_syms, const char *mod,
                   double noise_var, double *llr) {
    int    bits_per_sym = get_bits_per_symbol(mod);
    int    bits_per_dim = bits_per_sym / 2;
    double scale        = norm_factor(bits_per_sym);
    int    num_levels   = 1 << bits_per_dim;

    PamEntry table[16];
    build_pam_table(table, bits_per_dim);

    /* Pre-scale all PAM levels once */
    double levels[16];
    for (int j = 0; j < num_levels; j++)
        levels[j] = table[j].level * scale;

    /* LLR = 2/noise_var * (min_dist²_bit0 - min_dist²_bit1)
     * Positive LLR means bit=0 is more likely */
    double llr_scale = 2.0 / noise_var;

    for (int i = 0; i < num_syms; i++) {
        double rx_I = creal(syms[i]);
        double rx_Q = cimag(syms[i]);

        for (int bit_pos = 0; bit_pos < bits_per_dim; bit_pos++) {
            double min_sq_dist_I_bit0 = DBL_MAX,  min_sq_dist_I_bit1 = DBL_MAX;
            double min_sq_dist_Q_bit0 = DBL_MAX,  min_sq_dist_Q_bit1 = DBL_MAX;

            for (int j = 0; j < num_levels; j++) {
                int    bit_is_one  = (table[j].bits >> (bits_per_dim - 1 - bit_pos)) & 1;
                double diff_I      = rx_I - levels[j];
                double diff_Q      = rx_Q - levels[j];
                double sq_dist_I   = diff_I * diff_I;
                double sq_dist_Q   = diff_Q * diff_Q;

                if (!bit_is_one) {
                    if (sq_dist_I < min_sq_dist_I_bit0) min_sq_dist_I_bit0 = sq_dist_I;
                    if (sq_dist_Q < min_sq_dist_Q_bit0) min_sq_dist_Q_bit0 = sq_dist_Q;
                } else {
                    if (sq_dist_I < min_sq_dist_I_bit1) min_sq_dist_I_bit1 = sq_dist_I;
                    if (sq_dist_Q < min_sq_dist_Q_bit1) min_sq_dist_Q_bit1 = sq_dist_Q;
                }
            }

            llr[i * bits_per_sym + 2*bit_pos]     = llr_scale * (min_sq_dist_I_bit1 - min_sq_dist_I_bit0);
            llr[i * bits_per_sym + 2*bit_pos + 1] = llr_scale * (min_sq_dist_Q_bit1 - min_sq_dist_Q_bit0);
        }
    }
}

void qam_demap_llr_mmse(const cx_t *rx, int num_syms, const char *mod,
                         const cx_t *h_data, double N0, double *llr) {
    int    bits_per_sym = get_bits_per_symbol(mod);
    int    bits_per_dim = bits_per_sym / 2;
    double scale        = norm_factor(bits_per_sym);
    int    num_levels   = 1 << bits_per_dim;

    PamEntry table[16];
    build_pam_table(table, bits_per_dim);

    double levels[16];
    for (int j = 0; j < num_levels; j++)
        levels[j] = table[j].level * scale;

    for (int i = 0; i < num_syms; i++) {
        double ch_power = CX_NORM(h_data[i]);

        if (ch_power < 1e-10) {
            for (int b = 0; b < bits_per_sym; b++) llr[i * bits_per_sym + b] = 0.0;
            continue;
        }

        /* After MMSE equalisation the signal has a bias: E[ŝ] = alpha * s
         * where alpha = |h|²/(|h|²+N0).  The residual noise variance is sigma2. */
        double denom     = ch_power + N0;
        double alpha     = ch_power / denom;
        double sigma2    = N0 * ch_power / (denom * denom);
        double llr_scale = 2.0 / sigma2;

        double rx_I = creal(rx[i]);
        double rx_Q = cimag(rx[i]);

        for (int bit_pos = 0; bit_pos < bits_per_dim; bit_pos++) {
            double min_sq_dist_I_bit0 = DBL_MAX,  min_sq_dist_I_bit1 = DBL_MAX;
            double min_sq_dist_Q_bit0 = DBL_MAX,  min_sq_dist_Q_bit1 = DBL_MAX;

            for (int j = 0; j < num_levels; j++) {
                int    bit_is_one   = (table[j].bits >> (bits_per_dim - 1 - bit_pos)) & 1;
                double scaled_level = alpha * levels[j];   /* bias-corrected reference */
                double diff_I       = rx_I - scaled_level;
                double diff_Q       = rx_Q - scaled_level;
                double sq_dist_I    = diff_I * diff_I;
                double sq_dist_Q    = diff_Q * diff_Q;

                if (!bit_is_one) {
                    if (sq_dist_I < min_sq_dist_I_bit0) min_sq_dist_I_bit0 = sq_dist_I;
                    if (sq_dist_Q < min_sq_dist_Q_bit0) min_sq_dist_Q_bit0 = sq_dist_Q;
                } else {
                    if (sq_dist_I < min_sq_dist_I_bit1) min_sq_dist_I_bit1 = sq_dist_I;
                    if (sq_dist_Q < min_sq_dist_Q_bit1) min_sq_dist_Q_bit1 = sq_dist_Q;
                }
            }

            llr[i * bits_per_sym + 2*bit_pos]     = llr_scale * (min_sq_dist_I_bit1 - min_sq_dist_I_bit0);
            llr[i * bits_per_sym + 2*bit_pos + 1] = llr_scale * (min_sq_dist_Q_bit1 - min_sq_dist_Q_bit0);
        }
    }
}

/* Same as qam_demap_llr but noise_var differs per subcarrier (MRC output). */
void qam_demap_llr_persc(const cx_t *syms, int num_syms, const char *mod,
                          const double *noise_var_per_sc, double *llr) {
    int    bits_per_sym = get_bits_per_symbol(mod);
    int    bits_per_dim = bits_per_sym / 2;
    double scale        = norm_factor(bits_per_sym);
    int    num_levels   = 1 << bits_per_dim;

    PamEntry table[16];
    build_pam_table(table, bits_per_dim);

    double levels[16];
    for (int j = 0; j < num_levels; j++)
        levels[j] = table[j].level * scale;

    for (int i = 0; i < num_syms; i++) {
        double noise_var = noise_var_per_sc[i];
        double llr_scale = (noise_var > 1e-20) ? (2.0 / noise_var) : 1e20;
        double rx_I = creal(syms[i]);
        double rx_Q = cimag(syms[i]);

        for (int bit_pos = 0; bit_pos < bits_per_dim; bit_pos++) {
            double min_sq_dist_I_bit0 = DBL_MAX,  min_sq_dist_I_bit1 = DBL_MAX;
            double min_sq_dist_Q_bit0 = DBL_MAX,  min_sq_dist_Q_bit1 = DBL_MAX;

            for (int j = 0; j < num_levels; j++) {
                int    bit_is_one  = (table[j].bits >> (bits_per_dim - 1 - bit_pos)) & 1;
                double diff_I      = rx_I - levels[j];
                double diff_Q      = rx_Q - levels[j];
                double sq_dist_I   = diff_I * diff_I;
                double sq_dist_Q   = diff_Q * diff_Q;

                if (!bit_is_one) {
                    if (sq_dist_I < min_sq_dist_I_bit0) min_sq_dist_I_bit0 = sq_dist_I;
                    if (sq_dist_Q < min_sq_dist_Q_bit0) min_sq_dist_Q_bit0 = sq_dist_Q;
                } else {
                    if (sq_dist_I < min_sq_dist_I_bit1) min_sq_dist_I_bit1 = sq_dist_I;
                    if (sq_dist_Q < min_sq_dist_Q_bit1) min_sq_dist_Q_bit1 = sq_dist_Q;
                }
            }

            llr[i * bits_per_sym + 2*bit_pos]     = llr_scale * (min_sq_dist_I_bit1 - min_sq_dist_I_bit0);
            llr[i * bits_per_sym + 2*bit_pos + 1] = llr_scale * (min_sq_dist_Q_bit1 - min_sq_dist_Q_bit0);
        }
    }
}

void qpsk_modulate(const int *bits, int num_bits, cx_t *syms) {
    double scale    = 1.0 / sqrt(2.0);
    int    num_syms = num_bits / 2;
    for (int i = 0; i < num_syms; i++) {
        double i_val = (1 - 2 * bits[2*i])     * scale;
        double q_val = (1 - 2 * bits[2*i + 1]) * scale;
        syms[i] = CX_MAKE(i_val, q_val);
    }
}

void qpsk_demodulate(const cx_t *syms, int num_syms, int *bits) {
    for (int i = 0; i < num_syms; i++) {
        bits[2*i]     = (creal(syms[i]) < 0) ? 1 : 0;
        bits[2*i + 1] = (cimag(syms[i]) < 0) ? 1 : 0;
    }
}
