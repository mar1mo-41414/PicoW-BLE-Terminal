// `reboot` — soft reset via the watchdog. The BLE stack shuts down
// cleanly enough that the peer will just see a disconnect and can
// reconnect once we're back up.
#include "cli/cli.h"
#include "cli/command.h"

#include "hardware/watchdog.h"
#include "pico/time.h"

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argc; (void)argv;

    cli_write(ctx, "rebooting...\r\n");

    // Give the transport a moment to flush the goodbye before the reset
    // pulls the rug. 50 ms is enough for a BLE notification and lwIP
    // still runs its poll loop off the same timer.
    sleep_ms(50);

    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();  // unreachable
}

CLI_COMMAND_REGISTER(reboot,
    "Soft-reset the device",
    "reboot",
    handle);
