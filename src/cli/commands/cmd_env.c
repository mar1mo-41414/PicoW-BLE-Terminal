#include "cli/cli.h"
#include "cli/command.h"
#include "system/vars.h"

static void list_one(const char *name, const char *value, void *user) {
    cli_ctx_t *ctx = user;
    cli_printf(ctx, "  %s=%s\r\n", name, value);
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argc; (void)argv;
    if (vars_count() == 0) {
        cli_write(ctx, "(no variables set)\r\n");
        return CLI_OK;
    }
    vars_iter(list_one, ctx);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(env,
    "List all shell variables",
    "env",
    handle);
