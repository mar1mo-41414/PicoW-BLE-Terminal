// IEEE CRC-32 used by pico-ota for metadata integrity checks.
//
// Kept as a single shared implementation so the bootloader and the
// application can't drift bit-for-bit. Also exposed in the public
// header so downstream apps (this project's storage/config.c, etc.)
// can reuse it without a second copy.
#ifndef PICO_OTA_CRC32_H
#define PICO_OTA_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Standard IEEE CRC-32 (polynomial 0xEDB88320, reflected).
uint32_t pico_ota_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // PICO_OTA_CRC32_H
