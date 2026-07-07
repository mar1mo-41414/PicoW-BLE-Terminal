#include "drivers/adc_ctrl.h"

#include "hardware/adc.h"
#include "hardware/gpio.h"

// adc_init() is idempotent-ish but zeroes state, so track whether we've
// done the one-time bring-up.
static bool g_adc_ready = false;

// Track which ADC-capable pins we've called adc_gpio_init on.
static uint32_t g_pin_ready_mask = 0;

bool adc_ctrl_is_valid(uint pin) {
    return pin >= ADC_CTRL_MIN_PIN && pin <= ADC_CTRL_MAX_PIN;
}

static void ensure_adc(void) {
    if (!g_adc_ready) {
        adc_init();
        g_adc_ready = true;
    }
}

bool adc_ctrl_read(uint pin, uint16_t *raw_out, uint32_t *mv_out) {
    if (!adc_ctrl_is_valid(pin)) return false;

    ensure_adc();

    uint32_t bit = 1u << pin;
    if (!(g_pin_ready_mask & bit)) {
        adc_gpio_init(pin);
        g_pin_ready_mask |= bit;
    }

    adc_select_input(pin - ADC_CTRL_MIN_PIN);
    uint16_t raw = adc_read();

    if (raw_out) *raw_out = raw;
    if (mv_out) {
        // 3300 mV over 12 bits (4096 steps). Compute as uint32 to avoid
        // overflow at max reading.
        *mv_out = (uint32_t)raw * 3300u / 4095u;
    }
    return true;
}
