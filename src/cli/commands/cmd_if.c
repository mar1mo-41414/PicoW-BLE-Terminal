// `if COND... then BODY... [else ELSE...]`
//
// COND / BODY / ELSE are themselves commands with their own arguments.
// The parser splits argv on the literal words "then" and "else". Nesting
// is not supported (any interior `then`/`else` is grabbed by the outer).
#include "cli/cli.h"
#include "cli/command.h"

#include <string.h>

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 4) return CLI_ERR_USAGE;

    int then_idx = -1;
    int else_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (then_idx < 0 && strcmp(argv[i], "then") == 0) then_idx = i;
        else if (then_idx > 0 && else_idx < 0
              && strcmp(argv[i], "else") == 0) else_idx = i;
    }
    if (then_idx < 0 || then_idx == 1) return CLI_ERR_USAGE;

    int cond_argc = then_idx - 1;
    char **cond_argv = &argv[1];

    int then_end = (else_idx > 0) ? else_idx : argc;
    int then_argc = then_end - (then_idx + 1);
    char **then_argv = &argv[then_idx + 1];
    if (then_argc == 0) return CLI_ERR_USAGE;

    int else_argc = (else_idx > 0) ? argc - (else_idx + 1) : 0;
    char **else_argv = (else_idx > 0) ? &argv[else_idx + 1] : NULL;

    int rc = cli_dispatch_argv(ctx, cond_argc, cond_argv);
    if (rc == CLI_OK) {
        return cli_dispatch_argv(ctx, then_argc, then_argv);
    }
    if (else_argc > 0) {
        return cli_dispatch_argv(ctx, else_argc, else_argv);
    }
    return CLI_OK;
}

CLI_COMMAND_REGISTER(if,
    "Conditional: run BODY if COND succeeds, else ELSE",
    "if COND then BODY [else ELSE]",
    handle);
