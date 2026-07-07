// Internal log subsystem.
//
// The application writes log_write(level, "...") once. Every registered
// sink receives the same bytes. A sink is a small vtable owned by the
// transport module (BLE, USB CDC, future Wi-Fi TCP, syslog, ...).
//
// Sinks are opt-in: the log has no default output. Boot code calls
// log_add_sink() once per transport that comes up.
//
// Not thread-safe. Everything on this Pico W runs on core0 today —
// BTStack, CYW43 driver, and the CLI all pump the same event loop, so
// there is no re-entrancy to guard against. If we ever start using the
// second core we'll need to add a critical section here.
#ifndef PICOBLE_SYSTEM_LOG_H
#define PICOBLE_SYSTEM_LOG_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3,
} log_level_t;

// A sink writes the formatted bytes somewhere. It receives the level so
// it can filter or colorize; the payload does NOT include a trailing
// newline — the sink adds one if the destination needs one.
typedef struct log_sink {
    void (*write)(log_level_t level, const char *msg, size_t len, void *user);
    void *user;
    struct log_sink *_next;
} log_sink_t;

// Set the minimum level actually broadcast. Anything below is silently
// dropped before it reaches any sink.
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

// Register a sink. Storage is caller-owned — the log keeps the pointer.
// Registering the same struct twice is a no-op.
void log_add_sink(log_sink_t *sink);

// Formatted log line. The message itself should not include a newline.
void log_write(log_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_vwrite(log_level_t level, const char *fmt, va_list ap);

// Level-tagged convenience macros so call sites read naturally.
#define LOGE(...) log_write(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOGW(...) log_write(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOGI(...) log_write(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOGD(...) log_write(LOG_LEVEL_DEBUG, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_SYSTEM_LOG_H
