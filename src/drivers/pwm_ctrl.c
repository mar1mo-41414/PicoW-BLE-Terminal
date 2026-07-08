#include "drivers/pwm_ctrl.h"

#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

// Compute divider (17-bit fixed-point 12.4) and wrap so that
//   sys_hz / (div_1616 / 16 * (wrap + 1)) ≈ freq_hz.
// We fix wrap to 65535 and scale div; keeps 16-bit duty resolution.
static bool compute_div_wrap(uint32_t freq_hz, uint32_t *div_1616, uint16_t *wrap) {
    if (freq_hz == 0) return false;
    uint32_t sys_hz = clock_get_hz(clk_sys);
    if (sys_hz == 0) return false;

    *wrap = 65535u;
    // div_1616 = (sys_hz << 4) / (freq * (wrap+1))
    uint64_t d = ((uint64_t)sys_hz << 4) / ((uint64_t)freq_hz * (uint64_t)(*wrap + 1u));
    if (d < 16u)          d = 16u;                 // minimum divider is 1.0
    if (d >= (256u << 4)) d = (256u << 4) - 1u;    // maximum divider is 255.9375
    *div_1616 = (uint32_t)d;
    return true;
}

bool pwm_ctrl_set(uint pin, uint32_t freq_hz, uint8_t duty_pct) {
    if (pin > 29 || duty_pct > 100) return false;

    uint32_t div_1616;
    uint16_t wrap;
    if (!compute_div_wrap(freq_hz, &div_1616, &wrap)) return false;

    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&cfg, div_1616 >> 4, div_1616 & 0xFu);
    pwm_config_set_wrap(&cfg, wrap);
    pwm_init(slice, &cfg, true);

    uint16_t level = (uint16_t)(((uint32_t)wrap + 1u) * (uint32_t)duty_pct / 100u);
    pwm_set_gpio_level(pin, level);
    return true;
}

bool pwm_ctrl_off(uint pin) {
    if (pin > 29) return false;
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice, false);
    // Return the pin to plain GPIO (input high-Z).
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
    return true;
}
