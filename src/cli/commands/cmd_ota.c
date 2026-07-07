// `ota status`   — inspect the OTA subsystem.
// `ota download <url> [sha256]` — fetch an image over HTTP into slot B,
//                  verifying against the optional expected digest.
// `ota abort`    — discard an in-flight update and free staging state.
// `ota update`   — apply a staged image (needs bootloader; not yet built).
#include "cli/cli.h"
#include "cli/command.h"
#include "ota/ota.h"
#include "network/http_get.h"
#include "network/wifi.h"
#include "system/sha256.h"

#include <string.h>

static const char *slot_name(ota_slot_t s) {
    return s == OTA_SLOT_A ? "A" : "B";
}

static int cmd_status(cli_ctx_t *ctx) {
    ota_status_t st = ota_status();
    cli_write(ctx, "ota status\r\n");
    cli_printf(ctx, "  active slot     : %s\r\n", slot_name(st.active_slot));
    cli_printf(ctx, "  staging slot    : %s\r\n", slot_name(st.staging_slot));
    cli_printf(ctx, "  update running  : %s\r\n",
               st.update_in_progress ? "yes" : "no");
    cli_printf(ctx, "  pending verify  : %s\r\n",
               st.pending_verify ? "yes" : "no");
    if (st.update_in_progress) {
        cli_printf(ctx, "  progress        : %u / %u bytes\r\n",
                   (unsigned)st.bytes_written, (unsigned)st.staging_size);
    }
    if (st.image_ready) {
        char hex[65];
        sha256_format_hex(st.computed_sha256, hex);
        cli_printf(ctx, "  staged size     : %u bytes\r\n",
                   (unsigned)st.bytes_written);
        cli_printf(ctx, "  staged sha256   : %s\r\n", hex);
    }
    return CLI_OK;
}

// --- download path ---------------------------------------------------------

typedef struct {
    cli_ctx_t *ctx;
    size_t bytes;
    size_t last_report;
    bool   error;
} dl_state_t;

// Fires from LwIP callback context — keep it short. Flash writes here
// are safe: flash_safe_execute coordinates with cyw43-driver even when
// invoked from the async_context worker LwIP runs under.
static bool on_chunk(const uint8_t *data, size_t len, void *user) {
    dl_state_t *s = user;
    ota_result_t r = ota_write(s->bytes, data, len);
    if (r != OTA_OK) {
        s->error = true;
        return false;
    }
    s->bytes += len;
    // Progress every 32 KB. cli_printf → ble_nus_send just queues bytes,
    // safe to call from this context.
    if (s->bytes - s->last_report >= 32u * 1024u) {
        cli_printf(s->ctx, "  ..%u KB\r\n", (unsigned)(s->bytes / 1024u));
        s->last_report = s->bytes;
    }
    return true;
}

static int cmd_download(cli_ctx_t *ctx, const char *url, const char *sha_hex) {
    uint8_t expected[SHA256_DIGEST_LEN];
    bool have_expected = false;
    if (sha_hex && *sha_hex) {
        if (!sha256_parse_hex(sha_hex, expected)) {
            cli_write(ctx, "ota: sha256 must be exactly 64 hex chars\r\n");
            return CLI_ERR_ARG;
        }
        have_expected = true;
    }

    if (!wifi_is_connected()) {
        cli_write(ctx, "ota: Wi-Fi not connected. Run `wifi connect ...` first.\r\n");
        return CLI_ERR_HARDWARE;
    }

    cli_write(ctx, "ota: erasing staging slot (~8s, BLE may stall)...\r\n");
    ota_result_t r = ota_begin(0);
    if (r != OTA_OK) {
        cli_printf(ctx, "ota: begin failed (%d)\r\n", (int)r);
        return CLI_ERR_HARDWARE;
    }

    cli_printf(ctx, "ota: fetching %s\r\n", url);
    dl_state_t st = { .ctx = ctx };
    size_t total = 0;
    http_result_t hr = http_get(url, on_chunk, &st, 120000, &total);
    if (hr != HTTP_OK || st.error) {
        cli_printf(ctx, "ota: download failed (http=%d, ota=%s)\r\n",
                   (int)hr, st.error ? "flash-write" : "ok");
        ota_abort();
        return CLI_ERR_HARDWARE;
    }

    r = ota_finalize();
    if (r != OTA_OK) {
        cli_printf(ctx, "ota: finalize failed (%d)\r\n", (int)r);
        return CLI_ERR_HARDWARE;
    }

    ota_status_t s = ota_status();
    char hex[65];
    sha256_format_hex(s.computed_sha256, hex);
    cli_printf(ctx, "ota: %u bytes staged\r\n", (unsigned)s.bytes_written);
    cli_printf(ctx, "     sha256 %s\r\n", hex);

    if (have_expected) {
        if (ota_verify(expected) == OTA_OK) {
            cli_write(ctx, "     verified against expected digest\r\n");
        } else {
            cli_write(ctx, "     DIGEST MISMATCH — image will not be applied\r\n");
            return CLI_ERR_HARDWARE;
        }
    } else {
        cli_write(ctx, "     no expected digest given — verification skipped\r\n");
    }

    cli_write(ctx, "ota: staged. `ota update` requires the Phase-2 bootloader.\r\n");
    return CLI_OK;
}

// --- dispatch ---------------------------------------------------------------

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;

    if (strcmp(argv[1], "status") == 0) return cmd_status(ctx);

    if (strcmp(argv[1], "download") == 0) {
        if (argc < 3) return CLI_ERR_USAGE;
        return cmd_download(ctx, argv[2], argc >= 4 ? argv[3] : NULL);
    }

    if (strcmp(argv[1], "abort") == 0) {
        ota_abort();
        cli_write(ctx, "ota: aborted\r\n");
        return CLI_OK;
    }

    if (strcmp(argv[1], "check") == 0 || strcmp(argv[1], "update") == 0) {
        cli_printf(ctx, "ota %s: needs the Phase-2 bootloader (not yet built)\r\n",
                   argv[1]);
        return CLI_ERR_UNSUPPORTED;
    }

    cli_printf(ctx, "ota: unknown subcommand: %s\r\n", argv[1]);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(ota,
    "OTA download / status (apply needs bootloader)",
    "ota status | download <url> [sha256] | abort",
    handle);
