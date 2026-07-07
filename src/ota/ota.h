// OTA update subsystem — Phase 2 (bootloader-backed slot switching).
//
// The application always runs from either slot A or slot B. Which slot
// is active is decided at boot time by the bootloader based on the
// metadata block written here. New images are streamed into the
// *staging* slot (whichever the running slot isn't) and, on
// ota_apply(), the metadata is rewritten to swap active_slot and mark
// the freshly-staged image as "pending verification". The bootloader
// gives the pending image OTA_MAX_BOOT_ATTEMPTS chances to call
// ota_confirm() before rolling back automatically.
//
// The shared flash layout and metadata schema live in
// include/ota_metadata.h.
#ifndef PICOBLE_OTA_H
#define PICOBLE_OTA_H

#include "ota_metadata.h"
#include "system/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_OK              =  0,
    OTA_ERR_BUSY        = -1,
    OTA_ERR_NOT_STARTED = -2,
    OTA_ERR_SIZE        = -3,
    OTA_ERR_RANGE       = -4,
    OTA_ERR_VERIFY      = -5,
    OTA_ERR_UNSUPPORTED = -6,
    OTA_ERR_IO          = -7,
    OTA_ERR_NO_IMAGE    = -8,  // ota_apply with nothing staged
} ota_result_t;

typedef struct {
    uint8_t  active_slot;             // 0 or 1 — the slot we booted into
    uint8_t  staging_slot;             // the other slot
    bool     update_in_progress;
    bool     pending_verify;           // active slot is on probation
    uint8_t  boot_attempts;            // ticks up each pending boot
    bool     image_ready;              // finalize succeeded, digest available
    size_t   staging_size;
    size_t   bytes_written;
    uint8_t  computed_sha256[SHA256_DIGEST_LEN];
} ota_status_t;

// Read metadata + reconcile with PICOBLE_SLOT (compile-time slot id).
// On first boot after a fresh flash where no metadata exists, this
// commits a first-time record. Must be called from main() before any
// other ota_* function.
void ota_init(void);

ota_status_t ota_status(void);

// Erase the staging slot in preparation for a streaming transfer.
// image_size == 0 means "size not known upfront" (used by the HTTP
// downloader when Content-Length isn't sent). Blocks ~8 s.
ota_result_t ota_begin(size_t image_size);

// Sequential byte stream into the staging slot. offset MUST equal
// the total bytes already accepted.
ota_result_t ota_write(size_t offset, const uint8_t *data, size_t len);

// Flush the partial page and finalize the SHA-256.
ota_result_t ota_finalize(void);

// Abandon an in-flight update.
ota_result_t ota_abort(void);

// Commit the staged image as the next-boot slot. Writes updated
// metadata (active_slot flipped, pending_verify=1, boot_attempts=0)
// and reboots. Fails if no image is ready.
ota_result_t ota_apply(void);

// Called from the application once it has satisfied itself that the
// running image is healthy. Clears pending_verify and resets
// boot_attempts, so a subsequent power cycle boots this slot directly.
void ota_confirm(void);

// Optional: compare `expected` (32 bytes) with the digest computed
// during the last transfer. Only valid after ota_finalize.
ota_result_t ota_verify(const uint8_t expected[SHA256_DIGEST_LEN]);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_OTA_H
