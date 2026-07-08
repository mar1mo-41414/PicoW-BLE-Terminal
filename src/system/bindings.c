#include "system/bindings.h"
#include "system/log.h"
#include "system/uptime.h"
#include "pico_ota/crc32.h"

#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/time.h"

#include <stdio.h>
#include <string.h>

// ---- storage location ------------------------------------------------------
// Inside the "reserved" region documented in pico_ota/metadata.h.
// One sector, distinct from scripts (which lives earlier) and Wi-Fi
// creds (last sector).
#define BIND_STORAGE_OFFSET  0x001FA000u
#define BIND_MAGIC           0x424E4431u   // 'BND1'
#define BIND_VERSION         1u

// ---- runtime state ---------------------------------------------------------

typedef struct {
    bool          used;
    uint8_t       pin;
    uint8_t       edge;             // bind_edge_t
    uint32_t      debounce_ms;
    char          target[BIND_TARGET_MAX + 1];
    volatile uint32_t last_fired_ms;
    volatile bool pending;
} slot_t;

static slot_t     g_slots[BIND_SLOT_COUNT];
static cli_ctx_t *g_bg_ctx = NULL;
static bool       g_irq_wired = false;

// ---- flash record ----------------------------------------------------------

typedef struct {
    uint8_t  used;
    uint8_t  pin;
    uint8_t  edge;
    uint8_t  reserved;
    uint32_t debounce_ms;
    char     target[BIND_TARGET_MAX + 1];  // 97 bytes
} __attribute__((packed)) disk_bind_t;

typedef struct {
    uint32_t   magic;
    uint16_t   version;
    uint16_t   count;
    disk_bind_t entries[BIND_SLOT_COUNT];
    uint32_t   crc32;
} __attribute__((packed)) disk_record_t;

_Static_assert(sizeof(disk_record_t) <= FLASH_SECTOR_SIZE,
               "bindings disk record must fit in one flash sector");

// ---- utilities -------------------------------------------------------------

const char *bindings_edge_str(bind_edge_t e) {
    switch (e) {
        case BIND_EDGE_HIGH:   return "high";
        case BIND_EDGE_LOW:    return "low";
        case BIND_EDGE_CHANGE: return "change";
    }
    return "?";
}

bool bindings_parse_edge(const char *s, bind_edge_t *out) {
    if (!s || !out) return false;
    if (!strcmp(s, "high") || !strcmp(s, "rise") || !strcmp(s, "rising"))  { *out = BIND_EDGE_HIGH;   return true; }
    if (!strcmp(s, "low")  || !strcmp(s, "fall") || !strcmp(s, "falling")) { *out = BIND_EDGE_LOW;    return true; }
    if (!strcmp(s, "change") || !strcmp(s, "both"))                        { *out = BIND_EDGE_CHANGE; return true; }
    return false;
}

// ---- IRQ handling ----------------------------------------------------------
//
// Single callback for every GPIO IRQ (pico-sdk restricts to one per
// core). Debounce comparison is done here so bursts don't queue up.

static void gpio_irq_cb(uint gpio, uint32_t events) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < BIND_SLOT_COUNT; i++) {
        slot_t *s = &g_slots[i];
        if (!s->used || s->pin != gpio) continue;

        bool match = false;
        if (events & GPIO_IRQ_EDGE_RISE) match = (s->edge == BIND_EDGE_HIGH   || s->edge == BIND_EDGE_CHANGE);
        if (events & GPIO_IRQ_EDGE_FALL) match = match || (s->edge == BIND_EDGE_LOW || s->edge == BIND_EDGE_CHANGE);
        if (!match) continue;

        if (s->debounce_ms > 0 && (now - s->last_fired_ms) < s->debounce_ms) continue;
        s->last_fired_ms = now;
        s->pending = true;
    }
}

static uint32_t edge_mask(bind_edge_t e) {
    switch (e) {
        case BIND_EDGE_HIGH:   return GPIO_IRQ_EDGE_RISE;
        case BIND_EDGE_LOW:    return GPIO_IRQ_EDGE_FALL;
        case BIND_EDGE_CHANGE: return GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;
    }
    return 0;
}

// Recompute IRQ setup for a pin: OR together the masks of every used
// binding on that pin, and set the IRQ mask exactly to that.
static void refresh_pin_irq(uint pin) {
    uint32_t mask = 0;
    for (int i = 0; i < BIND_SLOT_COUNT; i++) {
        if (g_slots[i].used && g_slots[i].pin == pin) {
            mask |= edge_mask(g_slots[i].edge);
        }
    }
    if (mask) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);   // sane default — user can wire externally
        if (!g_irq_wired) {
            gpio_set_irq_enabled_with_callback(pin, mask, true, gpio_irq_cb);
            g_irq_wired = true;
        } else {
            gpio_set_irq_enabled(pin, mask, true);
        }
    } else {
        gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    }
}

// ---- add / remove / list ---------------------------------------------------

static int find_slot(uint pin) {
    for (int i = 0; i < BIND_SLOT_COUNT; i++) {
        if (g_slots[i].used && g_slots[i].pin == pin) return i;
    }
    return -1;
}

static int find_free(void) {
    for (int i = 0; i < BIND_SLOT_COUNT; i++) if (!g_slots[i].used) return i;
    return -1;
}

bool bindings_add(uint pin, bind_edge_t edge, const char *target, uint32_t debounce_ms) {
    if (pin > 29 || !target || strlen(target) > BIND_TARGET_MAX) return false;

    int slot = find_slot(pin);
    if (slot < 0) slot = find_free();
    if (slot < 0) return false;

    slot_t *s = &g_slots[slot];
    s->used         = true;
    s->pin          = (uint8_t)pin;
    s->edge         = (uint8_t)edge;
    s->debounce_ms  = debounce_ms;
    strncpy(s->target, target, BIND_TARGET_MAX);
    s->target[BIND_TARGET_MAX] = '\0';
    s->last_fired_ms = 0;
    s->pending = false;

    refresh_pin_irq(pin);
    return true;
}

bool bindings_remove(uint pin) {
    int slot = find_slot(pin);
    if (slot < 0) return false;
    g_slots[slot].used = false;
    refresh_pin_irq(pin);
    return true;
}

void bindings_clear_all(void) {
    for (int i = 0; i < BIND_SLOT_COUNT; i++) {
        if (g_slots[i].used) {
            uint pin = g_slots[i].pin;
            g_slots[i].used = false;
            refresh_pin_irq(pin);
        }
    }
}

size_t bindings_count(void) {
    size_t n = 0;
    for (int i = 0; i < BIND_SLOT_COUNT; i++) if (g_slots[i].used) n++;
    return n;
}

void bindings_iter(bindings_iter_fn cb, void *user) {
    if (!cb) return;
    for (int i = 0; i < BIND_SLOT_COUNT; i++) {
        if (!g_slots[i].used) continue;
        bind_snapshot_t snap = {
            .used         = true,
            .pin          = g_slots[i].pin,
            .edge         = (bind_edge_t)g_slots[i].edge,
            .debounce_ms  = g_slots[i].debounce_ms,
            .last_fired_ms = g_slots[i].last_fired_ms,
        };
        strncpy(snap.target, g_slots[i].target, BIND_TARGET_MAX);
        snap.target[BIND_TARGET_MAX] = '\0';
        cb(i, &snap, user);
    }
}

// ---- dispatch --------------------------------------------------------------

void bindings_dispatch_pending(void) {
    if (!g_bg_ctx) return;
    for (int i = 0; i < BIND_SLOT_COUNT; i++) {
        slot_t *s = &g_slots[i];
        if (!s->used || !s->pending) continue;
        s->pending = false;

        // The target is a full command line; hand it to cli_dispatch_line
        // via a mutable copy so it can be tokenized in-place.
        char line[BIND_TARGET_MAX + 1];
        strncpy(line, s->target, BIND_TARGET_MAX);
        line[BIND_TARGET_MAX] = '\0';
        cli_dispatch_line(g_bg_ctx, line);
    }
}

// ---- persistence -----------------------------------------------------------

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    size_t len;
} flash_op_t;

static void erase_cb  (void *arg) { flash_op_t *op = arg; flash_range_erase  (op->offset, op->len); }
static void program_cb(void *arg) { flash_op_t *op = arg; flash_range_program(op->offset, op->data, op->len); }

bool bindings_save(void) {
    static disk_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = BIND_MAGIC;
    rec.version = BIND_VERSION;
    uint16_t count = 0;
    for (int i = 0; i < BIND_SLOT_COUNT; i++) {
        if (!g_slots[i].used) continue;
        disk_bind_t *d = &rec.entries[count++];
        d->used = 1;
        d->pin  = g_slots[i].pin;
        d->edge = g_slots[i].edge;
        d->debounce_ms = g_slots[i].debounce_ms;
        strncpy(d->target, g_slots[i].target, BIND_TARGET_MAX);
        d->target[BIND_TARGET_MAX] = '\0';
    }
    rec.count = count;
    rec.crc32 = 0;
    rec.crc32 = pico_ota_crc32(&rec, sizeof(rec) - sizeof(uint32_t));

    // Program in one full-sector write. flash_range_program requires a
    // page-multiple size; pad with 0xFF up to FLASH_SECTOR_SIZE.
    static uint8_t sector[FLASH_SECTOR_SIZE];
    memset(sector, 0xFF, sizeof(sector));
    memcpy(sector, &rec, sizeof(rec));

    flash_op_t e = { .offset = BIND_STORAGE_OFFSET, .len = FLASH_SECTOR_SIZE };
    if (flash_safe_execute(erase_cb, &e, 500) != PICO_OK) { LOGE("bindings: erase failed"); return false; }
    flash_op_t p = { .offset = BIND_STORAGE_OFFSET, .data = sector, .len = FLASH_SECTOR_SIZE };
    if (flash_safe_execute(program_cb, &p, 1000) != PICO_OK) { LOGE("bindings: program failed"); return false; }
    LOGI("bindings: saved %u bindings", (unsigned)count);
    return true;
}

bool bindings_load(void) {
    const disk_record_t *xip = (const disk_record_t *)(XIP_BASE + BIND_STORAGE_OFFSET);
    disk_record_t rec;
    memcpy(&rec, xip, sizeof(rec));
    if (rec.magic != BIND_MAGIC || rec.version != BIND_VERSION) return false;
    uint32_t expected = rec.crc32;
    rec.crc32 = 0;
    if (pico_ota_crc32(&rec, sizeof(rec) - sizeof(uint32_t)) != expected) {
        LOGW("bindings: CRC mismatch — ignoring stored bindings");
        return false;
    }
    if (rec.count > BIND_SLOT_COUNT) return false;

    // Wipe live table, then install each stored record.
    for (int i = 0; i < BIND_SLOT_COUNT; i++) g_slots[i].used = false;
    for (uint16_t i = 0; i < rec.count; i++) {
        const disk_bind_t *d = &rec.entries[i];
        if (!d->used) continue;
        bindings_add(d->pin, (bind_edge_t)d->edge, d->target, d->debounce_ms);
    }
    LOGI("bindings: loaded %u from flash", (unsigned)rec.count);
    return true;
}

// ---- init ------------------------------------------------------------------

void bindings_init(cli_ctx_t *bg_ctx) {
    g_bg_ctx = bg_ctx;
    memset(g_slots, 0, sizeof(g_slots));
    bindings_load();
}
