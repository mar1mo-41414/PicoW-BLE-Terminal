// Sink-fan-out log implementation. See log.h for the model.
#include "system/log.h"

#include <stdio.h>

static log_sink_t  *g_sinks = NULL;
static log_level_t  g_level = LOG_LEVEL_INFO;

void log_set_level(log_level_t level) { g_level = level; }
log_level_t log_get_level(void)       { return g_level; }

void log_add_sink(log_sink_t *sink) {
    if (!sink || !sink->write) return;
    // De-dupe: if the same struct is already in the list, don't chain
    // it to itself. Callers occasionally re-init a transport.
    for (log_sink_t *s = g_sinks; s; s = s->_next) {
        if (s == sink) return;
    }
    sink->_next = g_sinks;
    g_sinks = sink;
}

void log_vwrite(log_level_t level, const char *fmt, va_list ap) {
    if (level > g_level) return;

    // Two-buffer approach: format once into a stack buffer, then hand
    // the bytes to each sink. Keeps sinks simple (no re-formatting).
    char buf[192];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n <= 0) return;
    size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;

    for (log_sink_t *s = g_sinks; s; s = s->_next) {
        s->write(level, buf, len, s->user);
    }
}

void log_write(log_level_t level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vwrite(level, fmt, ap);
    va_end(ap);
}
