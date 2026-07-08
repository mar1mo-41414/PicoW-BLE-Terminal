// Flash-backed configuration store. See config.h for scope.
//
// One 4 KB sector at the very end of the flash device holds a fixed-
// layout record. Reads copy directly from XIP; writes go through
// flash_safe_execute so we don't race the CYW43 SPI DMA (which shares
// the bus we'd be halting for the erase/program).
#include "storage/config.h"
#include "system/log.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico_ota/crc32.h"    // shared with the OTA framework

#include <string.h>

#ifndef PICO_FLASH_SIZE_BYTES
#error "PICO_FLASH_SIZE_BYTES must be defined by the board header"
#endif

// XIP-relative offset of our sector (last 4 KB of the flash device).
#define CONFIG_SECTOR_OFFSET   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_SECTOR_XIP_ADDR ((const uint8_t *)(XIP_BASE + CONFIG_SECTOR_OFFSET))

#define CONFIG_MAGIC   0x50434347u  // 'PCCG' — Pico Config
#define CONFIG_VERSION 1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    char     ssid[CONFIG_SSID_MAXLEN + 1];
    char     psk[CONFIG_PSK_MAXLEN + 1];
    uint32_t crc32;
} __attribute__((packed)) config_record_t;

_Static_assert(sizeof(config_record_t) <= FLASH_PAGE_SIZE,
               "config record must fit in one 256-byte flash page");

// CRC-32 comes from pico_ota_crc32() — same polynomial as the OTA
// metadata block, so this project has exactly one implementation.

// ---- read path -------------------------------------------------------------

bool config_load_wifi(wifi_creds_t *out) {
    if (!out) return false;

    config_record_t rec;
    // Copy out of XIP into RAM before any consistency check — a torn
    // erase would leave 0xFF bytes here and we want a stable snapshot.
    memcpy(&rec, CONFIG_SECTOR_XIP_ADDR, sizeof(rec));

    if (rec.magic != CONFIG_MAGIC) return false;
    if (rec.version != CONFIG_VERSION) return false;

    uint32_t want = rec.crc32;
    rec.crc32 = 0;
    if (pico_ota_crc32(&rec, sizeof(rec) - sizeof(uint32_t)) != want) return false;

    // Enforce NUL-termination even in the presence of a corrupted record
    // that passed CRC by accident — belt and braces.
    rec.ssid[CONFIG_SSID_MAXLEN] = '\0';
    rec.psk[CONFIG_PSK_MAXLEN]   = '\0';

    memcpy(out->ssid, rec.ssid, sizeof(out->ssid));
    memcpy(out->psk,  rec.psk,  sizeof(out->psk));
    return true;
}

// ---- write path ------------------------------------------------------------

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    size_t len;
} flash_op_t;

static void erase_cb(void *param) {
    flash_op_t *op = param;
    flash_range_erase(op->offset, op->len);
}

static void program_cb(void *param) {
    flash_op_t *op = param;
    flash_range_program(op->offset, op->data, op->len);
}

// One page is always enough for the record; we still allocate 256 B
// because flash_range_program requires page-sized input.
static bool write_record(const config_record_t *rec) {
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));  // erased-flash background pattern
    memcpy(page, rec, sizeof(*rec));

    flash_op_t erase = {
        .offset = CONFIG_SECTOR_OFFSET,
        .data = NULL,
        .len = FLASH_SECTOR_SIZE,
    };
    if (flash_safe_execute(erase_cb, &erase, 500) != PICO_OK) {
        LOGE("config: erase failed");
        return false;
    }

    flash_op_t prog = {
        .offset = CONFIG_SECTOR_OFFSET,
        .data = page,
        .len = FLASH_PAGE_SIZE,
    };
    if (flash_safe_execute(program_cb, &prog, 500) != PICO_OK) {
        LOGE("config: program failed");
        return false;
    }

    return true;
}

bool config_save_wifi(const wifi_creds_t *creds) {
    if (!creds) return false;
    size_t ssid_len = strnlen(creds->ssid, sizeof(creds->ssid));
    size_t psk_len  = strnlen(creds->psk,  sizeof(creds->psk));
    if (ssid_len == 0 || ssid_len > CONFIG_SSID_MAXLEN) return false;
    if (psk_len  == 0 || psk_len  > CONFIG_PSK_MAXLEN)  return false;

    config_record_t rec = {0};
    rec.magic   = CONFIG_MAGIC;
    rec.version = CONFIG_VERSION;
    memcpy(rec.ssid, creds->ssid, ssid_len);
    memcpy(rec.psk,  creds->psk,  psk_len);
    rec.crc32 = 0;
    rec.crc32 = pico_ota_crc32(&rec, sizeof(rec) - sizeof(uint32_t));

    return write_record(&rec);
}

bool config_clear_wifi(void) {
    // A wiped sector reads as all 0xFF, so magic won't match — that's
    // enough to make config_load_wifi() return false on the next call.
    flash_op_t erase = {
        .offset = CONFIG_SECTOR_OFFSET,
        .data = NULL,
        .len = FLASH_SECTOR_SIZE,
    };
    if (flash_safe_execute(erase_cb, &erase, 500) != PICO_OK) {
        LOGE("config: erase failed");
        return false;
    }
    return true;
}
