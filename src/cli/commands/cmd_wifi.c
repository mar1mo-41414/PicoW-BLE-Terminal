// `wifi status`             — current link + IP
// `wifi connect <ssid> <psk>` — join and persist credentials to flash
// `wifi forget`             — wipe stored credentials
//
// The connect path is synchronous — the shell blocks until the join
// completes or times out (~15 s). BLE stays connected during this;
// coexistence is handled by pico_cyw43_arch.
#include "cli/cli.h"
#include "cli/command.h"
#include "network/wifi.h"
#include "storage/config.h"

#include <string.h>

static int show_status(cli_ctx_t *ctx) {
    bool up = wifi_is_connected();
    cli_printf(ctx, "wifi: %s\r\n", up ? "connected" : "disconnected");
    if (up) {
        char ip[24];
        wifi_format_ip(ip, sizeof(ip));
        cli_printf(ctx, "  ip : %s\r\n", ip);
    }
    wifi_creds_t saved;
    if (config_load_wifi(&saved)) {
        cli_printf(ctx, "  saved ssid : %s\r\n", saved.ssid);
    } else {
        cli_write(ctx, "  saved ssid : (none)\r\n");
    }
    return CLI_OK;
}

static int do_connect(cli_ctx_t *ctx, const char *ssid, const char *psk) {
    cli_printf(ctx, "wifi: joining %s...\r\n", ssid);

    int rc = wifi_connect(ssid, psk, 15000);
    if (rc != 0) {
        cli_printf(ctx, "wifi: connect failed (rc=%d)\r\n", rc);
        return CLI_ERR_HARDWARE;
    }

    char ip[24];
    wifi_format_ip(ip, sizeof(ip));
    cli_printf(ctx, "wifi: connected, ip=%s\r\n", ip);

    // Persist so the next boot auto-connects without the user pasting
    // the passphrase again over BLE.
    wifi_creds_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.ssid, ssid, CONFIG_SSID_MAXLEN);
    strncpy(c.psk,  psk ? psk : "", CONFIG_PSK_MAXLEN);
    if (!config_save_wifi(&c)) {
        cli_write(ctx, "wifi: warning: failed to persist credentials\r\n");
    }
    return CLI_OK;
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;

    if (strcmp(argv[1], "status") == 0) return show_status(ctx);

    if (strcmp(argv[1], "connect") == 0) {
        if (argc < 4) return CLI_ERR_USAGE;
        return do_connect(ctx, argv[2], argv[3]);
    }

    if (strcmp(argv[1], "forget") == 0) {
        if (config_clear_wifi()) {
            cli_write(ctx, "wifi: credentials cleared\r\n");
            return CLI_OK;
        }
        cli_write(ctx, "wifi: clear failed\r\n");
        return CLI_ERR_HARDWARE;
    }

    cli_printf(ctx, "wifi: unknown subcommand: %s\r\n", argv[1]);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(wifi,
    "Wi-Fi status, connect, forget",
    "wifi status | connect <ssid> <psk> | forget",
    handle);
