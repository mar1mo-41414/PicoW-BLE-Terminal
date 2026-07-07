// OTA scaffolding. The API is real; the write path is a stub that
// records state so callers can be exercised end-to-end but no flash is
// touched. Replace the OTA_ERR_UNSUPPORTED returns with real flash ops
// once the second-stage bootloader half of this design is in place.
#include "ota/ota.h"
#include "system/log.h"

#include <string.h>

static ota_status_t g_status = {
    .active_slot         = OTA_SLOT_A,
    .staging_slot        = OTA_SLOT_B,
    .update_in_progress  = false,
    .pending_verify      = false,
    .staging_size        = 0,
    .bytes_written       = 0,
};

void ota_init(void) {
    // TODO: read persistent metadata block and populate active_slot /
    // pending_verify. For now we assume slot A is active on every boot.
    memset(&g_status, 0, sizeof(g_status));
    g_status.active_slot  = OTA_SLOT_A;
    g_status.staging_slot = OTA_SLOT_B;
    LOGI("ota: init (active=slot%c, unsupported build)",
         g_status.active_slot == OTA_SLOT_A ? 'A' : 'B');
}

ota_status_t ota_status(void) {
    return g_status;
}

ota_result_t ota_begin(size_t image_size) {
    if (g_status.update_in_progress) return OTA_ERR_BUSY;
    if (image_size == 0 || image_size > OTA_SLOT_SIZE) return OTA_ERR_SIZE;

    g_status.update_in_progress = true;
    g_status.staging_size       = image_size;
    g_status.bytes_written      = 0;
    LOGI("ota: begin size=%u", (unsigned)image_size);

    // Real path would erase the staging slot here (in 4KB sectors,
    // interrupts-off, using flash_range_erase). Not implemented yet.
    return OTA_ERR_UNSUPPORTED;
}

ota_result_t ota_write(size_t offset, const uint8_t *data, size_t len) {
    if (!g_status.update_in_progress) return OTA_ERR_NOT_STARTED;
    if (!data || len == 0) return OTA_ERR_RANGE;
    if (offset + len > g_status.staging_size) return OTA_ERR_RANGE;

    // Real path: buffer up to a 4KB page boundary, then flash_range_program
    // with interrupts and BLE polling paused. Not implemented yet.
    g_status.bytes_written += len;
    return OTA_ERR_UNSUPPORTED;
}

ota_result_t ota_finalize(void) {
    if (!g_status.update_in_progress) return OTA_ERR_NOT_STARTED;

    // Real path: hash the staged image, verify against a signature or
    // checksum embedded in the transfer, then rewrite the metadata block
    // to flip active_slot and set pending_verify. Reboot is the caller's
    // choice (typically `ota update` then `reboot`).
    g_status.update_in_progress = false;
    return OTA_ERR_UNSUPPORTED;
}

ota_result_t ota_abort(void) {
    if (!g_status.update_in_progress) return OTA_OK;
    g_status.update_in_progress = false;
    g_status.staging_size = 0;
    g_status.bytes_written = 0;
    LOGI("ota: abort");
    return OTA_OK;
}

void ota_confirm(void) {
    if (g_status.pending_verify) {
        g_status.pending_verify = false;
        LOGI("ota: image confirmed");
    }
}
