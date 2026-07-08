// `script save <name>`            — enter heredoc capture until ".end"
// `script fetch <name> <url>`     — HTTP GET into flash
// `script list`                    — list stored scripts
// `script show <name>`             — dump body to the terminal
// `script rm <name>`               — delete
//
// Save vs fetch:
//   - save is convenient over BLE alone; the shell buffers each typed
//     line until you send a bare ".end", then writes the accumulated
//     body to flash. Prompt changes from "> " to ": " during capture.
//   - fetch pulls the body over HTTP (using the same http_get client
//     the OTA path uses) — better for larger scripts written in an
//     editor.
#include "cli/cli.h"
#include "cli/command.h"
#include "network/http_get.h"
#include "network/wifi.h"
#include "storage/scripts.h"

#include <stdio.h>
#include <string.h>

// ---- shared capture state --------------------------------------------------
//
// One heredoc capture at a time, across every cli_ctx_t. If a second
// shell tries to start one, we refuse. The buffer lives in bss so the
// shell isn't holding a big stack frame while the user types.

static char   g_cap_buf[SCRIPT_BODY_MAX + 1];
static size_t g_cap_len = 0;
static char   g_cap_name[SCRIPT_NAME_MAX + 1];
static bool   g_cap_owner_taken = false;

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void capture_line_cb(cli_ctx_t *ctx, const char *line) {
    const char *trim = skip_ws(line);
    if (strcmp(trim, ".end") == 0) {
        bool ok = scripts_save(g_cap_name, (const uint8_t *)g_cap_buf, g_cap_len);
        if (ok) {
            cli_printf(ctx, "script: saved '%s' (%u bytes)\r\n",
                       g_cap_name, (unsigned)g_cap_len);
        } else {
            cli_write(ctx, "script: save failed (name in use unexpectedly, no free slot, or flash error)\r\n");
        }
        cli_capture_end(ctx);
        g_cap_owner_taken = false;
        g_cap_len = 0;
        g_cap_name[0] = '\0';
        return;
    }
    if (strcmp(trim, ".abort") == 0) {
        cli_write(ctx, "script: capture aborted\r\n");
        cli_capture_end(ctx);
        g_cap_owner_taken = false;
        g_cap_len = 0;
        g_cap_name[0] = '\0';
        return;
    }
    size_t ll = strlen(line);
    if (g_cap_len + ll + 1 > SCRIPT_BODY_MAX) {
        cli_write(ctx, "script: script too large, aborting\r\n");
        cli_capture_end(ctx);
        g_cap_owner_taken = false;
        g_cap_len = 0;
        g_cap_name[0] = '\0';
        return;
    }
    memcpy(&g_cap_buf[g_cap_len], line, ll);
    g_cap_len += ll;
    g_cap_buf[g_cap_len++] = '\n';
}

static int cmd_save(cli_ctx_t *ctx, const char *name) {
    if (g_cap_owner_taken) {
        cli_write(ctx, "script: a save is already in progress on another shell\r\n");
        return CLI_ERR_ARG;
    }
    if (strlen(name) > SCRIPT_NAME_MAX) {
        cli_printf(ctx, "script: name too long (max %d chars)\r\n", SCRIPT_NAME_MAX);
        return CLI_ERR_ARG;
    }
    g_cap_owner_taken = true;
    g_cap_len = 0;
    strncpy(g_cap_name, name, SCRIPT_NAME_MAX);
    g_cap_name[SCRIPT_NAME_MAX] = '\0';

    cli_printf(ctx,
        "script: entering capture — end with a lone '.end' line "
        "(or '.abort' to discard)\r\n");
    cli_capture_begin(ctx, capture_line_cb);
    return CLI_OK;
}

// ---- list / show / rm ------------------------------------------------------

static void list_cb(int slot, const char *name, size_t body_len, void *user) {
    cli_ctx_t *ctx = user;
    cli_printf(ctx, "  [%2d] %-31s  %4u B\r\n",
               slot, name, (unsigned)body_len);
}

static int cmd_list(cli_ctx_t *ctx) {
    if (scripts_count() == 0) {
        cli_write(ctx, "(no scripts stored)\r\n");
        return CLI_OK;
    }
    scripts_iter(list_cb, ctx);
    return CLI_OK;
}

static int cmd_show(cli_ctx_t *ctx, const char *name) {
    const uint8_t *body;
    size_t len;
    if (!scripts_open(name, &body, &len)) {
        cli_printf(ctx, "script: no such script or CRC failure: %s\r\n", name);
        return CLI_ERR_ARG;
    }
    // Dump line by line so \r\n replacement is clean over BLE.
    size_t off = 0;
    while (off < len) {
        size_t end = off;
        while (end < len && body[end] != '\n') end++;
        cli_writen(ctx, (const char *)&body[off], end - off);
        cli_write(ctx, "\r\n");
        off = (end < len) ? end + 1 : end;
    }
    scripts_close();
    return CLI_OK;
}

static int cmd_rm(cli_ctx_t *ctx, const char *name) {
    if (!scripts_remove(name)) {
        cli_printf(ctx, "script: no such script: %s\r\n", name);
        return CLI_ERR_ARG;
    }
    cli_printf(ctx, "script: removed '%s'\r\n", name);
    return CLI_OK;
}

// ---- fetch (HTTP GET) ------------------------------------------------------

// Same overflow policy as the heredoc capture: if the incoming body
// exceeds SCRIPT_BODY_MAX, we drop the whole download.
typedef struct {
    cli_ctx_t *ctx;
    size_t bytes;
    bool overflow;
} fetch_state_t;

static bool fetch_chunk(const uint8_t *data, size_t len, void *user) {
    fetch_state_t *s = user;
    if (s->bytes + len > SCRIPT_BODY_MAX) {
        s->overflow = true;
        return false;
    }
    memcpy(&g_cap_buf[s->bytes], data, len);
    s->bytes += len;
    return true;
}

static int cmd_fetch(cli_ctx_t *ctx, const char *name, const char *url) {
    if (!wifi_is_connected()) {
        cli_write(ctx, "script: Wi-Fi not connected. Run `wifi connect ...` first.\r\n");
        return CLI_ERR_HARDWARE;
    }
    if (strlen(name) > SCRIPT_NAME_MAX) {
        cli_printf(ctx, "script: name too long (max %d chars)\r\n", SCRIPT_NAME_MAX);
        return CLI_ERR_ARG;
    }
    if (g_cap_owner_taken) {
        cli_write(ctx, "script: a save is in progress on another shell — try again after it finishes\r\n");
        return CLI_ERR_ARG;
    }

    cli_printf(ctx, "script: fetching %s\r\n", url);
    fetch_state_t st = { .ctx = ctx };
    size_t total = 0;
    // g_cap_buf is our scratch area — no other command should touch it
    // while this runs. save capture check above guarantees exclusivity.
    http_result_t hr = http_get(url, fetch_chunk, &st, 30000, &total);
    if (hr != HTTP_OK) {
        cli_printf(ctx, "script: fetch failed (http=%d)%s\r\n",
                   (int)hr, st.overflow ? " (body too large)" : "");
        return CLI_ERR_HARDWARE;
    }
    if (!scripts_save(name, (const uint8_t *)g_cap_buf, st.bytes)) {
        cli_write(ctx, "script: save failed (no free slot or flash error)\r\n");
        return CLI_ERR_HARDWARE;
    }
    cli_printf(ctx, "script: saved '%s' (%u bytes)\r\n", name, (unsigned)st.bytes);
    return CLI_OK;
}

// ---- dispatch --------------------------------------------------------------

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;

    if (strcmp(argv[1], "save") == 0) {
        if (argc < 3) return CLI_ERR_USAGE;
        return cmd_save(ctx, argv[2]);
    }
    if (strcmp(argv[1], "list") == 0) return cmd_list(ctx);
    if (strcmp(argv[1], "show") == 0) {
        if (argc < 3) return CLI_ERR_USAGE;
        return cmd_show(ctx, argv[2]);
    }
    if (strcmp(argv[1], "rm") == 0) {
        if (argc < 3) return CLI_ERR_USAGE;
        return cmd_rm(ctx, argv[2]);
    }
    if (strcmp(argv[1], "fetch") == 0) {
        if (argc < 4) return CLI_ERR_USAGE;
        return cmd_fetch(ctx, argv[2], argv[3]);
    }
    cli_printf(ctx, "script: unknown subcommand: %s\r\n", argv[1]);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(script,
    "Store / fetch / list shell scripts in flash",
    "script save <name> | fetch <name> <url> | list | show <name> | rm <name>",
    handle);
