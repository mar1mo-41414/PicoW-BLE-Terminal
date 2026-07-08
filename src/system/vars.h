// Named shell variables.
//
// Simple fixed-slot store (no allocations). Values are ASCII strings
// and expanded into command tokens by cli.c before dispatch — see
// `$NAME` / `${NAME}` handling in cli.c's dispatch path.
//
// Not thread-safe (nothing else in this firmware is either). Same
// storage backs BLE and USB CDC shells — variables are process-wide,
// not per-connection.
#ifndef PICOBLE_SYSTEM_VARS_H
#define PICOBLE_SYSTEM_VARS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VAR_MAX_COUNT   32
#define VAR_NAME_MAX    31
#define VAR_VALUE_MAX   95

// Assign name → value. If name exists it is replaced; otherwise a new
// slot is allocated. Returns false only when we're out of slots OR
// name/value is out of range.
bool vars_set(const char *name, const char *value);

// Delete name. Returns true iff it was present.
bool vars_unset(const char *name);

// Look up name → value. Returns NULL if unset. The returned pointer is
// valid until the next vars_set / vars_unset call.
const char *vars_get(const char *name);

// Number of currently defined variables.
size_t vars_count(void);

// Iterate all pairs in registration order.
typedef void (*vars_iter_fn)(const char *name, const char *value, void *user);
void vars_iter(vars_iter_fn cb, void *user);

// Name validity — used by the parser too, to know how far to scan
// after '$'. Allows [A-Za-z_][A-Za-z0-9_]*.
bool vars_name_char_first(char c);
bool vars_name_char_rest (char c);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_SYSTEM_VARS_H
