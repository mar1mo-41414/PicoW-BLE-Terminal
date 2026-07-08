#include "cli/cli.h"
#include "cli/command.h"
#include "system/version.h"

#include "pico/version.h"

#ifndef PICO_SDK_VERSION_STRING
#define PICO_SDK_VERSION_STRING "unknown"
#endif

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argc; (void)argv;
    // The slot letter is baked in per-slot build via -DPICO_OTA_SLOT;
    // seeing it here (and in the greeting) confirms which slot booted.
    cli_printf(ctx, "PicoBLE Terminal %s  [slot %c]\r\n",
               PICOBLE_FW_VERSION, 'A' + (PICO_OTA_SLOT & 1));
    cli_printf(ctx, "Pico SDK %s\r\n", PICO_SDK_VERSION_STRING);
    cli_printf(ctx, "Built %s\r\n", PICOBLE_BUILD_DATE);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(version,
    "Show firmware and SDK versions",
    "version",
    handle);
