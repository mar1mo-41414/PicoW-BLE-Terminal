// Thin GPIO wrapper. All state changes go through here so pins are
// initialized lazily (first touch sets direction) and callers don't have
// to remember the SDK init dance.
#ifndef PICOBLE_DRIVERS_GPIO_CTRL_H
#define PICOBLE_DRIVERS_GPIO_CTRL_H

#include <stdbool.h>
#include <stdint.h>

#include "pico.h"  // pulls in `uint` typedef

#ifdef __cplusplus
extern "C" {
#endif

// RP2040 exposes GPIO 0..29. Pins 23-25 are wired to the CYW43 on Pico W
// and shouldn't be driven from user code — we don't hard-block them so
// power users can experiment, but gpio_ctrl_is_reserved() flags them so
// the CLI can warn.
#define GPIO_CTRL_MAX_PIN 29

bool gpio_ctrl_is_valid(uint pin);
bool gpio_ctrl_is_reserved(uint pin);   // Pico W: pins 23, 24, 25

// Configure `pin` as output and drive it high / low / toggle.
// Returns false if the pin number is out of range.
bool gpio_ctrl_write(uint pin, bool level);
bool gpio_ctrl_toggle(uint pin);

// Configure `pin` as input (with no pulls) and sample it once.
// Returns false on invalid pin; on success `*level_out` gets the value.
bool gpio_ctrl_read(uint pin, bool *level_out);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_DRIVERS_GPIO_CTRL_H
