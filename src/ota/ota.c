// OTA — real flash write path for the staging slot.
//
// Uses hardware_flash + pico_flash's flash_safe_execute to erase and
// program without racing the CYW43 SPI driver. Streaming API buffers
// into 256-byte pages and hashes on the fly so nothing needs to be
// re-read from flash at finalize time.
#include "ota/ota.h"
#include "system/log.h"
#include "system/sha256.h"

#include "hardware/flash.h"
#include "pico/flash.h"

#include <string.h>

// ---- state ------------------------------------------------------------------

static ota_status_t g_status;
static sha256_ctx_t g_sha;

// Working page buffer. Anything less than a full page has to sit here
// until either another chunk fills it or ota_finalize pads it.
static uint8_t g_page_buf[FLASH_PAGE_SIZE];
static size_t  g_page_buf_len = 0;

// Next-page offset within slot B (0, 256, 512, ...).
static uint32_t g_next_page_off = 0;

// ---- flash_safe_execute glue ------------------------------------------------

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    size_t len;
} flash_op_t;

static void erase_cb(void *arg) {
    flash_op_t *op = arg;
    flash_range_erase(op->offset, op->len);
}

static void program_cb(void *arg) {
    flash_op_t *op = arg;
    flash_range_program(op->offset, op->data, op->len);
}

static bool erase_range(uint32_t offset, size_t len) {
    flash_op_t op = { .offset = offset, .len = len };
    int rc = flash_safe_execute(erase_cb, &op, 1000);
    if (rc != PICO_OK) {
        LOGE("ota: erase failed rc=%d off=%08lx", rc, (unsigned long)offset);
        return false;
    }
    return true;
}

static bool program_page(uint32_t offset, const uint8_t page[FLASH_PAGE_SIZE]) {
    flash_op_t op = { .offset = offset, .data = page, .len = FLASH_PAGE_SIZE };
    int rc = flash_safe_execute(program_cb, &op, 500);
    if (rc != PICO_OK) {
        LOGE("ota: program failed rc=%d off=%08lx", rc, (unsigned long)offset);
        return false;
    }
    return true;
}

// ---- lifecycle --------------------------------------------------------------

void ota_init(void) {
    memset(&g_status, 0, sizeof(g_status));
    g_status.active_slot  = OTA_SLOT_A;
    g_status.staging_slot = OTA_SLOT_B;
    g_page_buf_len  = 0;
    g_next_page_off = 0;
    LOGI("ota: init (active=slot%c)",
         g_status.active_slot == OTA_SLOT_A ? 'A' : 'B');
}

ota_status_t ota_status(void) { return g_status; }

// ---- begin ------------------------------------------------------------------

ota_result_t ota_begin(size_t image_size) {
    if (g_status.update_in_progress) return OTA_ERR_BUSY;
    // image_size == 0 is a legal "unknown yet — I'll tell you at write
    // time" signal, used by the HTTP downloader which only learns the
    // Content-Length after the response headers arrive.
    if (image_size > OTA_SLOT_SIZE) return OTA_ERR_SIZE;

    LOGI("ota: begin size=%u — erasing %u KB",
         (unsigned)image_size, (unsigned)(OTA_SLOT_SIZE / 1024));

    // Erase one sector at a time so each flash_safe_execute call is
    // short (~40 ms). If we requested the whole 768 KB in one call,
    // the CYW43 driver's coordination window would time out and BLE
    // would definitely drop.
    for (uint32_t off = 0; off < OTA_SLOT_SIZE; off += FLASH_SECTOR_SIZE) {
        if (!erase_range(OTA_SLOT_B_OFFSET + off, FLASH_SECTOR_SIZE)) {
            return OTA_ERR_IO;
        }
    }

    g_status.update_in_progress = true;
    g_status.image_ready        = false;
    g_status.staging_size       = image_size;
    g_status.bytes_written      = 0;
    g_page_buf_len              = 0;
    g_next_page_off             = 0;
    memset(g_status.computed_sha256, 0, sizeof(g_status.computed_sha256));

    sha256_init(&g_sha);
    LOGI("ota: staging slot erased, ready to receive");
    return OTA_OK;
}

// ---- write ------------------------------------------------------------------

ota_result_t ota_write(size_t offset, const uint8_t *data, size_t len) {
    if (!g_status.update_in_progress) return OTA_ERR_NOT_STARTED;
    if (!data || len == 0)             return OTA_ERR_RANGE;
    if (offset != g_status.bytes_written)     return OTA_ERR_RANGE;
    // staging_size == 0 means "declared unknown"; treat OTA_SLOT_SIZE
    // as the hard ceiling instead.
    size_t limit = g_status.staging_size ? g_status.staging_size : OTA_SLOT_SIZE;
    if (offset + len > limit)                 return OTA_ERR_RANGE;

    sha256_update(&g_sha, data, len);
    g_status.bytes_written += len;

    while (len > 0) {
        size_t space = FLASH_PAGE_SIZE - g_page_buf_len;
        size_t take  = len < space ? len : space;
        memcpy(g_page_buf + g_page_buf_len, data, take);
        g_page_buf_len += take;
        data += take;
        len  -= take;

        if (g_page_buf_len == FLASH_PAGE_SIZE) {
            if (!program_page(OTA_SLOT_B_OFFSET + g_next_page_off, g_page_buf)) {
                return OTA_ERR_IO;
            }
            g_next_page_off += FLASH_PAGE_SIZE;
            g_page_buf_len = 0;
        }
    }
    return OTA_OK;
}

// ---- finalize ---------------------------------------------------------------

ota_result_t ota_finalize(void) {
    if (!g_status.update_in_progress) return OTA_ERR_NOT_STARTED;

    // Pad and flush the trailing partial page.
    if (g_page_buf_len > 0) {
        memset(g_page_buf + g_page_buf_len, 0xFF,
               FLASH_PAGE_SIZE - g_page_buf_len);
        if (!program_page(OTA_SLOT_B_OFFSET + g_next_page_off, g_page_buf)) {
            return OTA_ERR_IO;
        }
        g_next_page_off += FLASH_PAGE_SIZE;
        g_page_buf_len = 0;
    }

    sha256_final(&g_sha, g_status.computed_sha256);

    g_status.update_in_progress = false;
    g_status.image_ready        = true;
    LOGI("ota: finalize ok, %u bytes staged",
         (unsigned)g_status.bytes_written);
    return OTA_OK;
}

// ---- abort ------------------------------------------------------------------

ota_result_t ota_abort(void) {
    if (!g_status.update_in_progress) return OTA_OK;
    g_status.update_in_progress = false;
    g_status.image_ready        = false;
    g_status.staging_size       = 0;
    g_status.bytes_written      = 0;
    g_page_buf_len              = 0;
    g_next_page_off             = 0;
    memset(g_status.computed_sha256, 0, sizeof(g_status.computed_sha256));
    LOGI("ota: abort");
    return OTA_OK;
}

// ---- confirm / verify -------------------------------------------------------

void ota_confirm(void) {
    if (g_status.pending_verify) {
        g_status.pending_verify = false;
        LOGI("ota: image confirmed");
    }
}

ota_result_t ota_verify(const uint8_t expected[SHA256_DIGEST_LEN]) {
    if (!g_status.image_ready) return OTA_ERR_NOT_STARTED;
    if (!expected) return OTA_ERR_RANGE;
    return sha256_equal(g_status.computed_sha256, expected)
        ? OTA_OK : OTA_ERR_VERIFY;
}
