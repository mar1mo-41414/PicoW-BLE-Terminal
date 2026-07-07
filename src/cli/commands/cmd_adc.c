// `adc <pin> read`
#include "cli/cli.h"
#include "cli/command.h"
#include "drivers/adc_ctrl.h"

#include <stdlib.h>
#include <string.h>

static bool parse_pin(const char *s, uint *out) {
    if (!s || !*s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*end != '\0') return false;
    if (v > 255u) return false;
    *out = (uint)v;
    return true;
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 3) return CLI_ERR_USAGE;

    uint pin;
    if (!parse_pin(argv[1], &pin) || !adc_ctrl_is_valid(pin)) {
        cli_printf(ctx, "adc: pin must be %u..%u (got %s)\r\n",
                   (unsigned)ADC_CTRL_MIN_PIN,
                   (unsigned)ADC_CTRL_MAX_PIN, argv[1]);
        return CLI_ERR_ARG;
    }

    if (strcmp(argv[2], "read") != 0) {
        cli_printf(ctx, "adc: unknown action: %s\r\n", argv[2]);
        return CLI_ERR_USAGE;
    }

    uint16_t raw;
    uint32_t mv;
    if (!adc_ctrl_read(pin, &raw, &mv)) {
        cli_write(ctx, "adc: read failed\r\n");
        return CLI_ERR_HARDWARE;
    }
    cli_printf(ctx, "raw=%u  mv=%lu\r\n", (unsigned)raw, (unsigned long)mv);
    return CLI_OK;
}

CLI_COMMAND_REGISTER(adc,
    "Read an ADC-capable pin (GPIO 26-29)",
    "adc <pin> read",
    handle);
