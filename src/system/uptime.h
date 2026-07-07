// Uptime helpers.
#ifndef PICOBLE_SYSTEM_UPTIME_H
#define PICOBLE_SYSTEM_UPTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Milliseconds since boot. Wraps every ~49 days (uint32_t). Callers that
// need to compare across long spans should use uptime_us().
uint32_t uptime_ms(void);
uint64_t uptime_us(void);

// Human-friendly form: "1d 02:34:56" (drops the "Nd " when < 24h).
// Writes into caller-provided buffer; returns bytes written excluding NUL.
size_t uptime_format(char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_SYSTEM_UPTIME_H
