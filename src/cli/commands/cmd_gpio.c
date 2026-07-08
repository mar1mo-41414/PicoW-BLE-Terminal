// `gpio <pin> read|high|low|toggle|latch <duration>`
#include "cli/cli.h"
#include "cli/command.h"
#include "cli/duration.h"
#include "drivers/gpio_ctrl.h"

#include <stdlib.h>
#include <string.h>

static bool parse_pin(const char *s, uint *out) {
    if (!s || !*s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*end != '\0') return false;
    if (v > GPIO_CTRL_MAX_PIN) return false;
    *out = (uint)v;
    return true;
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 3) return CLI_ERR_USAGE;

    uint pin;
    if (!parse_pin(argv[1], &pin)) {
        cli_printf(ctx, "gpio: invalid pin: %s\r\n", argv[1]);
        return CLI_ERR_ARG;
    }
    if (gpio_ctrl_is_reserved(pin)) {
        // Warn but proceed — the user may know what they're doing.
        cli_printf(ctx, "gpio: warning: pin %u is reserved for CYW43\r\n", pin);
    }

    const char *op = argv[2];
    if (strcmp(op, "read") == 0) {
        bool level;
        if (!gpio_ctrl_read(pin, &level)) {
            cli_write(ctx, "gpio: read failed\r\n");
            return CLI_ERR_HARDWARE;
        }
        cli_printf(ctx, "%u\r\n", level ? 1u : 0u);
        return CLI_OK;
    }
    if (strcmp(op, "high") == 0) {
        return gpio_ctrl_write(pin, true) ? CLI_OK : CLI_ERR_HARDWARE;
    }
    if (strcmp(op, "low") == 0) {
        return gpio_ctrl_write(pin, false) ? CLI_OK : CLI_ERR_HARDWARE;
    }
    if (strcmp(op, "toggle") == 0) {
        return gpio_ctrl_toggle(pin) ? CLI_OK : CLI_ERR_HARDWARE;
    }
    if (strcmp(op, "latch") == 0) {
        if (argc < 4) return CLI_ERR_USAGE;
        uint32_t ms;
        if (!cli_parse_duration_ms(argv[3], &ms)) {
            cli_printf(ctx,
                "gpio: bad duration: %s (expected e.g. 500ms, 2s, or bare ms; max 60s)\r\n",
                argv[3]);
            return CLI_ERR_ARG;
        }
        if (!gpio_ctrl_write(pin, true))  return CLI_ERR_HARDWARE;
        cli_keepalive_sleep_ms(ms);
        if (!gpio_ctrl_write(pin, false)) return CLI_ERR_HARDWARE;
        return CLI_OK;
    }

    cli_printf(ctx, "gpio: unknown action: %s\r\n", op);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(gpio,
    "Read or drive a GPIO pin",
    "gpio <pin> read|high|low|toggle|latch <ms|s>",
    handle);
