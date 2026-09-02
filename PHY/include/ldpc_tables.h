/* ================================================================
 *  ldpc_tables.h
 *  TS 38.212 Table 5.3.2-1/5.3.2-2/5.3.2-3 -- NR LDPC base graph shift
 *  coefficients and lifting-size sets (data only, no logic)
 *
 *  Transcribed programmatically (not by hand) from the local spec copy
 *  at 3gpp/38212-hc0/38212-hc0.docx (word/document.xml table cells,
 *  zipfile+regex extraction, 2026-09-02) to eliminate hand-transcription
 *  typo risk across the ~4100 numeric shift coefficients. Verified after
 *  extraction (see ldpc_nr.c/ldpc_tables self-check paths): BG1_TABLE has
 *  exactly 316 entries covering base-graph rows 0-45/cols 0-67 with no
 *  duplicate (row,col) pairs; BG2_TABLE has exactly 197 entries covering
 *  rows 0-41/cols 0-51, same uniqueness; for both tables, every parity
 *  column at index >= base_info_cols+4 has degree exactly 1 with shift 0
 *  at every one of the 8 lifting-size sets (the literal identity/
 *  "diagonal" part of the NR LDPC parity structure), and base-graph rows
 *  0-3 never reference those columns (confirming the small closed
 *  "core" 4x4 circulant-block system used by the Richardson-Urbanke-
 *  style structured encoder in ldpc_nr.c).
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef LDPC_TABLES_H
#define LDPC_TABLES_H

/* One nonzero entry of a base graph: base-graph row/col index, and the
 * base shift coefficient for each of the 8 lifting-size sets (iLS=0..7,
 * TS 38.212 Table 5.3.2-1) -- actual circulant shift for a chosen Zc is
 * (this value) mod Zc, per TS 38.212 5.3.2. row/col fit in unsigned char
 * (max 67), shift fits in short (max ~380). */
typedef struct {
    unsigned char row;
    unsigned char col;
    short shift[8];
} BGEntry;

#define BG1_ROWS 46
#define BG1_COLS 68
#define BG1_INFO_COLS 22
#define BG1_NUM_ENTRIES 316

#define BG2_ROWS 42
#define BG2_COLS 52
#define BG2_INFO_COLS 10
#define BG2_NUM_ENTRIES 197

extern const BGEntry BG1_TABLE[BG1_NUM_ENTRIES];
extern const BGEntry BG2_TABLE[BG2_NUM_ENTRIES];

/* TS 38.212 Table 5.3.2-1: 8 lifting-size sets, 51 distinct Zc values
 * total (directly counted from the extracted table -- not 52). */
#define ZC_NUM_SETS 8
extern const int ZC_SETS[ZC_NUM_SETS][8];
extern const int ZC_SET_LEN[ZC_NUM_SETS];

#endif
