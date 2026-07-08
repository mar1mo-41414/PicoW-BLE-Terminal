#include "cli/duration.h"

#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include <stdlib.h>
#include <string.h>

#define DURATION_MAX_MS  (60u * 1000u)  // 60 seconds

bool cli_parse_duration_ms(const char *s, uint32_t *out_ms) {
    if (!s || !*s || !out_ms) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s) return false;

    uint32_t ms;
    if (*end == '\0' || strcmp(end, "ms") == 0) {
        ms = (uint32_t)v;
    } else if (strcmp(end, "s") == 0) {
        if (v > DURATION_MAX_MS / 1000u) return false;
        ms = (uint32_t)v * 1000u;
    } else {
        return false;
    }
    if (ms == 0 || ms > DURATION_MAX_MS) return false;
    *out_ms = ms;
    return true;
}

void cli_keepalive_sleep_ms(uint32_t ms) {
    absolute_time_t deadline = make_timeout_time_ms(ms);
    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        cyw43_arch_poll();
        sleep_ms(2);
    }
}
