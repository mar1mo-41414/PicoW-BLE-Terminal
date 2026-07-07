// Bundle of runtime facts about the board — the "system info" command
// pulls its data through here so we can add fields in one place.
#ifndef PICOBLE_SYSTEM_SYSINFO_H
#define PICOBLE_SYSTEM_SYSINFO_H

#include "cli/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

// Print every known fact to `ctx`. Fields we cannot resolve on this
// board are printed as "unimplemented" rather than skipped, so the
// output shape is stable across variants.
void sysinfo_print(cli_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_SYSTEM_SYSINFO_H
