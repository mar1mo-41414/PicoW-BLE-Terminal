#include "system/uptime.h"

#include "pico/time.h"

#include <stdio.h>

uint32_t uptime_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

uint64_t uptime_us(void) {
    return to_us_since_boot(get_absolute_time());
}

size_t uptime_format(char *buf, size_t buflen) {
    if (!buf || buflen == 0) return 0;
    uint32_t total_s = uptime_ms() / 1000u;
    uint32_t d = total_s / 86400u;
    uint32_t h = (total_s / 3600u) % 24u;
    uint32_t m = (total_s / 60u) % 60u;
    uint32_t s = total_s % 60u;

    int n;
    if (d > 0) {
        n = snprintf(buf, buflen, "%ud %02u:%02u:%02u", d, h, m, s);
    } else {
        n = snprintf(buf, buflen, "%02u:%02u:%02u", h, m, s);
    }
    return n > 0 ? (size_t)n : 0;
}
