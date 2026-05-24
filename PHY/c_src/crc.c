#include "crc.h"
#include <stdlib.h>
#include <string.h>

static unsigned int get_poly(CRCType type) {
    switch (type) {
        case CRC24A: return 0x864CFB;
        case CRC24C: return 0xB2B117;
        case CRC16:  return 0x1021;
        default:     return 0;
    }
}

int get_crc_length(CRCType type) {
    switch (type) {
        case CRC24A: return 24;
        case CRC24C: return 24;
        case CRC16:  return 16;
        default:     return 0;
    }
}

void compute_crc(const int *data, int data_len, CRCType type, int *out_crc) {
    int crc_len = get_crc_length(type);
    unsigned int poly = get_poly(type);
    unsigned int mask = (crc_len == 24) ? 0xFFFFFFu : 0xFFFFu;
    unsigned int reg  = 0;

    for (int i = 0; i < data_len; i++) {
        unsigned int msb = (reg >> (crc_len - 1)) & 1u;
        reg = ((reg << 1) | (unsigned int)data[i]) & mask;
        if (msb) reg ^= poly;
    }
    for (int i = 0; i < crc_len; i++) {
        unsigned int msb = (reg >> (crc_len - 1)) & 1u;
        reg = (reg << 1) & mask;
        if (msb) reg ^= poly;
    }
    for (int i = 0; i < crc_len; i++)
        out_crc[i] = (int)((reg >> (crc_len - 1 - i)) & 1u);
}

void attach_crc(const int *data, int data_len, CRCType type, int *out) {
    memcpy(out, data, data_len * sizeof(int));
    compute_crc(data, data_len, type, out + data_len);
}

int check_crc(const int *buf, int buf_len, CRCType type) {
    int crc_len  = get_crc_length(type);
    int data_len = buf_len - crc_len;
    if (data_len <= 0) return 0;
    int computed[24];
    compute_crc(buf, data_len, type, computed);
    for (int i = 0; i < crc_len; i++)
        if (buf[data_len + i] != computed[i]) return 0;
    return 1;
}

void attach_crc_rnti(const int *data, int data_len, CRCType type,
                     uint16_t rnti, int *out) {
    int crc_len = get_crc_length(type);
    int crc[24];
    compute_crc(data, data_len, type, crc);
    for (int i = 0; i < 16; i++) {
        int rb = (rnti >> (15 - i)) & 1;
        crc[crc_len - 16 + i] ^= rb;
    }
    memcpy(out, data, data_len * sizeof(int));
    memcpy(out + data_len, crc, crc_len * sizeof(int));
}

int check_crc_rnti(const int *buf, int buf_len, CRCType type, uint16_t rnti) {
    int crc_len  = get_crc_length(type);
    int data_len = buf_len - crc_len;
    if (data_len <= 0) return 0;
    int computed[24];
    compute_crc(buf, data_len, type, computed);
    for (int i = 0; i < 16; i++) {
        int rb = (rnti >> (15 - i)) & 1;
        computed[crc_len - 16 + i] ^= rb;
    }
    for (int i = 0; i < crc_len; i++)
        if (buf[data_len + i] != computed[i]) return 0;
    return 1;
}
