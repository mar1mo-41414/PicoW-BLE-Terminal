#include "drivers/temp_sensor.h"

#include "hardware/adc.h"

static bool g_ready = false;

// RP2040 datasheet §4.9.5: T = 27 - (Vbe - 0.706) / 0.001721
// Vbe = raw * 3.3 / 4095
bool temp_sensor_read_c(float *celsius) {
    if (!celsius) return false;
    if (!g_ready) {
        adc_init();
        adc_set_temp_sensor_enabled(true);
        g_ready = true;
    }
    adc_select_input(4);
    uint16_t raw = adc_read();
    float vbe = (float)raw * 3.3f / 4095.0f;
    *celsius = 27.0f - (vbe - 0.706f) / 0.001721f;
    return true;
}
