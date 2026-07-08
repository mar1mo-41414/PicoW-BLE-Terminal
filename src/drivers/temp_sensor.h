// RP2040 on-die temperature sensor (ADC channel 4).
#ifndef PICOBLE_DRIVERS_TEMP_SENSOR_H
#define PICOBLE_DRIVERS_TEMP_SENSOR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Read the internal temperature sensor and return degrees Celsius.
// Returns false only if the ADC block fails to come up (unexpected).
bool temp_sensor_read_c(float *celsius);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_DRIVERS_TEMP_SENSOR_H
