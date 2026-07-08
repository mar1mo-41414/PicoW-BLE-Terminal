// `bindings`             — list currently installed bindings.
// `bindings save`        — persist current bindings to flash.
// `bindings load`        — reload from flash (drops in-memory changes).
#include "cli/cli.h"
#include "cli/command.h"
#include "system/bindings.h"

#include <string.h>

static void list_one(int slot, const bind_snapshot_t *b, void *user) {
    cli_ctx_t *ctx = user;
    cli_printf(ctx, "  [%2d] gpio %2u %-6s (debounce=%ums) -> %s\r\n",
               slot, (unsigned)b->pin, bindings_edge_str(b->edge),
               (unsigned)b->debounce_ms, b->target);
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc == 1) {
        if (bindings_count() == 0) {
            cli_write(ctx, "(no bindings)\r\n");
            return CLI_OK;
        }
        bindings_iter(list_one, ctx);
        return CLI_OK;
    }
    if (strcmp(argv[1], "save") == 0) {
        return bindings_save() ? CLI_OK : CLI_ERR_HARDWARE;
    }
    if (strcmp(argv[1], "load") == 0) {
        return bindings_load() ? CLI_OK : CLI_ERR_HARDWARE;
    }
    cli_printf(ctx, "bindings: unknown subcommand: %s\r\n", argv[1]);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(bindings,
    "List / save / load GPIO bindings",
    "bindings [save | load]",
    handle);
