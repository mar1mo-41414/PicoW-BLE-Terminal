// `bind gpio <pin> <edge> <cmd...>`
//
// edge = high|low|change (aliases: rise/rising, fall/falling, both)
// cmd  = the rest of the line, joined with spaces, is the command to
//        run whenever the edge fires.
//
// Example:
//   bind gpio 2 high  gpio 5 latch 500ms
//   bind gpio 3 change run alarm
//   bind gpio 4 rise  "gpio 5 high"
#include "cli/cli.h"
#include "cli/command.h"
#include "system/bindings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 5) return CLI_ERR_USAGE;
    if (strcmp(argv[1], "gpio") != 0) return CLI_ERR_USAGE;

    char *end;
    unsigned long pin = strtoul(argv[2], &end, 10);
    if (*end != '\0' || pin > 29) {
        cli_printf(ctx, "bind: bad pin: %s\r\n", argv[2]);
        return CLI_ERR_ARG;
    }

    bind_edge_t edge;
    if (!bindings_parse_edge(argv[3], &edge)) {
        cli_printf(ctx, "bind: bad edge: %s (expected high|low|change / rise|fall / both)\r\n", argv[3]);
        return CLI_ERR_ARG;
    }

    // Concatenate argv[4..argc-1] into a single command string. Space
    // between tokens is the only separator; quoted tokens keep their
    // internal whitespace because the parser already collapsed them.
    char target[BIND_TARGET_MAX + 1];
    size_t off = 0;
    for (int i = 4; i < argc; i++) {
        size_t len = strlen(argv[i]);
        if (off + len + 1 >= sizeof(target)) {
            cli_printf(ctx, "bind: command too long (max %d chars)\r\n", BIND_TARGET_MAX);
            return CLI_ERR_ARG;
        }
        if (off > 0) target[off++] = ' ';
        memcpy(&target[off], argv[i], len);
        off += len;
    }
    target[off] = '\0';

    if (!bindings_add((uint)pin, edge, target, /*debounce_ms=*/20)) {
        cli_write(ctx, "bind: no free slot or bad target\r\n");
        return CLI_ERR_HARDWARE;
    }
    cli_printf(ctx, "bind: gpio %lu %s -> %s\r\n", pin, bindings_edge_str(edge), target);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(bind,
    "Run a command whenever a GPIO edge fires",
    "bind gpio <pin> <high|low|change> <command...>",
    handle);
