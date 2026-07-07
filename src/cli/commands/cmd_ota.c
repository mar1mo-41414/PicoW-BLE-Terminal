// `ota status`                        — inspect the OTA subsystem.
// `ota download <url> [sha256]`       — fetch an image into the staging slot.
// `ota apply` (alias `ota update`)     — reboot into the staged slot.
// `ota confirm`                        — clear pending_verify for this boot.
// `ota abort`                          — discard an in-flight update.
#include "cli/cli.h"
#include "cli/command.h"
#include "ota/ota.h"
#include "network/http_get.h"
#include "network/wifi.h"
#include "storage/config.h"
#include "system/sha256.h"

#include <string.h>

static char slot_letter(uint8_t s) { return 'A' + (s & 1u); }

static int cmd_status(cli_ctx_t *ctx) {
    ota_status_t st = ota_status();
    cli_write(ctx, "ota status\r\n");
    cli_printf(ctx, "  active slot     : %c\r\n", slot_letter(st.active_slot));
    cli_printf(ctx, "  staging slot    : %c\r\n", slot_letter(st.staging_slot));
    cli_printf(ctx, "  pending verify  : %s (boot attempts=%u)\r\n",
               st.pending_verify ? "yes" : "no",
               (unsigned)st.boot_attempts);
    cli_printf(ctx, "  update running  : %s\r\n",
               st.update_in_progress ? "yes" : "no");
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

static bool on_chunk(const uint8_t *data, size_t len, void *user) {
    dl_state_t *s = user;
    ota_result_t r = ota_write(s->bytes, data, len);
    if (r != OTA_OK) {
        s->error = true;
        return false;
    }
    s->bytes += len;
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

    ota_status_t s0 = ota_status();
    cli_printf(ctx, "ota: erasing slot %c staging area (~8s, BLE may stall)...\r\n",
               slot_letter(s0.staging_slot));
    ota_result_t r = ota_begin(0);
    if (r != OTA_OK) {
        cli_printf(ctx, "ota: begin failed (%d)\r\n", (int)r);
        return CLI_ERR_HARDWARE;
    }

    if (!wifi_is_connected()) {
        cli_write(ctx, "ota: Wi-Fi dropped during erase — reconnecting...\r\n");
        wifi_creds_t creds;
        if (config_load_wifi(&creds)) {
            wifi_connect(creds.ssid, creds.psk, 15000);
        }
        if (!wifi_is_connected()) {
            cli_write(ctx, "ota: Wi-Fi did not come back; aborting\r\n");
            ota_abort();
            return CLI_ERR_HARDWARE;
        }
    }

    cli_printf(ctx, "ota: fetching %s\r\n", url);
    dl_state_t st = { .ctx = ctx };
    size_t total = 0;
    http_result_t hr = http_get(url, on_chunk, &st, 120000, &total);
    if (hr != HTTP_OK || st.error) {
        int lerr = http_get_last_lwip_err();
        cli_printf(ctx, "ota: download failed (http=%d, lwip=%d, ota=%s)\r\n",
                   (int)hr, lerr, st.error ? "flash-write" : "ok");
        if (!wifi_is_connected()) {
            cli_write(ctx, "     Wi-Fi: link now DOWN\r\n");
        }
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
    cli_printf(ctx, "ota: %u bytes staged in slot %c\r\n",
               (unsigned)s.bytes_written, slot_letter(s.staging_slot));
    cli_printf(ctx, "     sha256 %s\r\n", hex);

    if (have_expected) {
        if (ota_verify(expected) == OTA_OK) {
            cli_write(ctx, "     verified against expected digest\r\n");
        } else {
            cli_write(ctx, "     DIGEST MISMATCH — will not apply\r\n");
            return CLI_ERR_HARDWARE;
        }
    } else {
        cli_write(ctx, "     no expected digest given — verification skipped\r\n");
    }

    cli_write(ctx, "ota: staged. Run `ota apply` to reboot into it.\r\n");
    return CLI_OK;
}

// --- apply / confirm --------------------------------------------------------

static int cmd_apply(cli_ctx_t *ctx) {
    ota_status_t st = ota_status();
    if (!st.image_ready) {
        cli_write(ctx, "ota: no image staged — run `ota download` first\r\n");
        return CLI_ERR_ARG;
    }

    cli_printf(ctx,
        "ota: applying — rebooting into slot %c (on probation).\r\n"
        "     if the new image doesn't call `ota confirm` within 30 s of boot\r\n"
        "     three times in a row, the bootloader will roll back to slot %c.\r\n",
        slot_letter(st.staging_slot), slot_letter(st.active_slot));

    ota_result_t r = ota_apply();
    // If we got here at all, apply failed (successful path reboots).
    cli_printf(ctx, "ota: apply failed (%d)\r\n", (int)r);
    return CLI_ERR_HARDWARE;
}

static int cmd_confirm(cli_ctx_t *ctx) {
    ota_status_t st = ota_status();
    if (!st.pending_verify && st.boot_attempts == 0) {
        cli_write(ctx, "ota: nothing to confirm (not on probation)\r\n");
        return CLI_OK;
    }
    ota_confirm();
    cli_write(ctx, "ota: confirmed — no rollback on next boot\r\n");
    return CLI_OK;
}

// --- dispatch ---------------------------------------------------------------

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;

    if (strcmp(argv[1], "status")   == 0) return cmd_status(ctx);
    if (strcmp(argv[1], "abort")    == 0) { ota_abort(); cli_write(ctx, "ota: aborted\r\n"); return CLI_OK; }
    if (strcmp(argv[1], "confirm")  == 0) return cmd_confirm(ctx);
    if (strcmp(argv[1], "apply")    == 0
     || strcmp(argv[1], "update")   == 0) return cmd_apply(ctx);

    if (strcmp(argv[1], "download") == 0) {
        if (argc < 3) return CLI_ERR_USAGE;
        return cmd_download(ctx, argv[2], argc >= 4 ? argv[3] : NULL);
    }

    cli_printf(ctx, "ota: unknown subcommand: %s\r\n", argv[1]);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(ota,
    "OTA download / apply / confirm / status",
    "ota status | download <url> [sha256] | apply | confirm | abort",
    handle);
