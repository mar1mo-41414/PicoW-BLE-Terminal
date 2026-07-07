// `help` — list every registered command with a one-line summary.
#include "cli/cli.h"
#include "cli/command.h"

#include <string.h>

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argv;

    if (argc == 1) {
        // First pass: work out the column width so summaries line up.
        size_t widest = 0;
        for (const cli_command_t *c = cli_command_iter(NULL); c;
             c = cli_command_iter(c)) {
            size_t n = strlen(c->name);
            if (n > widest) widest = n;
        }

        cli_write(ctx, "Available commands:\r\n");
        for (const cli_command_t *c = cli_command_iter(NULL); c;
             c = cli_command_iter(c)) {
            cli_printf(ctx, "  %-*s  %s\r\n",
                       (int)widest, c->name,
                       c->summary ? c->summary : "");
        }
        return CLI_OK;
    }

    // `help <cmd>` — show that command's usage.
    const cli_command_t *cmd = cli_command_find(argv[1]);
    if (!cmd) {
        cli_printf(ctx, "help: no such command: %s\r\n", argv[1]);
        return CLI_ERR_ARG;
    }
    if (cmd->summary) cli_printf(ctx, "%s\r\n", cmd->summary);
    cli_usage(ctx, cmd);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(help,
    "List commands, or `help <cmd>` for details",
    "help [command]",
    handle);
