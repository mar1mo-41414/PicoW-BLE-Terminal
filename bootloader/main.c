// PicoBLE-Terminal bootloader.
//
// Executes at 0x10000000 with its own boot2. Decides which application
// slot to run based on the metadata block written by the application's
// OTA layer, and handles rollback when a freshly-staged image fails
// to confirm itself.
//
// Design constraints:
//   - No stdio, no cyw43, no BTStack. Small binary that fits under
//     16 KB reserved at the start of flash.
//   - Flash writes (for rollback / metadata init) happen with
//     interrupts disabled and NO cyw43-driver coordination — the
//     chip has never been powered up at this point.
//   - We never look at the CYW43 pins, so any GPIO/PIO state left
//     behind by the app is fine to leave alone.
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "ota_metadata.h"

#include <string.h>

// XIP view of the metadata block.
static const volatile ota_metadata_t *xip_meta =
    (const volatile ota_metadata_t *)(XIP_BASE + OTA_METADATA_OFFSET);

#define VTOR_ADDR   0xE000ED08UL

// ---- CRC-32/IEEE — same polynomial the app uses in storage/config.c and ota.c
// so both sides agree bit-for-bit on record validity.

static uint32_t crc32_ieee(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

// ---- metadata --------------------------------------------------------------

static bool read_metadata(ota_metadata_t *out) {
    // Copy out of XIP so we work on a stable snapshot.
    memcpy(out, (const void *)xip_meta, sizeof(*out));
    if (out->magic   != OTA_META_MAGIC)   return false;
    if (out->version != OTA_META_VERSION) return false;
    uint32_t expected = out->crc32;
    out->crc32 = 0;
    return crc32_ieee(out, sizeof(*out) - sizeof(uint32_t)) == expected;
}

static void write_metadata(ota_metadata_t *m) {
    m->crc32 = 0;
    m->crc32 = crc32_ieee(m, sizeof(*m) - sizeof(uint32_t));

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, m, sizeof(*m));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(OTA_METADATA_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(OTA_METADATA_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

// ---- slot jump -------------------------------------------------------------

static __attribute__((noreturn)) void enter_bootsel(void) {
    reset_usb_boot(0, 0);
    while (1) tight_loop_contents();
}

// The app image starts with its own boot2 at slot_base and its vector
// table at slot_base + 0x100 (256 B). Boot2 has already been executed
// by our own bootrom sequence, so we skip past it and dive straight
// into the vectors.
static __attribute__((noreturn)) void jump_to_slot(uint32_t slot_offset) {
    uint32_t vt_addr = XIP_BASE + slot_offset + 256u;
    uint32_t sp    = *(volatile uint32_t *)(vt_addr);
    uint32_t entry = *(volatile uint32_t *)(vt_addr + 4);

    // Sanity — an erased slot reads 0xFFFFFFFF everywhere; a partially
    // written slot might have plausible-looking SP but bogus entry.
    if (sp    <  0x20000000u || sp    > 0x20042000u) enter_bootsel();
    if ((entry & 1u) == 0)                            enter_bootsel();
    if (entry <  0x10000000u || entry > 0x10200000u) enter_bootsel();

    // Point Cortex-M0+ VTOR at the app's vectors before entering.
    *(volatile uint32_t *)VTOR_ADDR = vt_addr;

    __asm volatile (
        "msr msp, %0\n"
        "bx  %1\n"
        : : "r" (sp), "r" (entry)
    );
    __builtin_unreachable();
}

// ---- boot logic ------------------------------------------------------------

static bool slot_looks_valid(uint32_t slot_offset) {
    uint32_t vt = XIP_BASE + slot_offset + 256u;
    uint32_t sp    = *(volatile uint32_t *)(vt);
    uint32_t entry = *(volatile uint32_t *)(vt + 4);
    return (sp    >= 0x20000000u) && (sp    <= 0x20042000u)
        && ((entry & 1u) != 0)
        && (entry >= 0x10000000u) && (entry <= 0x10200000u);
}

int main(void) {
    ota_metadata_t meta;
    bool valid = read_metadata(&meta);

    if (!valid) {
        // First boot after a bare-metal install of the bootloader:
        // the metadata sector is still all 0xFF. If slot A already
        // holds a plausible image (someone flashed slot A UF2 after
        // the bootloader UF2), commit that as the active slot and
        // boot it; otherwise fall into USB BOOTSEL so the user can
        // drag in the app UF2.
        if (slot_looks_valid(OTA_SLOT_A_OFFSET)) {
            memset(&meta, 0, sizeof(meta));
            meta.magic       = OTA_META_MAGIC;
            meta.version     = OTA_META_VERSION;
            meta.active_slot = 0;
            write_metadata(&meta);
            jump_to_slot(OTA_SLOT_A_OFFSET);
        }
        enter_bootsel();
    }

    // Rollback: if a probationary boot never got confirmed within the
    // allowed number of attempts, flip active_slot and reboot into the
    // previous known-good image.
    if (meta.pending_verify && meta.boot_attempts >= OTA_MAX_BOOT_ATTEMPTS) {
        meta.active_slot     ^= 1u;
        meta.pending_verify   = 0;
        meta.boot_attempts    = 0;
        write_metadata(&meta);
        watchdog_reboot(0, 0, 0);
        while (1) tight_loop_contents();
    }

    if (meta.pending_verify) {
        meta.boot_attempts++;
        write_metadata(&meta);
    }

    uint32_t slot_offset = (meta.active_slot == 0)
        ? OTA_SLOT_A_OFFSET
        : OTA_SLOT_B_OFFSET;

    if (!slot_looks_valid(slot_offset)) {
        // Active slot has been corrupted somehow (unfinished OTA write,
        // erased by external tool, etc). Try the other slot; if that's
        // also gone, enter BOOTSEL as a last resort.
        uint32_t other = (slot_offset == OTA_SLOT_A_OFFSET)
            ? OTA_SLOT_B_OFFSET
            : OTA_SLOT_A_OFFSET;
        if (slot_looks_valid(other)) {
            meta.active_slot   ^= 1u;
            meta.pending_verify = 0;
            meta.boot_attempts  = 0;
            write_metadata(&meta);
            jump_to_slot(other);
        }
        enter_bootsel();
    }

    jump_to_slot(slot_offset);
    return 0;  // unreachable
}
