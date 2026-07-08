// CLI transport-agnostic shell loop.
#include "cli/cli.h"
#include "cli/command.h"
#include "cli/parser.h"
#include "system/version.h"

#include <stdio.h>
#include <string.h>

// ---- output helpers --------------------------------------------------------

int cli_writen(cli_ctx_t *ctx, const char *s, size_t n) {
    if (!ctx || !ctx->out || !s || n == 0) return 0;
    return ctx->out(s, n, ctx->user);
}

int cli_write(cli_ctx_t *ctx, const char *s) {
    return s ? cli_writen(ctx, s, strlen(s)) : 0;
}

int cli_vprintf(cli_ctx_t *ctx, const char *fmt, va_list ap) {
    // 256 bytes covers every message this firmware currently emits; the
    // shell truncates longer output rather than allocate, since we're
    // running on a 264KB SRAM device with no heap policy yet.
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n <= 0) return 0;
    size_t out = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    return cli_writen(ctx, buf, out);
}

int cli_printf(cli_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cli_vprintf(ctx, fmt, ap);
    va_end(ap);
    return r;
}

// ---- lifecycle -------------------------------------------------------------

void cli_init(cli_ctx_t *ctx, cli_output_fn out, void *user) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->out = out;
    ctx->user = user;
}

void cli_reset(cli_ctx_t *ctx) {
    if (!ctx) return;
    ctx->line_len = 0;
    ctx->last_was_cr = 0;
}

static void write_prompt(cli_ctx_t *ctx) {
    cli_write(ctx, "> ");
}

void cli_greet(cli_ctx_t *ctx) {
    cli_reset(ctx);
    cli_printf(ctx, "\r\nPicoBLE Terminal %s  [slot %c]\r\n",
               PICOBLE_FW_VERSION, 'A' + (PICO_OTA_SLOT & 1));
    cli_write(ctx, "Type \"help\"\r\n\r\n");
    write_prompt(ctx);
}

// ---- line assembly ---------------------------------------------------------

static void dispatch(cli_ctx_t *ctx) {
    // Skip prompt spacing after a blank line — matches how most shells feel.
    if (ctx->line_len == 0) {
        write_prompt(ctx);
        return;
    }
    ctx->line[ctx->line_len] = '\0';

    char *argv[CLI_MAX_ARGS];
    int argc = cli_parse(ctx->line, argv, CLI_MAX_ARGS);

    if (argc == CLI_PARSE_ERR_UNCLOSED_QUOTE) {
        cli_write(ctx, "error: unclosed quote\r\n");
    } else if (argc == CLI_PARSE_ERR_TOO_MANY_ARGS) {
        cli_printf(ctx, "error: too many arguments (max %d)\r\n", CLI_MAX_ARGS);
    } else if (argc == 0) {
        // pure whitespace — nothing to do
    } else {
        const cli_command_t *cmd = cli_command_find(argv[0]);
        if (!cmd) {
            cli_printf(ctx, "%s: command not found\r\n", argv[0]);
        } else {
            int rc = cmd->handler(argc, argv, ctx);
            if (rc == CLI_ERR_USAGE) cli_usage(ctx, cmd);
        }
    }

    ctx->line_len = 0;
    write_prompt(ctx);
}

void cli_feed_line(cli_ctx_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx) return;
    cli_feed(ctx, data, len);
    // If the packet already contained a terminator, line_len is 0 here
    // and this LF becomes an empty line (skipped in dispatch). If it did
    // not, this synthesizes the missing line end.
    if (ctx->line_len > 0) {
        static const uint8_t lf = '\n';
        cli_feed(ctx, &lf, 1);
    }
}

void cli_feed(cli_ctx_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data) return;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        // CR or LF: end-of-line. Swallow the LF that follows a CR so CRLF
        // fires exactly once. Bare CR and bare LF both work.
        if (b == '\n') {
            if (ctx->last_was_cr) {
                ctx->last_was_cr = 0;
                continue;
            }
            dispatch(ctx);
            continue;
        }
        if (b == '\r') {
            ctx->last_was_cr = 1;
            dispatch(ctx);
            continue;
        }
        ctx->last_was_cr = 0;

        // Interactive editing: honour BS/DEL if a client (or the user
        // typing over USB CDC) sends them. We don't echo — the terminal
        // client is expected to echo locally, which is the norm for BLE
        // UART tooling.
        if (b == 0x08 || b == 0x7F) {
            if (ctx->line_len > 0) ctx->line_len--;
            continue;
        }

        // Drop any other control byte silently to avoid odd behaviour
        // from stray escape sequences arriving over BLE.
        if (b < 0x20) continue;

        if (ctx->line_len < CLI_LINE_MAX - 1) {
            ctx->line[ctx->line_len++] = (char)b;
        }
        // Silent overflow: characters past the limit are dropped. This is
        // intentional — a truncated command will fail to match and print
        // "command not found", which is safer than accepting a garbled
        // partial line.
    }
}
