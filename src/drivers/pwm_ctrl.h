// PWM output on any GPIO pin.
//
// Wraps pico-sdk hardware_pwm. Each pin maps to a (slice, channel) pair;
// two adjacent pins may share a slice and therefore a frequency, so
// pwm_ctrl_set() re-programs the slice from scratch each call.
#ifndef PICOBLE_DRIVERS_PWM_CTRL_H
#define PICOBLE_DRIVERS_PWM_CTRL_H

#include "pico.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Drive pin as PWM at freq_hz with the given duty (0..100 %).
// Frequency range is roughly 8 Hz .. ~1 MHz at usable resolution.
bool pwm_ctrl_set(uint pin, uint32_t freq_hz, uint8_t duty_pct);

// Stop driving pin; returns the pin to GPIO function.
bool pwm_ctrl_off(uint pin);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_DRIVERS_PWM_CTRL_H
