// Flash-backed script storage.
//
// 16 fixed slots of 4 KB each, starting at SCRIPT_STORAGE_OFFSET.
// Each slot holds an optional named script: a small header (name,
// length, CRC) plus the raw text. Empty slots read as 0xFF (magic
// mismatch → treated as absent).
//
// Layout was chosen so it sits inside the "reserved" region of the
// pico-ota flash map (0x185000..0x1FEFFF), well after the OTA slots
// and before the Wi-Fi credential sector. If you change either the
// count or the size, update docs/SPEC.md so the map stays in sync.
#ifndef PICOBLE_STORAGE_SCRIPTS_H
#define PICOBLE_STORAGE_SCRIPTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCRIPT_STORAGE_OFFSET   0x00185000u   // XIP-relative
#define SCRIPT_SLOT_SIZE        (4u * 1024u)  // one flash sector
#define SCRIPT_SLOT_COUNT       16
#define SCRIPT_NAME_MAX         31
#define SCRIPT_HEADER_SIZE      64            // first bytes of slot
#define SCRIPT_BODY_MAX         (SCRIPT_SLOT_SIZE - SCRIPT_HEADER_SIZE)

// Save (or overwrite) a script. body may contain any bytes; it is not
// interpreted here — the caller's line splitter does that in run.
// Returns false on: name invalid, body too big, no free slot (for a
// new name), or flash write failure.
bool scripts_save(const char *name, const uint8_t *body, size_t body_len);

// Load body of a named script into `out`. Returns false if not found
// or on CRC failure. `*out_len` receives the actual size.
bool scripts_load(const char *name, uint8_t *out, size_t out_max, size_t *out_len);

// Delete a named script (erase the sector). Returns false if not found.
bool scripts_remove(const char *name);

// Verify CRC then return a *direct XIP pointer* to the body and its
// length. Only one script may be "open" at a time — the underlying
// CRC scratch buffer is shared. Callers use this instead of scripts_load
// when they want to iterate the body without copying it (e.g. `run`).
bool scripts_open(const char *name, const uint8_t **body_out, size_t *body_len_out);
void scripts_close(void);

// Iterate every stored script's metadata in slot order.
typedef void (*scripts_iter_fn)(int slot, const char *name, size_t body_len, void *user);
void scripts_iter(scripts_iter_fn cb, void *user);

// Count of scripts currently stored.
int  scripts_count(void);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_STORAGE_SCRIPTS_H
