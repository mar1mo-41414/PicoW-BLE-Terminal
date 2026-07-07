// OTA metadata shared between the bootloader and the application.
//
// The bootloader reads this block from flash to decide which slot to
// hand control to and, if a probation window is exhausted, to roll
// back to the previous slot. The application writes this block to
// stage a new image (ota_apply) and to confirm a probationary boot
// succeeded (ota_confirm).
//
// Layout is *identical* on both sides; this header is the single
// source of truth. Do not add fields in the middle — only append,
// bump OTA_META_VERSION, and be prepared to migrate older records.
#ifndef PICOBLE_OTA_METADATA_H
#define PICOBLE_OTA_METADATA_H

#include <stdint.h>

// ---- flash layout (offsets from XIP_BASE) ----------------------------------

#define OTA_BOOTLOADER_OFFSET   0x00000000u
#define OTA_BOOTLOADER_SIZE     (16u  * 1024u)      // 16 KB, 4 sectors

#define OTA_SLOT_A_OFFSET       (OTA_BOOTLOADER_OFFSET + OTA_BOOTLOADER_SIZE)
#define OTA_SLOT_SIZE           (768u * 1024u)      // 768 KB per slot, 192 sectors
#define OTA_SLOT_B_OFFSET       (OTA_SLOT_A_OFFSET + OTA_SLOT_SIZE)

#define OTA_METADATA_OFFSET     (OTA_SLOT_B_OFFSET + OTA_SLOT_SIZE)
#define OTA_METADATA_SIZE       (4u   * 1024u)      // 4 KB — one sector

// ---- record contents --------------------------------------------------------

#define OTA_META_MAGIC          0x4F544131u   // 'OTA1'
#define OTA_META_VERSION        1u

// Number of consecutive boots into a pending slot allowed before the
// bootloader rolls back to the previous slot. 3 means: the freshly
// staged image gets three tries to call ota_confirm(); on the fourth
// boot without a confirm we assume it is broken.
#define OTA_MAX_BOOT_ATTEMPTS   3u

typedef struct {
    uint32_t magic;                     // OTA_META_MAGIC
    uint16_t version;                   // OTA_META_VERSION
    uint8_t  active_slot;               // 0 = slot A, 1 = slot B
    uint8_t  pending_verify;            // 1 while the active slot is on probation
    uint8_t  boot_attempts;             // bumped by the bootloader each boot
    uint8_t  reserved[3];               // future flags — zero on write
    uint32_t image_size[2];             // 0 = slot has no valid image
    uint8_t  image_sha256[2][32];       // integrity hash per slot
    uint32_t crc32;                     // CRC-32/IEEE over the above
} __attribute__((packed)) ota_metadata_t;

_Static_assert(sizeof(ota_metadata_t) <= 256,
               "OTA metadata must fit in a single 256-byte flash page");

#endif  // PICOBLE_OTA_METADATA_H
