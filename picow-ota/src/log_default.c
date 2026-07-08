// Default (silent) logging implementations. Marked weak so an
// application can override any or all of them with real routing.
#include "pico_ota/log.h"

__attribute__((weak))
void pico_ota_log_info(const char *fmt, ...) { (void)fmt; }

__attribute__((weak))
void pico_ota_log_warn(const char *fmt, ...) { (void)fmt; }

__attribute__((weak))
void pico_ota_log_error(const char *fmt, ...) { (void)fmt; }
