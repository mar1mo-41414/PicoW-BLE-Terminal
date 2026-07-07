// OTA update subsystem — scaffolding.
//
// This file exists to lock in the *interface* the rest of the firmware
// will use once real OTA lands. The current implementation is a stub
// that reports its status and refuses writes, so callers (the `ota`
// command, future Wi-Fi update fetchers) can already be written against
// the final API without waiting for the bootloader work.
//
// Design intent, still to be implemented:
//
//   Flash layout on Pico W (2 MB, 4 KB sectors, RP2040):
//     0x000000 - 0x0BFFFF   (768 KB)  slot A  (currently running image)
//     0x0C0000 - 0x17FFFF   (768 KB)  slot B  (staging area for update)
//     0x180000 - 0x183FFF   ( 16 KB)  metadata (image hash, boot flags)
//     0x184000 - 0x1FFFFF   (~496 KB) reserved for filesystem / logs
//
//   Boot flow:
//     - Second-stage boot verifies the metadata block, hashes the
//       "active" slot, and if the hash matches jumps into that slot.
//     - If the active slot is marked "pending verification" and the app
//       does not confirm itself within N seconds after boot, the boot
//       flag is flipped back to the other slot and the device reboots.
//     - `ota_confirm()` clears the "pending" flag once the new image is
//       running normally.
//
//   The write path here reserves the API surface but does no flashing
//   until the bootloader half is in place.
#ifndef PICOBLE_OTA_H
#define PICOBLE_OTA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Slot layout — all offsets are relative to the start of flash
// (XIP_BASE) so they can be fed straight into hardware_flash APIs.
#define OTA_SLOT_A_OFFSET       0x00000000u
#define OTA_SLOT_SIZE           (768u * 1024u)
#define OTA_SLOT_B_OFFSET       (OTA_SLOT_A_OFFSET + OTA_SLOT_SIZE)
#define OTA_METADATA_OFFSET     (OTA_SLOT_B_OFFSET + OTA_SLOT_SIZE)
#define OTA_METADATA_SIZE       (16u * 1024u)

// Human-readable identifiers for the two slots.
typedef enum {
    OTA_SLOT_A = 0,
    OTA_SLOT_B = 1,
} ota_slot_t;

// Result codes. Anything other than OTA_OK is an error and should be
// surfaced to the caller.
typedef enum {
    OTA_OK              = 0,
    OTA_ERR_BUSY        = -1,   // begin() called mid-update
    OTA_ERR_NOT_STARTED = -2,   // write/finalize with no begin
    OTA_ERR_SIZE        = -3,   // requested image > slot capacity
    OTA_ERR_RANGE       = -4,   // offset+len out of slot bounds
    OTA_ERR_VERIFY      = -5,   // integrity check failed
    OTA_ERR_UNSUPPORTED = -6,   // not implemented on this build
    OTA_ERR_IO          = -7,   // flash op failed
} ota_result_t;

typedef struct {
    ota_slot_t active_slot;    // slot currently running
    ota_slot_t staging_slot;   // slot the update writes into
    bool       update_in_progress;
    bool       pending_verify; // active slot is still on probation
    size_t     staging_size;   // total size announced by ota_begin()
    size_t     bytes_written;
} ota_status_t;

// One-time setup — reads metadata, decides which slot is active,
// clears any half-finished update state.
void ota_init(void);

// Read-only status snapshot.
ota_status_t ota_status(void);

// Start a new update. `image_size` is the total number of bytes the
// caller will provide via ota_write(). Returns OTA_ERR_BUSY if an
// update is already open, OTA_ERR_SIZE if the image is too big,
// OTA_ERR_UNSUPPORTED if OTA is disabled at build time.
ota_result_t ota_begin(size_t image_size);

// Stream a chunk into the staging slot. `offset` is relative to the
// start of the staging slot. Chunks may arrive out of order but must
// eventually cover the entire announced range.
ota_result_t ota_write(size_t offset, const uint8_t *data, size_t len);

// Complete the transfer: verify the staged image, flip the boot flag
// so the next reboot lands on the new slot, and mark the image as
// "pending verification".
ota_result_t ota_finalize(void);

// Cancel an in-progress update and reclaim the staging slot.
ota_result_t ota_abort(void);

// Called by application code once it has satisfied itself that the
// running image is healthy. Clears the "pending verification" flag.
// Safe to call outside an update cycle (no-op).
void ota_confirm(void);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_OTA_H
