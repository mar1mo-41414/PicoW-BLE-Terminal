// Minimal ADC wrapper. Provides one-shot reads from GPIO 26-29.
#ifndef PICOBLE_DRIVERS_ADC_CTRL_H
#define PICOBLE_DRIVERS_ADC_CTRL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// GPIO 26..29 map to ADC channels 0..3. On Pico W, GPIO 29 is wired to
// VSYS/3 rather than an external pin, but you can still read it from
// the CLI — it acts as a battery-voltage monitor.
#define ADC_CTRL_MIN_PIN 26
#define ADC_CTRL_MAX_PIN 29

bool adc_ctrl_is_valid(uint pin);

// Sample once. Returns false on invalid pin or hardware error.
//   *raw_out : 12-bit reading (0..4095)
//   *mv_out  : converted to millivolts assuming 3.3V reference
bool adc_ctrl_read(uint pin, uint16_t *raw_out, uint32_t *mv_out);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_DRIVERS_ADC_CTRL_H
