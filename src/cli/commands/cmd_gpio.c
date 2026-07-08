// `gpio <pin> read|high|low|toggle|latch <duration>`
#include "cli/cli.h"
#include "cli/command.h"
#include "drivers/gpio_ctrl.h"

#include "pico/cyw43_arch.h"
#include "pico/time.h"

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

// Parse "500ms", "2s", "100" (bare = ms). Cap at 60 s so a fat-fingered
// argument can't silently pin the shell for hours.
static bool parse_duration_ms(const char *s, uint32_t *out) {
    if (!s || !*s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s) return false;                 // no digits at all

    uint32_t ms;
    if (*end == '\0' || strcmp(end, "ms") == 0) {
        ms = (uint32_t)v;
    } else if (strcmp(end, "s") == 0) {
        if (v > 60) return false;
        ms = (uint32_t)v * 1000u;
    } else {
        return false;                            // unknown suffix
    }
    if (ms == 0 || ms > 60u * 1000u) return false;
    *out = ms;
    return true;
}

// Sleep for the requested duration WITHOUT starving the Wi-Fi (poll)
// stack. sleep_ms alone would freeze cyw43 processing — same trap
// http_get had to avoid.
static void keepalive_sleep_ms(uint32_t ms) {
    absolute_time_t deadline = make_timeout_time_ms(ms);
    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        cyw43_arch_poll();
        sleep_ms(2);
    }
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
        if (!parse_duration_ms(argv[3], &ms)) {
            cli_printf(ctx,
                "gpio: bad duration: %s (expected e.g. 500ms, 2s, or bare ms; max 60s)\r\n",
                argv[3]);
            return CLI_ERR_ARG;
        }
        if (!gpio_ctrl_write(pin, true))  return CLI_ERR_HARDWARE;
        keepalive_sleep_ms(ms);
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
