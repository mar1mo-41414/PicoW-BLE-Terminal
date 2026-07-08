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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Output sink. Return 0 on success, negative on transport error. Errors
// are silently dropped by the CLI — the shell does not attempt retries.
typedef int (*cli_output_fn)(const char *data, size_t len, void *user);

#define CLI_LINE_MAX 256

// Forward — the capture callback receives whole lines when a command
// has put the shell into multi-line capture mode (e.g. `script save`).
struct cli_ctx;
typedef void (*cli_capture_fn)(struct cli_ctx *ctx, const char *line);

typedef struct cli_ctx {
    cli_output_fn out;
    void         *user;

    char   line[CLI_LINE_MAX];
    size_t line_len;

    unsigned last_was_cr : 1;

    // Multi-line capture (heredoc). When non-NULL, incoming lines go
    // to this callback instead of the normal command dispatcher. The
    // callback ends the capture by calling cli_capture_end(ctx).
    cli_capture_fn capture_cb;
} cli_ctx_t;

// Initialize a context. `out` is invoked whenever the CLI has bytes to
// send; if NULL the CLI silently drops output.
void cli_init(cli_ctx_t *ctx, cli_output_fn out, void *user);

// Emit the banner and first prompt.
void cli_greet(cli_ctx_t *ctx);

// Feed received bytes. Multiple lines in a single buffer are OK; the CLI
// splits on LF and tolerates CR (both bare CR and CRLF).
void cli_feed(cli_ctx_t *ctx, const uint8_t *data, size_t len);

// Reset accumulated line state and clear any capture mode.
void cli_reset(cli_ctx_t *ctx);

// Feed AND, if the accumulated buffer has content that isn't
// newline-terminated, dispatch it. Use this for packet-oriented
// transports like BLE UART.
void cli_feed_line(cli_ctx_t *ctx, const uint8_t *data, size_t len);

// Output helpers usable from command handlers.
int cli_write(cli_ctx_t *ctx, const char *s);
int cli_writen(cli_ctx_t *ctx, const char *s, size_t n);
int cli_printf(cli_ctx_t *ctx, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int cli_vprintf(cli_ctx_t *ctx, const char *fmt, va_list ap);

// Parse a mutable line, expand $NAME variables, and dispatch. Returns
// the handler's return code or a negative CLI_ERR_* for parse errors.
// `line` is destructively tokenized; caller keeps ownership.
int cli_dispatch_line(cli_ctx_t *ctx, char *line);

// Dispatch pre-parsed argv (no parsing, no expansion, no prompt). Used
// by `run` and `repeat` internally so they can invoke sub-commands
// without going back through the raw line path.
int cli_dispatch_argv(cli_ctx_t *ctx, int argc, char **argv);

// Multi-line capture (heredoc): every subsequent line goes to `cb`
// instead of the command dispatcher, until the callback (or another
// path) calls cli_capture_end(). Only one capture per ctx.
void cli_capture_begin(cli_ctx_t *ctx, cli_capture_fn cb);
void cli_capture_end  (cli_ctx_t *ctx);
bool cli_is_capturing (const cli_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_CLI_H
