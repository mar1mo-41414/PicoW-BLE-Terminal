// `temp` — read the on-die temperature sensor.
#include "cli/cli.h"
#include "cli/command.h"
#include "drivers/temp_sensor.h"

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argc; (void)argv;
    float c;
    if (!temp_sensor_read_c(&c)) {
        cli_write(ctx, "temp: read failed\r\n");
        return CLI_ERR_HARDWARE;
    }
    // No printf floats on Cortex-M0+ by default (linker), so format the
    // integer + one decimal by hand.
    int c10 = (int)(c * 10.0f + (c >= 0 ? 0.5f : -0.5f));
    cli_printf(ctx, "%d.%d C\r\n", c10 / 10, c10 < 0 ? -c10 % 10 : c10 % 10);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(temp,
    "Read the RP2040 on-die temperature sensor",
    "temp",
    handle);
