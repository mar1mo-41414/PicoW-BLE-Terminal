#include "cli/cli.h"
#include "cli/command.h"

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    for (int i = 1; i < argc; i++) {
        cli_write(ctx, argv[i]);
        if (i + 1 < argc) cli_write(ctx, " ");
    }
    cli_write(ctx, "\r\n");
    return CLI_OK;
}

CLI_COMMAND_REGISTER(echo,
    "Print arguments back to the terminal",
    "echo [arg ...]",
    handle);
