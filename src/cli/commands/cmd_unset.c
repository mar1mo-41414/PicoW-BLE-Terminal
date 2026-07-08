#include "cli/cli.h"
#include "cli/command.h"
#include "system/vars.h"

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;
    int deleted = 0;
    for (int i = 1; i < argc; i++) {
        if (vars_unset(argv[i])) deleted++;
    }
    if (deleted == 0) cli_write(ctx, "unset: no matching variable\r\n");
    return CLI_OK;
}

CLI_COMMAND_REGISTER(unset,
    "Remove one or more shell variables",
    "unset <name> [name ...]",
    handle);
