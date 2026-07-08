// `run <name>` — execute a stored script line by line.
#include "cli/cli.h"
#include "cli/command.h"
#include "storage/scripts.h"

#include <string.h>

// Nested `run` is not supported — the CRC-checked "open" slot in
// storage/scripts.c is a shared single-use resource. Users compose
// with repeat / set / sleep instead.
static bool g_running = false;

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;

    if (g_running) {
        cli_write(ctx, "run: nested run is not supported (yet)\r\n");
        return CLI_ERR_ARG;
    }

    const uint8_t *body;
    size_t body_len;
    if (!scripts_open(argv[1], &body, &body_len)) {
        cli_printf(ctx, "run: no such script or CRC failure: %s\r\n", argv[1]);
        return CLI_ERR_ARG;
    }
    g_running = true;

    // Iterate the body directly from XIP — one shell-sized scratch line
    // per iteration is all the stack we spend.
    char line[CLI_LINE_MAX];
    size_t off = 0;
    while (off < body_len) {
        size_t ll = 0;
        while (off < body_len && body[off] != '\n' && body[off] != '\r'
               && ll < sizeof(line) - 1) {
            line[ll++] = (char)body[off++];
        }
        line[ll] = '\0';
        // Skip past terminators
        while (off < body_len && (body[off] == '\n' || body[off] == '\r')) off++;

        // Skip blank lines and comments — Unix-shell friendly.
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        cli_dispatch_line(ctx, line);
    }

    g_running = false;
    scripts_close();
    return CLI_OK;
}

CLI_COMMAND_REGISTER(run,
    "Execute a stored script line by line",
    "run <name>",
    handle);
