// OTA update subsystem.
//
// This build implements *Phase 1* — download-and-stage. It reserves the
// interface (begin/write/finalize/abort/confirm/status/verify) and
// exercises the flash write path end-to-end into slot B, but it does
// not yet flip the boot slot. Applying the image needs a bootloader,
// which is Phase 2.
//
// Flash layout on Pico W (2 MB, 4 KB sectors, RP2040):
//   0x000000 - 0x0BFFFF   (768 KB)  slot A  (currently running image)
//   0x0C0000 - 0x17FFFF   (768 KB)  slot B  (OTA staging area)
//   0x180000 - 0x183FFF   ( 16 KB)  metadata (future: boot flags, hash)
//   0x184000 - 0x1FEFFF   (~492 KB) reserved for future filesystem/logs
//   0x1FF000 - 0x1FFFFF   (  4 KB)  Wi-Fi credential store (storage/config)
//
// Streaming model: callers feed bytes with strictly increasing offsets
// starting at 0. Chunks may be any size; the OTA layer buffers into
// 256-byte flash pages and programs them as they fill. SHA-256 is
// computed inline while data flows in, so ota_finalize() does not need
// to re-read the staged image from flash.
#ifndef PICOBLE_OTA_H
#define PICOBLE_OTA_H

#include "system/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_SLOT_A_OFFSET       0x00000000u
#define OTA_SLOT_SIZE           (768u * 1024u)
#define OTA_SLOT_B_OFFSET       (OTA_SLOT_A_OFFSET + OTA_SLOT_SIZE)
#define OTA_METADATA_OFFSET     (OTA_SLOT_B_OFFSET + OTA_SLOT_SIZE)
#define OTA_METADATA_SIZE       (16u * 1024u)

typedef enum {
    OTA_SLOT_A = 0,
    OTA_SLOT_B = 1,
} ota_slot_t;

typedef enum {
    OTA_OK              = 0,
    OTA_ERR_BUSY        = -1,
    OTA_ERR_NOT_STARTED = -2,
    OTA_ERR_SIZE        = -3,
    OTA_ERR_RANGE       = -4,
    OTA_ERR_VERIFY      = -5,
    OTA_ERR_UNSUPPORTED = -6,
    OTA_ERR_IO          = -7,
} ota_result_t;

typedef struct {
    ota_slot_t active_slot;
    ota_slot_t staging_slot;
    bool       update_in_progress;
    bool       pending_verify;
    bool       image_ready;    // finalize succeeded, digest available
    size_t     staging_size;
    size_t     bytes_written;
    uint8_t    computed_sha256[SHA256_DIGEST_LEN];
} ota_status_t;

void         ota_init(void);
ota_status_t ota_status(void);

// Erase the staging slot and prepare for a fresh transfer. Blocking —
// takes ~8 seconds because 192 sectors × ~40 ms erase each. BLE peers
// may see a disconnect during that window; that's expected.
ota_result_t ota_begin(size_t image_size);

// Stream a chunk into the staging slot. `offset` MUST equal the total
// bytes previously accepted — this implementation only handles strictly
// sequential transfers, which is what an HTTP GET body naturally is.
ota_result_t ota_write(size_t offset, const uint8_t *data, size_t len);

// Flush the last partial page, complete the SHA-256, mark the image as
// ready. Digest is available via ota_status().computed_sha256.
ota_result_t ota_finalize(void);

// Discard the in-flight update; the staging slot keeps its stale data
// until the next ota_begin() re-erases it.
ota_result_t ota_abort(void);

// Application call — once the currently-running image is happy with
// itself, clears the "pending verification" flag so the bootloader
// doesn't roll back to the previous slot on the next power cycle.
void ota_confirm(void);

// Compare `expected` (32 bytes) with the digest computed during the
// transfer. Only valid after ota_finalize() succeeded.
ota_result_t ota_verify(const uint8_t expected[SHA256_DIGEST_LEN]);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_OTA_H
