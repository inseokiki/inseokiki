/* ================================================================
 *  crc.h
 *  CRC-24A / CRC-24B / CRC-24C generation and checking
 *
 *  Author : Inseok Kang
 * ================================================================ */
#ifndef CRC_H
#define CRC_H

#include <stdint.h>

typedef enum {
    CRC24A = 0,   /* PDSCH TB: poly 0x864CFB */
    CRC24C = 1,   /* PBCH/PDCCH: poly 0xB2B117 */
    CRC16  = 2,   /* large TB:  poly 0x1021 */
    CRC24B = 3    /* code block (TS 38.212 5.2.2, C>1 only): poly 0x800063,
                     confirmed via 3gpp-server MCP TS 38.212 v18.8.0 5.1 image9 */
} CRCType;

int  get_crc_length(CRCType type);

/* Compute CRC bits. out_crc must have get_crc_length(type) elements. */
void compute_crc(const int *data, int data_len, CRCType type, int *out_crc);

/* Append CRC. out must have data_len + get_crc_length(type) elements. */
void attach_crc(const int *data, int data_len, CRCType type, int *out);

/* Return 1 if CRC valid, 0 otherwise. buf_len = data_len + crc_len. */
int check_crc(const int *buf, int buf_len, CRCType type);

/* Attach CRC with RNTI masking (PDCCH). out must be data_len+crc_len. */
void attach_crc_rnti(const int *data, int data_len, CRCType type,
                     uint16_t rnti, int *out);

/* Check CRC with RNTI de-masking. */
int check_crc_rnti(const int *buf, int buf_len, CRCType type, uint16_t rnti);

#endif
