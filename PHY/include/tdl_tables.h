/* ================================================================
 *  tdl_tables.h
 *  TS 38.901 Table 7.7.2-1..5 -- TDL-A/B/C/D/E tap delay/power profiles
 *  (data only, no logic)
 *
 *  Transcribed programmatically (not by hand) from the local spec copy
 *  at 3gpp/38901-h10/38901-h10.docx (word/document.xml table cells,
 *  zipfile+regex extraction, same method as ldpc_tables.c/polar_tables.c,
 *  2026-09-15) to eliminate hand-transcription typo risk. Unlike the
 *  LDPC/Polar base-graph tables, these five tables rendered as plain
 *  WordprocessingML text (no OLE/MathType images), so the numeric values
 *  below are a direct, mechanical transcription of the primary source's
 *  own cell text -- not independently cross-checked against a second
 *  source, since there was no OCR/image-reading uncertainty to corroborate
 *  away (contrast tdl.h's note on the delay-scaling formula itself, whose
 *  equation *is* an embedded image and is corroborated by surrounding
 *  descriptive text only).
 *
 *  TDL-A/B/C are pure NLOS (all taps Rayleigh, TS 38.901 Table 7.7.2-1/
 *  2/3, 23/23/24 taps respectively). TDL-D/E (Table 7.7.2-4/5, 13/14
 *  Rayleigh taps each) additionally carry a LOS (Rician) component
 *  co-located with tap index 0's delay (normalized delay 0.0000) --
 *  represented here as a separate `los_power_db` field on TDLProfileSpec
 *  rather than as an extra array entry, since it shares tap 0's delay
 *  rather than owning its own. The primary source states this LOS
 *  component's K-factor directly (K1=13.3dB for TDL-D, K1=22dB for
 *  TDL-E) in the table's own note text -- confirmed algebraically to
 *  equal (this struct's) los_power_db - taps[0].power_db exactly
 *  (e.g. TDL-D: -0.2 - (-13.5) = 13.3), so no separate derivation is
 *  needed at runtime; both dB values are used directly as given.
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef TDL_TABLES_H
#define TDL_TABLES_H

typedef struct {
    double delay_norm;  /* normalized delay (dimensionless), TS 38.901
                            §7.7.3 eq.(7.7-1): scaled_delay_ns =
                            delay_norm * desired_RMS_delay_spread_ns */
    double power_db;    /* relative power [dB], NOT yet normalized so the
                            profile's total linear power sums to 1 --
                            tdl_tables.c's raw table sums range ~0.3dB
                            (TDL-D/E) to ~8.5dB (TDL-B) above 0dB, so
                            callers must renormalize (same requirement
                            this project's previous approximate profile
                            already had) */
} TDLTapSpec;

typedef struct {
    const TDLTapSpec *taps;
    int    num_taps;       /* Rayleigh/diffuse tap count: 23/23/24/13/14
                               for A/B/C/D/E */
    int    has_los;         /* 1 for TDL-D/E, 0 for A/B/C */
    double los_power_db;    /* only meaningful if has_los: extra Rician
                                mean component co-located with taps[0]'s
                                delay (0.0000), same dB reference as
                                taps[].power_db */
} TDLProfileSpec;

/* Looks up a TS 38.901 TDL profile by its letter ('A'..'E', case-
   insensitive). Returns NULL for anything else -- callers must reject
   that as a config error, not silently fall back to a default profile. */
const TDLProfileSpec *tdl_profile_lookup(char letter);

#endif
