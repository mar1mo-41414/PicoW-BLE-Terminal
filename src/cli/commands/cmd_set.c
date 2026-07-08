// `set NAME=VALUE` — assign a shell variable
// `set NAME VALUE` — alternate form (whitespace separator)
// `set`            — list current variables (same as `env`)
//
// Variables can then be referenced anywhere on the command line as
// $NAME or ${NAME}, and are expanded before dispatch (see cli.c).
#include "cli/cli.h"
#include "cli/command.h"
#include "system/vars.h"

#include <stdio.h>
#include <string.h>

static void list_one(const char *name, const char *value, void *user) {
    cli_ctx_t *ctx = user;
    cli_printf(ctx, "  %s=%s\r\n", name, value);
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc == 1) {
        if (vars_count() == 0) cli_write(ctx, "(no variables set)\r\n");
        else                    vars_iter(list_one, ctx);
        return CLI_OK;
    }

    // `set NAME=VALUE` — split at first '='
    if (argc == 2) {
        char *eq = strchr(argv[1], '=');
        if (!eq) return CLI_ERR_USAGE;
        *eq = '\0';
        const char *name  = argv[1];
        const char *value = eq + 1;
        if (!vars_set(name, value)) {
            cli_printf(ctx, "set: rejected (bad name or over-length value)\r\n");
            return CLI_ERR_ARG;
        }
        return CLI_OK;
    }

    // `set NAME VALUE`
    if (argc >= 3) {
        if (!vars_set(argv[1], argv[2])) {
            cli_printf(ctx, "set: rejected (bad name or over-length value)\r\n");
            return CLI_ERR_ARG;
        }
        return CLI_OK;
    }
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(set,
    "Assign a shell variable, or list all when called with no arguments",
    "set [NAME=VALUE | NAME VALUE]",
    handle);
