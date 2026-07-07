#include "drivers/gpio_ctrl.h"

#include "hardware/gpio.h"

// Track which pins we've already gpio_init()'d so we don't churn the
// pad every write. One bit per GPIO — trivially fits in a uint32_t.
static uint32_t g_initialized = 0;

static void ensure_init(uint pin) {
    uint32_t mask = 1u << pin;
    if (!(g_initialized & mask)) {
        gpio_init(pin);
        g_initialized |= mask;
    }
}

bool gpio_ctrl_is_valid(uint pin) {
    return pin <= GPIO_CTRL_MAX_PIN;
}

bool gpio_ctrl_is_reserved(uint pin) {
    // On Pico W the CYW43 shares pins 23 (WL_ON), 24 (WL_D), 25 (WL_CS).
    // Everything else is fair game.
    return pin == 23 || pin == 24 || pin == 25;
}

bool gpio_ctrl_write(uint pin, bool level) {
    if (!gpio_ctrl_is_valid(pin)) return false;
    ensure_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, level);
    return true;
}

bool gpio_ctrl_toggle(uint pin) {
    if (!gpio_ctrl_is_valid(pin)) return false;
    ensure_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_xor_mask(1u << pin);
    return true;
}

bool gpio_ctrl_read(uint pin, bool *level_out) {
    if (!gpio_ctrl_is_valid(pin) || !level_out) return false;
    ensure_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    *level_out = gpio_get(pin);
    return true;
}
