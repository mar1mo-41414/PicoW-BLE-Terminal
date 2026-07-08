// `pwm <pin> <freq_hz> <duty_pct>` — start / adjust PWM on a pin.
// `pwm <pin> off`                   — stop PWM, return pin to GPIO.
#include "cli/cli.h"
#include "cli/command.h"
#include "drivers/pwm_ctrl.h"

#include <stdlib.h>
#include <string.h>

static bool parse_uint(const char *s, unsigned long max, unsigned long *out) {
    if (!s || !*s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*end != '\0' || v > max) return false;
    *out = v;
    return true;
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 3) return CLI_ERR_USAGE;

    unsigned long pin;
    if (!parse_uint(argv[1], 29, &pin)) {
        cli_printf(ctx, "pwm: bad pin: %s\r\n", argv[1]);
        return CLI_ERR_ARG;
    }

    if (strcmp(argv[2], "off") == 0) {
        return pwm_ctrl_off((uint)pin) ? CLI_OK : CLI_ERR_HARDWARE;
    }

    if (argc < 4) return CLI_ERR_USAGE;
    unsigned long freq, duty;
    if (!parse_uint(argv[2], 1000000, &freq) || freq == 0) {
        cli_printf(ctx, "pwm: bad freq: %s\r\n", argv[2]);
        return CLI_ERR_ARG;
    }
    if (!parse_uint(argv[3], 100, &duty)) {
        cli_printf(ctx, "pwm: bad duty %s (0..100)\r\n", argv[3]);
        return CLI_ERR_ARG;
    }
    if (!pwm_ctrl_set((uint)pin, (uint32_t)freq, (uint8_t)duty)) {
        cli_write(ctx, "pwm: hardware setup failed\r\n");
        return CLI_ERR_HARDWARE;
    }
    return CLI_OK;
}

CLI_COMMAND_REGISTER(pwm,
    "Drive a GPIO pin as PWM (or turn it off)",
    "pwm <pin> <freq_hz> <duty_pct> | pwm <pin> off",
    handle);
