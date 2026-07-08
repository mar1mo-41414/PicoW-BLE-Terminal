// `while COND... do BODY...`
//
// Loops as long as COND returns CLI_OK. Hard cap at 10000 iterations
// so a runaway condition can't lock the shell forever.
#include "cli/cli.h"
#include "cli/command.h"

#include <string.h>

#define WHILE_MAX_ITER 10000

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 4) return CLI_ERR_USAGE;

    int do_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "do") == 0) { do_idx = i; break; }
    }
    if (do_idx < 0 || do_idx == 1) return CLI_ERR_USAGE;

    int cond_argc = do_idx - 1;
    char **cond_argv = &argv[1];
    int body_argc = argc - (do_idx + 1);
    char **body_argv = &argv[do_idx + 1];
    if (body_argc == 0) return CLI_ERR_USAGE;

    for (int i = 0; i < WHILE_MAX_ITER; i++) {
        int rc = cli_dispatch_argv(ctx, cond_argc, cond_argv);
        if (rc != CLI_OK) return CLI_OK;
        rc = cli_dispatch_argv(ctx, body_argc, body_argv);
        if (rc == CLI_ERR_HARDWARE) {
            cli_printf(ctx, "while: aborted after %d iteration(s) (hardware error)\r\n", i);
            return rc;
        }
    }
    cli_printf(ctx, "while: iteration cap (%d) reached\r\n", WHILE_MAX_ITER);
    return CLI_ERR_HARDWARE;
}

CLI_COMMAND_REGISTER(while,
    "Loop BODY while COND succeeds (10000-iteration safety cap)",
    "while COND do BODY",
    handle);
