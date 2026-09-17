/* ================================================================
 *  tdl_tables.c
 *  TS 38.901 Table 7.7.2-1..5 data -- see tdl_tables.h for provenance
 *  and scope notes.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#include "tdl_tables.h"
#include <ctype.h>
#include <stddef.h>

static const TDLTapSpec TDL_A_TAPS[] = {
    { 0.0000, -13.40 },
    { 0.3819,   0.00 },
    { 0.4025,  -2.20 },
    { 0.5868,  -4.00 },
    { 0.4610,  -6.00 },
    { 0.5375,  -8.20 },
    { 0.6708,  -9.90 },
    { 0.5750, -10.50 },
    { 0.7618,  -7.50 },
    { 1.5375, -15.90 },
    { 1.8978,  -6.60 },
    { 2.2242, -16.70 },
    { 2.1718, -12.40 },
    { 2.4942, -15.20 },
    { 2.5119, -10.80 },
    { 3.0582, -11.30 },
    { 4.0810, -12.70 },
    { 4.4579, -16.20 },
    { 4.5695, -18.30 },
    { 4.7966, -18.90 },
    { 5.0066, -16.60 },
    { 5.3043, -19.90 },
    { 9.6586, -29.70 },
};

static const TDLTapSpec TDL_B_TAPS[] = {
    { 0.0000,   0.00 },
    { 0.1072,  -2.20 },
    { 0.2155,  -4.00 },
    { 0.2095,  -3.20 },
    { 0.2870,  -9.80 },
    { 0.2986,  -1.20 },
    { 0.3752,  -3.40 },
    { 0.5055,  -5.20 },
    { 0.3681,  -7.60 },
    { 0.3697,  -3.00 },
    { 0.5700,  -8.90 },
    { 0.5283,  -9.00 },
    { 1.1021,  -4.80 },
    { 1.2756,  -5.70 },
    { 1.5474,  -7.50 },
    { 1.7842,  -1.90 },
    { 2.0169,  -7.60 },
    { 2.8294, -12.20 },
    { 3.0219,  -9.80 },
    { 3.6187, -11.40 },
    { 4.1067, -14.90 },
    { 4.2790,  -9.20 },
    { 4.7834, -11.30 },
};

static const TDLTapSpec TDL_C_TAPS[] = {
    { 0.0000,  -4.40 },
    { 0.2099,  -1.20 },
    { 0.2219,  -3.50 },
    { 0.2329,  -5.20 },
    { 0.2176,  -2.50 },
    { 0.6366,   0.00 },
    { 0.6448,  -2.20 },
    { 0.6560,  -3.90 },
    { 0.6584,  -7.40 },
    { 0.7935,  -7.10 },
    { 0.8213, -10.70 },
    { 0.9336, -11.10 },
    { 1.2285,  -5.10 },
    { 1.3083,  -6.80 },
    { 2.1704,  -8.70 },
    { 2.7105, -13.20 },
    { 4.2589, -13.90 },
    { 4.6003, -13.90 },
    { 5.4902, -15.80 },
    { 5.6077, -17.10 },
    { 6.3065, -16.00 },
    { 6.6374, -15.70 },
    { 7.0427, -21.60 },
    { 8.6523, -22.80 },
};

/* TDL-D tap 0 also carries a LOS (Rician) component at normalized delay
 * 0.0000, power -0.20 dB, K1=13.3dB (= -0.20 - (-13.50), see tdl_tables.h) --
 * that LOS mean is NOT one of these array entries, it lives in
 * TDL_D_PROFILE.los_power_db below. */
static const TDLTapSpec TDL_D_TAPS[] = {
    {  0.0000, -13.50 },
    {  0.0350, -18.80 },
    {  0.6120, -21.00 },
    {  1.3630, -22.80 },
    {  1.4050, -17.90 },
    {  1.8040, -20.10 },
    {  2.5960, -21.90 },
    {  1.7750, -22.90 },
    {  4.0420, -27.80 },
    {  7.9370, -23.60 },
    {  9.4240, -24.80 },
    {  9.7080, -30.00 },
    { 12.5250, -27.70 },
};

/* TDL-E tap 0 also carries a LOS (Rician) component at normalized delay
 * 0.0000, power -0.03 dB, K1=22dB (= -0.03 - (-22.03), see tdl_tables.h). */
static const TDLTapSpec TDL_E_TAPS[] = {
    {  0.0000, -22.03 },
    {  0.5133, -15.80 },
    {  0.5440, -18.10 },
    {  0.5630, -19.80 },
    {  0.5440, -22.90 },
    {  0.7112, -22.40 },
    {  1.9092, -18.60 },
    {  1.9293, -20.80 },
    {  1.9589, -22.60 },
    {  2.6426, -22.30 },
    {  3.7136, -25.60 },
    {  5.4524, -20.20 },
    { 12.0034, -29.80 },
    { 20.6519, -29.20 },
};

#define NTAPS(arr) ((int)(sizeof(arr) / sizeof((arr)[0])))

static const TDLProfileSpec TDL_A_PROFILE = { TDL_A_TAPS, NTAPS(TDL_A_TAPS), 0, 0.0 };
static const TDLProfileSpec TDL_B_PROFILE = { TDL_B_TAPS, NTAPS(TDL_B_TAPS), 0, 0.0 };
static const TDLProfileSpec TDL_C_PROFILE = { TDL_C_TAPS, NTAPS(TDL_C_TAPS), 0, 0.0 };
static const TDLProfileSpec TDL_D_PROFILE = { TDL_D_TAPS, NTAPS(TDL_D_TAPS), 1, -0.20 };
static const TDLProfileSpec TDL_E_PROFILE = { TDL_E_TAPS, NTAPS(TDL_E_TAPS), 1, -0.03 };

const TDLProfileSpec *tdl_profile_lookup(char letter) {
    switch (toupper((unsigned char)letter)) {
        case 'A': return &TDL_A_PROFILE;
        case 'B': return &TDL_B_PROFILE;
        case 'C': return &TDL_C_PROFILE;
        case 'D': return &TDL_D_PROFILE;
        case 'E': return &TDL_E_PROFILE;
        default:  return NULL;
    }
}
