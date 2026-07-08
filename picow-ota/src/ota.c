// pico-ota — application-side implementation. See pico_ota/ota.h.
#include "pico_ota/ota.h"
#include "pico_ota/log.h"
#include "pico_ota/sha256.h"
#include "pico_ota/crc32.h"

#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/time.h"

#include <string.h>

// The application knows at compile time which slot linker script it
// was built with — that determines which flash offset every literal
// address points into. If we ever boot with a metadata record that
// disagrees, the bootloader made a mistake and we surface a warning.
#ifndef PICO_OTA_SLOT
#error "PICO_OTA_SLOT must be defined by the build (0=A, 1=B). Use pico_ota_set_slot()."
#endif

// ---- state -----------------------------------------------------------------

static ota_status_t g_status;
static sha256_ctx_t g_sha;

static uint8_t  g_page_buf[FLASH_PAGE_SIZE];
static size_t   g_page_buf_len   = 0;
static uint32_t g_next_page_off  = 0;

// XIP view of the metadata block.
static const volatile ota_metadata_t *xip_meta =
    (const volatile ota_metadata_t *)(XIP_BASE + OTA_METADATA_OFFSET);

// ---- flash_safe_execute glue -----------------------------------------------

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
        pico_ota_log_error("ota: erase failed rc=%d off=%08lx", rc, (unsigned long)offset);
        return false;
    }
    return true;
}

static bool program_page(uint32_t offset, const uint8_t page[FLASH_PAGE_SIZE]) {
    flash_op_t op = { .offset = offset, .data = page, .len = FLASH_PAGE_SIZE };
    int rc = flash_safe_execute(program_cb, &op, 500);
    if (rc != PICO_OK) {
        pico_ota_log_error("ota: program failed rc=%d off=%08lx", rc, (unsigned long)offset);
        return false;
    }
    return true;
}

// ---- metadata I/O ----------------------------------------------------------

static bool read_metadata(ota_metadata_t *out) {
    memcpy(out, (const void *)xip_meta, sizeof(*out));
    if (out->magic   != OTA_META_MAGIC)   return false;
    if (out->version != OTA_META_VERSION) return false;
    uint32_t expected = out->crc32;
    out->crc32 = 0;
    return pico_ota_crc32(out, sizeof(*out) - sizeof(uint32_t)) == expected;
}

static bool write_metadata(ota_metadata_t *m) {
    m->crc32 = 0;
    m->crc32 = pico_ota_crc32(m, sizeof(*m) - sizeof(uint32_t));

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, m, sizeof(*m));

    if (!erase_range(OTA_METADATA_OFFSET, FLASH_SECTOR_SIZE)) return false;
    if (!program_page(OTA_METADATA_OFFSET, page))              return false;
    return true;
}

// Read the current metadata, adjust the caller-provided field(s), and
// write back. On error the record on flash is left in a defined state:
// either the previous version (if erase failed) or a fresh version (if
// erase succeeded and program failed — the sector reads as 0xFF, which
// the bootloader treats as "no valid metadata").
static bool update_metadata(void (*mutator)(ota_metadata_t *)) {
    ota_metadata_t m;
    if (!read_metadata(&m)) {
        // Fresh record — synthesise one that reflects who we are.
        memset(&m, 0, sizeof(m));
        m.magic       = OTA_META_MAGIC;
        m.version     = OTA_META_VERSION;
        m.active_slot = PICO_OTA_SLOT;
    }
    mutator(&m);
    return write_metadata(&m);
}

// ---- helpers ---------------------------------------------------------------

static uint32_t staging_slot_offset(void) {
    return g_status.staging_slot == 0 ? OTA_SLOT_A_OFFSET : OTA_SLOT_B_OFFSET;
}

// ---- lifecycle -------------------------------------------------------------

void ota_init(void) {
    memset(&g_status, 0, sizeof(g_status));

    ota_metadata_t m;
    bool have = read_metadata(&m);

    if (have) {
        if (m.active_slot != PICO_OTA_SLOT) {
            pico_ota_log_warn("ota: metadata active_slot=%u but built as slot %u",
                 m.active_slot, PICO_OTA_SLOT);
        }
        g_status.pending_verify = m.pending_verify;
        g_status.boot_attempts  = m.boot_attempts;
    } else {
        // No valid metadata — write one so the bootloader has ground
        // truth from here on.
        memset(&m, 0, sizeof(m));
        m.magic       = OTA_META_MAGIC;
        m.version     = OTA_META_VERSION;
        m.active_slot = PICO_OTA_SLOT;
        write_metadata(&m);
        pico_ota_log_info("ota: initial metadata written (slot %u active)", PICO_OTA_SLOT);
    }

    g_status.active_slot  = PICO_OTA_SLOT;
    g_status.staging_slot = PICO_OTA_SLOT ^ 1u;

    pico_ota_log_info("ota: active=%c staging=%c pending=%d attempts=%u",
         'A' + g_status.active_slot, 'A' + g_status.staging_slot,
         (int)g_status.pending_verify,
         (unsigned)g_status.boot_attempts);
}

ota_status_t ota_status(void) { return g_status; }

// ---- begin / write / finalize (streams into staging slot) ------------------

ota_result_t ota_begin(size_t image_size) {
    if (g_status.update_in_progress) return OTA_ERR_BUSY;
    if (image_size > OTA_SLOT_SIZE)  return OTA_ERR_SIZE;

    uint32_t base = staging_slot_offset();
    pico_ota_log_info("ota: begin size=%u — erasing %u KB at slot %c",
         (unsigned)image_size, (unsigned)(OTA_SLOT_SIZE / 1024),
         'A' + g_status.staging_slot);

    // Sector-by-sector so the CYW43 coordination window stays short.
    for (uint32_t off = 0; off < OTA_SLOT_SIZE; off += FLASH_SECTOR_SIZE) {
        if (!erase_range(base + off, FLASH_SECTOR_SIZE)) return OTA_ERR_IO;
    }

    g_status.update_in_progress = true;
    g_status.image_ready        = false;
    g_status.staging_size       = image_size;
    g_status.bytes_written      = 0;
    g_page_buf_len              = 0;
    g_next_page_off             = 0;
    memset(g_status.computed_sha256, 0, sizeof(g_status.computed_sha256));

    sha256_init(&g_sha);
    return OTA_OK;
}

ota_result_t ota_write(size_t offset, const uint8_t *data, size_t len) {
    if (!g_status.update_in_progress)         return OTA_ERR_NOT_STARTED;
    if (!data || len == 0)                    return OTA_ERR_RANGE;
    if (offset != g_status.bytes_written)     return OTA_ERR_RANGE;
    size_t limit = g_status.staging_size ? g_status.staging_size : OTA_SLOT_SIZE;
    if (offset + len > limit)                 return OTA_ERR_RANGE;

    sha256_update(&g_sha, data, len);
    g_status.bytes_written += len;

    uint32_t base = staging_slot_offset();
    while (len > 0) {
        size_t space = FLASH_PAGE_SIZE - g_page_buf_len;
        size_t take  = len < space ? len : space;
        memcpy(g_page_buf + g_page_buf_len, data, take);
        g_page_buf_len += take;
        data += take;
        len  -= take;

        if (g_page_buf_len == FLASH_PAGE_SIZE) {
            if (!program_page(base + g_next_page_off, g_page_buf)) {
                return OTA_ERR_IO;
            }
            g_next_page_off += FLASH_PAGE_SIZE;
            g_page_buf_len = 0;
        }
    }
    return OTA_OK;
}

ota_result_t ota_finalize(void) {
    if (!g_status.update_in_progress) return OTA_ERR_NOT_STARTED;

    if (g_page_buf_len > 0) {
        memset(g_page_buf + g_page_buf_len, 0xFF,
               FLASH_PAGE_SIZE - g_page_buf_len);
        uint32_t base = staging_slot_offset();
        if (!program_page(base + g_next_page_off, g_page_buf)) return OTA_ERR_IO;
        g_next_page_off += FLASH_PAGE_SIZE;
        g_page_buf_len = 0;
    }

    sha256_final(&g_sha, g_status.computed_sha256);
    g_status.update_in_progress = false;
    g_status.image_ready        = true;
    pico_ota_log_info("ota: finalize ok, %u bytes staged in slot %c",
         (unsigned)g_status.bytes_written, 'A' + g_status.staging_slot);
    return OTA_OK;
}

ota_result_t ota_abort(void) {
    if (!g_status.update_in_progress) return OTA_OK;
    g_status.update_in_progress = false;
    g_status.image_ready        = false;
    g_status.staging_size       = 0;
    g_status.bytes_written      = 0;
    g_page_buf_len              = 0;
    g_next_page_off             = 0;
    memset(g_status.computed_sha256, 0, sizeof(g_status.computed_sha256));
    pico_ota_log_info("ota: abort");
    return OTA_OK;
}

// ---- verify / apply / confirm ----------------------------------------------

ota_result_t ota_verify(const uint8_t expected[SHA256_DIGEST_LEN]) {
    if (!g_status.image_ready) return OTA_ERR_NOT_STARTED;
    if (!expected)             return OTA_ERR_RANGE;
    return sha256_equal(g_status.computed_sha256, expected)
        ? OTA_OK : OTA_ERR_VERIFY;
}

// Local closure state for the metadata mutator — flash_safe_execute
// callbacks receive only a void* so we can't cleanly pass structs.
// Since the caller owns single-threaded access, a module-scope temp
// is fine.
static ota_status_t g_apply_capture;

static void apply_mutator(ota_metadata_t *m) {
    uint8_t new_active = g_apply_capture.staging_slot;
    m->active_slot     = new_active;
    m->pending_verify  = 1;
    m->boot_attempts   = 0;
    m->image_size[new_active] = (uint32_t)g_apply_capture.bytes_written;
    memcpy(m->image_sha256[new_active],
           g_apply_capture.computed_sha256, SHA256_DIGEST_LEN);
    // Preserve the other slot's metadata intact.
}

ota_result_t ota_apply(void) {
    if (!g_status.image_ready) return OTA_ERR_NO_IMAGE;

    g_apply_capture = g_status;
    if (!update_metadata(apply_mutator)) return OTA_ERR_IO;

    pico_ota_log_info("ota: applying — reboot into slot %c (pending verify)",
         'A' + g_status.staging_slot);
    // Give BLE / USB a moment to flush the goodbye.
    sleep_ms(50);
    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();
}

static void confirm_mutator(ota_metadata_t *m) {
    m->pending_verify = 0;
    m->boot_attempts  = 0;
}

void ota_confirm(void) {
    if (!g_status.pending_verify && g_status.boot_attempts == 0) return;
    if (update_metadata(confirm_mutator)) {
        g_status.pending_verify = false;
        g_status.boot_attempts  = 0;
        pico_ota_log_info("ota: image confirmed");
    }
}
