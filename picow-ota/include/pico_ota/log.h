// pico-ota logging hooks.
//
// The framework calls these from ota.c when it wants to say something
// (erase progress, verification result, metadata migration...). They
// are declared as weak symbols with no-op default implementations, so
// an application can choose to override them with real logging (route
// to its own log system, printf, syslog, wherever) OR leave them
// undefined and the framework stays completely silent.
//
// Level constants intentionally mirror the classic syslog / most
// project loggers so users can pass them straight through.
#ifndef PICO_OTA_LOG_H
#define PICO_OTA_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void pico_ota_log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void pico_ota_log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void pico_ota_log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif  // PICO_OTA_LOG_H
