// `sleep <duration>` — pause the shell without stalling BLE / Wi-Fi.
#include "cli/cli.h"
#include "cli/command.h"
#include "cli/duration.h"

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;
    uint32_t ms;
    if (!cli_parse_duration_ms(argv[1], &ms)) {
        cli_printf(ctx,
            "sleep: bad duration: %s (expected e.g. 500ms, 2s, or bare ms; max 60s)\r\n",
            argv[1]);
        return CLI_ERR_ARG;
    }
    cli_keepalive_sleep_ms(ms);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(sleep,
    "Pause for a duration (ms or Ns)",
    "sleep <ms|s>",
    handle);
