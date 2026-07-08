// I2C master wrapper for the two RP2040 controllers.
//
// Bus 0 defaults to GPIO 4 (SDA) / 5 (SCL); bus 1 to GPIO 6 / 7. Both
// buses are brought up lazily on first access at 100 kHz with internal
// pull-ups enabled — good enough for STEMMA / Grove modules on a
// breadboard, adjust the CMake or wire external pull-ups if you push
// past 400 kHz.
#ifndef PICOBLE_DRIVERS_I2C_CTRL_H
#define PICOBLE_DRIVERS_I2C_CTRL_H

#include "pico.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_BUS_MAX 2

// Scan the 7-bit address range 0x08..0x77 with a 1-byte read probe.
// Writes acknowledged addresses into `addrs` in ascending order and
// returns how many were found (capped at addrs_cap).
size_t i2c_ctrl_scan(uint bus, uint8_t *addrs, size_t addrs_cap);

// Blocking read `len` bytes from `addr`. Returns bytes read, or
// negative on NACK / bus error.
int i2c_ctrl_read(uint bus, uint8_t addr, uint8_t *dst, size_t len);

// Blocking write `len` bytes to `addr`.
int i2c_ctrl_write(uint bus, uint8_t addr, const uint8_t *src, size_t len);

// Compact bus validity check for the CLI to bounce bad inputs.
static inline bool i2c_ctrl_is_valid_bus(uint bus) { return bus < I2C_BUS_MAX; }

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_DRIVERS_I2C_CTRL_H
