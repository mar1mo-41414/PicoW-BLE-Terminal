// Table-free IEEE CRC-32. Small footprint, low frequency of use — we
// only checksum records on the order of ~100 bytes, so speed doesn't
// matter.
#include "pico_ota/crc32.h"

uint32_t pico_ota_crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}
