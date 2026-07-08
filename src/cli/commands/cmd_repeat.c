// `repeat <count> <cmd> [args ...]`
//
// Re-dispatches the sub-command N times. Bails early if a sub-command
// returns CLI_ERR_HARDWARE — the assumption is that hardware faults
// won't fix themselves on retry so continuing wastes cycles and floods
// the terminal.
#include "cli/cli.h"
#include "cli/command.h"

#include <stdlib.h>

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 3) return CLI_ERR_USAGE;

    char *end;
    long n = strtol(argv[1], &end, 10);
    if (*end != '\0' || n <= 0 || n > 100000) {
        cli_printf(ctx, "repeat: bad count: %s (1..100000)\r\n", argv[1]);
        return CLI_ERR_ARG;
    }

    int sub_argc = argc - 2;
    char **sub_argv = &argv[2];
    for (long i = 0; i < n; i++) {
        int rc = cli_dispatch_argv(ctx, sub_argc, sub_argv);
        if (rc == CLI_ERR_HARDWARE) {
            cli_printf(ctx, "repeat: aborted after %ld iteration(s) (hardware error)\r\n", i);
            return rc;
        }
    }
    return CLI_OK;
}

CLI_COMMAND_REGISTER(repeat,
    "Run a command N times",
    "repeat <count> <command> [args ...]",
    handle);
