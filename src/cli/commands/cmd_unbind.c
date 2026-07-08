// `unbind gpio <pin>` — remove one binding.
// `unbind all`        — wipe every binding.
#include "cli/cli.h"
#include "cli/command.h"
#include "system/bindings.h"

#include <stdlib.h>
#include <string.h>

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;

    if (strcmp(argv[1], "all") == 0) {
        bindings_clear_all();
        cli_write(ctx, "unbind: all bindings cleared (not yet persisted; run `bindings save`)\r\n");
        return CLI_OK;
    }
    if (strcmp(argv[1], "gpio") != 0 || argc < 3) return CLI_ERR_USAGE;

    char *end;
    unsigned long pin = strtoul(argv[2], &end, 10);
    if (*end != '\0' || pin > 29) {
        cli_printf(ctx, "unbind: bad pin: %s\r\n", argv[2]);
        return CLI_ERR_ARG;
    }
    if (!bindings_remove((uint)pin)) {
        cli_printf(ctx, "unbind: no binding on gpio %lu\r\n", pin);
        return CLI_ERR_ARG;
    }
    cli_printf(ctx, "unbind: removed binding on gpio %lu\r\n", pin);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(unbind,
    "Remove a GPIO binding",
    "unbind gpio <pin> | unbind all",
    handle);
