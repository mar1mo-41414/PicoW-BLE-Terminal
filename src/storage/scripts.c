// Flash-backed script storage. See scripts.h.
#include "storage/scripts.h"
#include "system/log.h"

#include "hardware/flash.h"
#include "pico/flash.h"

#include "pico_ota/crc32.h"

#include <string.h>

#define SCRIPT_MAGIC   0x53435250u  // 'SCRP'
#define SCRIPT_VERSION 1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    char     name[SCRIPT_NAME_MAX + 1];   // 32 B including NUL
    uint32_t body_len;
    uint32_t crc32;                        // over header+body with crc32=0
    uint8_t  pad[16];                      // fills header to exactly 64 B
} __attribute__((packed)) script_header_t;

_Static_assert(sizeof(script_header_t) == SCRIPT_HEADER_SIZE,
               "script_header_t must be exactly SCRIPT_HEADER_SIZE bytes");

// ---- flash_safe_execute glue -----------------------------------------------

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    size_t len;
} flash_op_t;

static void erase_cb(void *arg)   { flash_op_t *op = arg; flash_range_erase(op->offset, op->len); }
static void program_cb(void *arg) { flash_op_t *op = arg; flash_range_program(op->offset, op->data, op->len); }

static bool erase_slot(int slot) {
    flash_op_t op = {
        .offset = SCRIPT_STORAGE_OFFSET + (uint32_t)slot * SCRIPT_SLOT_SIZE,
        .len    = SCRIPT_SLOT_SIZE,
    };
    if (flash_safe_execute(erase_cb, &op, 500) != PICO_OK) {
        LOGE("scripts: erase failed slot=%d", slot);
        return false;
    }
    return true;
}

static bool program_slot(int slot, const uint8_t *buf) {
    flash_op_t op = {
        .offset = SCRIPT_STORAGE_OFFSET + (uint32_t)slot * SCRIPT_SLOT_SIZE,
        .data   = buf,
        .len    = SCRIPT_SLOT_SIZE,
    };
    if (flash_safe_execute(program_cb, &op, 1000) != PICO_OK) {
        LOGE("scripts: program failed slot=%d", slot);
        return false;
    }
    return true;
}

// ---- slot lookup -----------------------------------------------------------

static const script_header_t *slot_hdr(int slot) {
    uintptr_t addr = XIP_BASE + SCRIPT_STORAGE_OFFSET
                   + (uintptr_t)slot * SCRIPT_SLOT_SIZE;
    return (const script_header_t *)addr;
}

static const uint8_t *slot_bytes(int slot) {
    return (const uint8_t *)slot_hdr(slot);
}

static bool slot_valid(int slot) {
    const script_header_t *h = slot_hdr(slot);
    return h->magic == SCRIPT_MAGIC && h->version == SCRIPT_VERSION;
}

static int find_by_name(const char *name) {
    for (int i = 0; i < SCRIPT_SLOT_COUNT; i++) {
        if (!slot_valid(i)) continue;
        if (strncmp(slot_hdr(i)->name, name, SCRIPT_NAME_MAX) == 0) return i;
    }
    return -1;
}

static int find_free(void) {
    for (int i = 0; i < SCRIPT_SLOT_COUNT; i++) {
        if (!slot_valid(i)) return i;
    }
    return -1;
}

// ---- name validation -------------------------------------------------------

static bool valid_name(const char *name) {
    if (!name || !*name) return false;
    size_t n = 0;
    for (const char *p = name; *p; p++, n++) {
        char c = *p;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
               || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
        if (n >= SCRIPT_NAME_MAX) return false;
    }
    return true;
}

// ---- CRC over header + body (with header.crc32 zeroed) ---------------------

static uint32_t compute_crc(const uint8_t *slot_buf, size_t body_len) {
    // Caller has already ensured header.crc32 == 0 in slot_buf.
    return pico_ota_crc32(slot_buf, SCRIPT_HEADER_SIZE + body_len);
}

// ---- save / load / remove --------------------------------------------------

bool scripts_save(const char *name, const uint8_t *body, size_t body_len) {
    if (!valid_name(name))           return false;
    if (body_len > SCRIPT_BODY_MAX)  return false;
    if (body_len > 0 && !body)       return false;

    int slot = find_by_name(name);
    if (slot < 0) slot = find_free();
    if (slot < 0) {
        LOGW("scripts: no free slot (max %d)", SCRIPT_SLOT_COUNT);
        return false;
    }

    static uint8_t scratch[SCRIPT_SLOT_SIZE];
    memset(scratch, 0xFF, sizeof(scratch));
    script_header_t *hdr = (script_header_t *)scratch;
    hdr->magic    = SCRIPT_MAGIC;
    hdr->version  = SCRIPT_VERSION;
    hdr->reserved = 0;
    strncpy(hdr->name, name, SCRIPT_NAME_MAX);
    hdr->name[SCRIPT_NAME_MAX] = '\0';
    hdr->body_len = (uint32_t)body_len;
    hdr->crc32    = 0;
    if (body_len > 0) memcpy(&scratch[SCRIPT_HEADER_SIZE], body, body_len);
    hdr->crc32 = compute_crc(scratch, body_len);

    if (!erase_slot(slot))            return false;
    if (!program_slot(slot, scratch)) return false;
    return true;
}

bool scripts_load(const char *name, uint8_t *out, size_t out_max, size_t *out_len) {
    int slot = find_by_name(name);
    if (slot < 0) return false;

    const script_header_t *h = slot_hdr(slot);
    size_t body_len = h->body_len;
    if (body_len > SCRIPT_BODY_MAX) return false;

    // Reconstruct a fresh copy with crc32=0 and CRC-check.
    static uint8_t check[SCRIPT_SLOT_SIZE];
    memcpy(check, slot_bytes(slot), SCRIPT_HEADER_SIZE + body_len);
    script_header_t *ch = (script_header_t *)check;
    uint32_t expected = ch->crc32;
    ch->crc32 = 0;
    if (compute_crc(check, body_len) != expected) {
        LOGW("scripts: CRC mismatch on '%s'", name);
        return false;
    }

    if (out) {
        if (body_len > out_max) return false;
        memcpy(out, &check[SCRIPT_HEADER_SIZE], body_len);
    }
    if (out_len) *out_len = body_len;
    return true;
}

bool scripts_remove(const char *name) {
    int slot = find_by_name(name);
    if (slot < 0) return false;
    return erase_slot(slot);
}

// ---- open / close (direct XIP access with one-shot CRC check) --------------

static bool g_open = false;

bool scripts_open(const char *name, const uint8_t **body_out, size_t *body_len_out) {
    if (g_open) return false;                   // one at a time
    int slot = find_by_name(name);
    if (slot < 0) return false;

    const script_header_t *h = slot_hdr(slot);
    size_t body_len = h->body_len;
    if (body_len > SCRIPT_BODY_MAX) return false;

    // CRC-check by rebuilding a scratch copy with crc32 zeroed.
    static uint8_t check[SCRIPT_SLOT_SIZE];
    memcpy(check, slot_bytes(slot), SCRIPT_HEADER_SIZE + body_len);
    script_header_t *ch = (script_header_t *)check;
    uint32_t expected = ch->crc32;
    ch->crc32 = 0;
    if (compute_crc(check, body_len) != expected) {
        LOGW("scripts: CRC mismatch on '%s'", name);
        return false;
    }

    if (body_out)     *body_out     = slot_bytes(slot) + SCRIPT_HEADER_SIZE;
    if (body_len_out) *body_len_out = body_len;
    g_open = true;
    return true;
}

void scripts_close(void) { g_open = false; }

// ---- iteration -------------------------------------------------------------

void scripts_iter(scripts_iter_fn cb, void *user) {
    if (!cb) return;
    for (int i = 0; i < SCRIPT_SLOT_COUNT; i++) {
        if (!slot_valid(i)) continue;
        const script_header_t *h = slot_hdr(i);
        cb(i, h->name, h->body_len, user);
    }
}

int scripts_count(void) {
    int n = 0;
    for (int i = 0; i < SCRIPT_SLOT_COUNT; i++) if (slot_valid(i)) n++;
    return n;
}
