// `system info` — the only subcommand for now. The dispatcher-in-a-
// dispatcher pattern keeps room for `system reset`, `system stats`, etc.
#include "cli/cli.h"
#include "cli/command.h"
#include "system/sysinfo.h"

#include <string.h>

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;

    if (strcmp(argv[1], "info") == 0) {
        sysinfo_print(ctx);
        return CLI_OK;
    }

    cli_printf(ctx, "system: unknown subcommand: %s\r\n", argv[1]);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(system,
    "System information and controls",
    "system info",
    handle);
