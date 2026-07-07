// Interactive shell surface. A cli_ctx_t represents one input source
// (typically the BLE NUS connection, or USB CDC when we mirror there).
//
// Input flow:
//   cli_feed(ctx, buf, len)   -- push bytes as they arrive
//   the CLI accumulates until it sees LF (CR is tolerated / dropped)
//   then it parses and dispatches to a registered command
//
// Output flow:
//   commands call cli_write / cli_printf on their ctx; the ctx forwards
//   to whatever sink was installed (BLE notify, USB fwrite, log ring...)
#ifndef PICOBLE_CLI_H
#define PICOBLE_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// Output sink. Return 0 on success, negative on transport error. Errors
// are silently dropped by the CLI — the shell does not attempt retries.
typedef int (*cli_output_fn)(const char *data, size_t len, void *user);

#define CLI_LINE_MAX 256

typedef struct cli_ctx {
    cli_output_fn out;
    void         *user;

    char   line[CLI_LINE_MAX];
    size_t line_len;

    // If the last byte we consumed was CR, swallow a following LF so a
    // CRLF pair fires the command once, not twice.
    unsigned last_was_cr : 1;
} cli_ctx_t;

// Initialize a context. `out` is invoked whenever the CLI has bytes to
// send; if NULL the CLI silently drops output.
void cli_init(cli_ctx_t *ctx, cli_output_fn out, void *user);

// Emit the banner and first prompt. Call after the transport is up
// (e.g. right after a BLE connection is established).
void cli_greet(cli_ctx_t *ctx);

// Feed received bytes. Multiple lines in a single buffer are OK; the CLI
// splits on LF and tolerates CR (both bare CR and CRLF).
void cli_feed(cli_ctx_t *ctx, const uint8_t *data, size_t len);

// Reset accumulated line state — call on disconnect so a half-typed
// line from a previous session doesn't leak into the next.
void cli_reset(cli_ctx_t *ctx);

// Output helpers usable from command handlers.
int cli_write(cli_ctx_t *ctx, const char *s);
int cli_writen(cli_ctx_t *ctx, const char *s, size_t n);
int cli_printf(cli_ctx_t *ctx, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int cli_vprintf(cli_ctx_t *ctx, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_CLI_H
