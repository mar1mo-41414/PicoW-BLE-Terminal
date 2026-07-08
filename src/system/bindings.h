// GPIO event bindings — declare "when pin X does Y, run Z".
//
// GPIO IRQs mark a slot pending and return immediately; the actual
// command dispatch happens on the main loop from a BTStack timer that
// calls bindings_dispatch_pending(). Debouncing filters bursts inside
// the IRQ, before pending is set.
//
// Persistence lives in its own 4 KB flash sector — nothing here shares
// state with storage/config.c or storage/scripts.c.
#ifndef PICOBLE_SYSTEM_BINDINGS_H
#define PICOBLE_SYSTEM_BINDINGS_H

#include "cli/cli.h"
#include "pico.h"                       // for uint typedef

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIND_SLOT_COUNT   16
#define BIND_TARGET_MAX   96

typedef enum {
    BIND_EDGE_HIGH   = 0,   // rising edge
    BIND_EDGE_LOW    = 1,   // falling edge
    BIND_EDGE_CHANGE = 2,   // either edge
} bind_edge_t;

typedef struct {
    bool         used;
    uint8_t      pin;
    bind_edge_t  edge;
    uint32_t     debounce_ms;
    char         target[BIND_TARGET_MAX + 1];
    uint32_t     last_fired_ms;
} bind_snapshot_t;

// Initialize the module. `bg_ctx` is a caller-owned CLI context whose
// output sink is where triggered commands write to — usually a null-
// sink ctx so bindings execute quietly without smearing an active
// shell. Also loads any persisted bindings.
void bindings_init(cli_ctx_t *bg_ctx);

// Add / replace / remove.
bool bindings_add   (uint pin, bind_edge_t edge, const char *target, uint32_t debounce_ms);
bool bindings_remove(uint pin);
void bindings_clear_all(void);

size_t bindings_count(void);
typedef void (*bindings_iter_fn)(int slot, const bind_snapshot_t *b, void *user);
void bindings_iter(bindings_iter_fn cb, void *user);

// Called from the main-loop timer (see main.c). Executes any bindings
// whose IRQ set the pending flag since the last dispatch.
void bindings_dispatch_pending(void);

// Persistence. save() writes the current binding table to flash;
// load() reads and re-installs (called automatically by bindings_init).
bool bindings_save(void);
bool bindings_load(void);

// Human-readable edge string.
const char *bindings_edge_str(bind_edge_t e);
bool        bindings_parse_edge(const char *s, bind_edge_t *out);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_SYSTEM_BINDINGS_H
