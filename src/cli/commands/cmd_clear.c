// `clear` — VT100 clear screen + home cursor. Works with every modern
// terminal emulator; harmless on clients that ignore escape sequences.
#include "cli/cli.h"
#include "cli/command.h"

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argc; (void)argv;
    cli_write(ctx, "\x1b[2J\x1b[H");
    return CLI_OK;
}

CLI_COMMAND_REGISTER(clear,
    "Clear the screen",
    "clear",
    handle);
