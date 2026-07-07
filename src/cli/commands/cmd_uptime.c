#include "cli/cli.h"
#include "cli/command.h"
#include "system/uptime.h"

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argc; (void)argv;
    char buf[32];
    uptime_format(buf, sizeof(buf));
    cli_printf(ctx, "up %s\r\n", buf);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(uptime,
    "Time since boot",
    "uptime",
    handle);
