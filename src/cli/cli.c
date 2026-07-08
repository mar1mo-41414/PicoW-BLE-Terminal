// CLI transport-agnostic shell loop.
#include "cli/cli.h"
#include "cli/command.h"
#include "cli/parser.h"
#include "system/vars.h"
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
    // 256 bytes covers every message this firmware currently emits.
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
    ctx->line_len   = 0;
    ctx->last_was_cr = 0;
    ctx->capture_cb  = NULL;
}

static void write_prompt(cli_ctx_t *ctx) {
    cli_write(ctx, ctx->capture_cb ? ": " : "> ");
}

void cli_greet(cli_ctx_t *ctx) {
    cli_reset(ctx);
    cli_printf(ctx, "\r\nPicoBLE Terminal %s  [slot %c]\r\n",
               PICOBLE_FW_VERSION, 'A' + (PICO_OTA_SLOT & 1));
    cli_write(ctx, "Type \"help\"\r\n\r\n");
    write_prompt(ctx);
}

// ---- capture mode ----------------------------------------------------------

void cli_capture_begin(cli_ctx_t *ctx, cli_capture_fn cb) {
    if (!ctx) return;
    ctx->capture_cb = cb;
}
void cli_capture_end(cli_ctx_t *ctx) {
    if (!ctx) return;
    ctx->capture_cb = NULL;
}
bool cli_is_capturing(const cli_ctx_t *ctx) {
    return ctx && ctx->capture_cb;
}

// ---- variable expansion ----------------------------------------------------
//
// Post-tokenization: each argv[i] is walked and $NAME / ${NAME}
// occurrences are substituted with their value. If the referenced
// variable is unset the substitution is empty. Only a single token per
// argv is produced — no word splitting — so `set X="a b"; foo $X` runs
// as `foo "a b"` (one arg), which matches how our quoted parser would
// have treated a literal `foo "a b"`.

#define CLI_EXPAND_BUF_SIZE 512
static char g_expand_buf[CLI_EXPAND_BUF_SIZE];

static bool expand_argv(char **argv, int argc) {
    size_t off = 0;
    for (int i = 0; i < argc; i++) {
        char *tok = &g_expand_buf[off];
        const char *src = argv[i];
        while (*src) {
            if (*src == '$') {
                const char *s = src + 1;
                bool braced = (*s == '{');
                if (braced) s++;
                if (!vars_name_char_first(*s)) {
                    // Bare '$' or invalid — treat literally.
                    if (off >= CLI_EXPAND_BUF_SIZE - 1) return false;
                    g_expand_buf[off++] = '$';
                    src++;
                    continue;
                }
                const char *name_start = s;
                while (vars_name_char_rest(*s)) s++;
                size_t name_len = (size_t)(s - name_start);
                if (name_len > VAR_NAME_MAX) return false;

                char name[VAR_NAME_MAX + 1];
                memcpy(name, name_start, name_len);
                name[name_len] = '\0';
                if (braced && *s == '}') s++;

                const char *val = vars_get(name);
                if (val) {
                    size_t vl = strlen(val);
                    if (off + vl >= CLI_EXPAND_BUF_SIZE) return false;
                    memcpy(&g_expand_buf[off], val, vl);
                    off += vl;
                }
                src = s;
            } else {
                if (off >= CLI_EXPAND_BUF_SIZE - 1) return false;
                g_expand_buf[off++] = *src++;
            }
        }
        if (off >= CLI_EXPAND_BUF_SIZE) return false;
        g_expand_buf[off++] = '\0';
        argv[i] = tok;
    }
    return true;
}

// ---- dispatch --------------------------------------------------------------

int cli_dispatch_argv(cli_ctx_t *ctx, int argc, char **argv) {
    if (argc == 0) return 0;
    const cli_command_t *cmd = cli_command_find(argv[0]);
    if (!cmd) {
        cli_printf(ctx, "%s: command not found\r\n", argv[0]);
        return CLI_ERR_ARG;
    }
    int rc = cmd->handler(argc, argv, ctx);
    if (rc == CLI_ERR_USAGE) cli_usage(ctx, cmd);
    return rc;
}

int cli_dispatch_line(cli_ctx_t *ctx, char *line) {
    if (!ctx || !line) return CLI_ERR_ARG;
    if (line[0] == '\0') return 0;

    char *argv[CLI_MAX_ARGS];
    int argc = cli_parse(line, argv, CLI_MAX_ARGS);

    if (argc == CLI_PARSE_ERR_UNCLOSED_QUOTE) {
        cli_write(ctx, "error: unclosed quote\r\n");
        return CLI_ERR_ARG;
    }
    if (argc == CLI_PARSE_ERR_TOO_MANY_ARGS) {
        cli_printf(ctx, "error: too many arguments (max %d)\r\n", CLI_MAX_ARGS);
        return CLI_ERR_ARG;
    }
    if (argc == 0) return 0;

    if (!expand_argv(argv, argc)) {
        cli_write(ctx, "error: variable expansion overflowed\r\n");
        return CLI_ERR_ARG;
    }

    return cli_dispatch_argv(ctx, argc, argv);
}

// ---- line assembly & feed loop --------------------------------------------

static void end_of_line(cli_ctx_t *ctx) {
    ctx->line[ctx->line_len] = '\0';

    // Capture mode short-circuits normal dispatch: every line — even
    // an empty one — goes verbatim to the capture callback, which
    // decides when to end capture by calling cli_capture_end().
    if (ctx->capture_cb) {
        cli_capture_fn cb = ctx->capture_cb;
        cb(ctx, ctx->line);
    } else if (ctx->line_len > 0) {
        cli_dispatch_line(ctx, ctx->line);
    }

    ctx->line_len = 0;
    write_prompt(ctx);
}

void cli_feed_line(cli_ctx_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx) return;
    cli_feed(ctx, data, len);
    if (ctx->line_len > 0) {
        static const uint8_t lf = '\n';
        cli_feed(ctx, &lf, 1);
    }
}

void cli_feed(cli_ctx_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data) return;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        if (b == '\n') {
            if (ctx->last_was_cr) { ctx->last_was_cr = 0; continue; }
            end_of_line(ctx);
            continue;
        }
        if (b == '\r') {
            ctx->last_was_cr = 1;
            end_of_line(ctx);
            continue;
        }
        ctx->last_was_cr = 0;

        if (b == 0x08 || b == 0x7F) {
            if (ctx->line_len > 0) ctx->line_len--;
            continue;
        }
        if (b < 0x20) continue;

        if (ctx->line_len < CLI_LINE_MAX - 1) {
            ctx->line[ctx->line_len++] = (char)b;
        }
    }
}
