#include "cli/cli.h"
#include "cli/command.h"
#include "system/version.h"

#include "pico/version.h"

#ifndef PICO_SDK_VERSION_STRING
#define PICO_SDK_VERSION_STRING "unknown"
#endif

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argc; (void)argv;
    cli_printf(ctx, "PicoBLE Terminal %s\r\n", PICOBLE_FW_VERSION);
    cli_printf(ctx, "Pico SDK %s\r\n", PICO_SDK_VERSION_STRING);
    cli_printf(ctx, "Built %s\r\n", PICOBLE_BUILD_DATE);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(version,
    "Show firmware and SDK versions",
    "version",
    handle);
