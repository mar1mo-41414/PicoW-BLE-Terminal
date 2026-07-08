#include "drivers/i2c_ctrl.h"

#include "hardware/i2c.h"
#include "hardware/gpio.h"

static bool g_ready[I2C_BUS_MAX] = { false, false };

static i2c_inst_t *bus_inst(uint bus) {
    return (bus == 0) ? i2c0 : i2c1;
}

static void ensure_init(uint bus) {
    if (bus >= I2C_BUS_MAX || g_ready[bus]) return;
    uint sda = (bus == 0) ? 4 : 6;
    uint scl = (bus == 0) ? 5 : 7;
    i2c_init(bus_inst(bus), 100 * 1000);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    g_ready[bus] = true;
}

size_t i2c_ctrl_scan(uint bus, uint8_t *addrs, size_t addrs_cap) {
    if (bus >= I2C_BUS_MAX || !addrs || addrs_cap == 0) return 0;
    ensure_init(bus);
    size_t n = 0;
    // 7-bit range excluding reserved regions (0x00-0x07 and 0x78-0x7F).
    for (uint8_t a = 0x08; a < 0x78; a++) {
        uint8_t dummy;
        int r = i2c_read_blocking(bus_inst(bus), a, &dummy, 1, false);
        if (r >= 0 && n < addrs_cap) addrs[n++] = a;
    }
    return n;
}

int i2c_ctrl_read(uint bus, uint8_t addr, uint8_t *dst, size_t len) {
    if (bus >= I2C_BUS_MAX || !dst || len == 0) return -1;
    ensure_init(bus);
    return i2c_read_blocking(bus_inst(bus), addr, dst, len, false);
}

int i2c_ctrl_write(uint bus, uint8_t addr, const uint8_t *src, size_t len) {
    if (bus >= I2C_BUS_MAX || !src || len == 0) return -1;
    ensure_init(bus);
    return i2c_write_blocking(bus_inst(bus), addr, src, len, false);
}
