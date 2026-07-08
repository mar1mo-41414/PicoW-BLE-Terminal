// PicoBLE-Terminal — top-level wiring.
//
// Boot sequence:
//   1. stdio_init_all()           USB CDC comes up so log messages have a
//                                 destination even if BLE fails.
//   2. log_add_sink(usb)          route LOGI/LOGE to the serial console.
//   3. cli_init(x2)               one shell for BLE, one for USB CDC —
//                                 independent line buffers.
//   4. ota_init()                 populates ota_status().
//   5. ble_nus_init()              starts advertising once HCI reaches
//                                 the working state.
//   6. btstack_run_loop_execute() takes over forever. USB CDC is polled
//                                 from a BTStack timer so the whole
//                                 firmware pumps off one event loop.
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "btstack.h"

#include "cli/cli.h"
#include "ble/ble_nus.h"
#include "network/wifi.h"
#include "pico_ota/ota.h"
#include "pico_ota/log.h"
#include "storage/config.h"
#include "system/bindings.h"
#include "system/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ---- shells -----------------------------------------------------------------

static cli_ctx_t g_ble_ctx;
static cli_ctx_t g_usb_ctx;
static cli_ctx_t g_bg_ctx;      // background ctx: silent output, used by bindings

static int usb_cli_write(const char *data, size_t len, void *user) {
    (void)user;
    if (len == 0) return 0;
    fwrite(data, 1, len, stdout);
    fflush(stdout);
    return 0;
}

// Sink that swallows output. Bindings execute asynchronously so their
// command output would smear whatever the user is currently typing at
// the BLE prompt. Any interesting side-effects show up in the log sink
// (USB CDC) rather than being lost entirely.
static int null_cli_write(const char *data, size_t len, void *user) {
    (void)data; (void)len; (void)user;
    return 0;
}

// ---- log sink ---------------------------------------------------------------
//
// Prefix log lines with a level letter and drop the payload verbatim.
// Logs go only to USB by default — the BLE shell prompt would smear if
// asynchronous log lines poured out over notifications while the user is
// typing. Adding a BLE log sink later is a one-liner in main().

static void usb_log_sink_write(log_level_t lvl, const char *msg, size_t len,
                               void *user) {
    (void)user;
    const char *tag = (lvl == LOG_LEVEL_ERROR) ? "E"
                    : (lvl == LOG_LEVEL_WARN)  ? "W"
                    : (lvl == LOG_LEVEL_INFO)  ? "I"
                                               : "D";
    printf("[%s] %.*s\n", tag, (int)len, msg);
    fflush(stdout);
}

static log_sink_t g_usb_log_sink = {
    .write = usb_log_sink_write,
    .user  = NULL,
    ._next = NULL,
};

// --- pico-ota log adapter ---------------------------------------------------
//
// The framework declares pico_ota_log_{info,warn,error} as weak. These
// strong definitions route them into this app's log subsystem so we get
// consistent per-level formatting on USB CDC without the framework
// needing to know anything about it.

static void ota_log_route(log_level_t lvl, const char *fmt, va_list ap) {
    log_vwrite(lvl, fmt, ap);
}

void pico_ota_log_info(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); ota_log_route(LOG_LEVEL_INFO,  fmt, ap); va_end(ap);
}
void pico_ota_log_warn(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); ota_log_route(LOG_LEVEL_WARN,  fmt, ap); va_end(ap);
}
void pico_ota_log_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); ota_log_route(LOG_LEVEL_ERROR, fmt, ap); va_end(ap);
}

// ---- BLE callbacks ---------------------------------------------------------

static void on_ble_rx(const uint8_t *data, size_t n, void *user) {
    (void)user;
    // BLE UART clients like Bluefruit Connect and nRF Toolbox often send
    // each "Send" as one write with no trailing newline. Treat the packet
    // boundary as an implicit line end so the shell dispatches regardless
    // of the client's EOL setting.
    cli_feed_line(&g_ble_ctx, data, n);
}

static void on_ble_connect(void *user) {
    (void)user;
    cli_greet(&g_ble_ctx);
}

static void on_ble_disconnect(void *user) {
    (void)user;
    cli_reset(&g_ble_ctx);
}

// ---- USB stdin poll --------------------------------------------------------

static btstack_timer_source_t g_stdio_timer;
static bool                   g_usb_greeted = false;

// Polling instead of stdio callbacks keeps the transport pluggable: on
// current SDKs, getchar_timeout_us(0) returns PICO_ERROR_TIMEOUT (< 0)
// when there is no host or no input.
static void stdio_poll(btstack_timer_source_t *ts) {
    for (;;) {
        int c = getchar_timeout_us(0);
        if (c < 0) break;

        if (!g_usb_greeted) {
            g_usb_greeted = true;
            cli_greet(&g_usb_ctx);
        }
        uint8_t b = (uint8_t)c;
        cli_feed(&g_usb_ctx, &b, 1);
    }
    btstack_run_loop_set_timer(ts, 10);
    btstack_run_loop_add_timer(ts);
}

// --- Wi-Fi background poll --------------------------------------------------
//
// pico_cyw43_arch_lwip_poll only advances the network stack when
// cyw43_arch_poll() is called. During long idle periods we pump it
// from a BTStack timer so Wi-Fi keeps working even if no CLI command
// is currently spinning its own wait loop.

static btstack_timer_source_t g_net_poll_timer;

static void net_poll_expired(btstack_timer_source_t *ts) {
    cyw43_arch_poll();
    btstack_run_loop_set_timer(ts, 5);
    btstack_run_loop_add_timer(ts);
}

// --- OTA auto-confirm --------------------------------------------------------
//
// The bootloader marks a freshly applied image as pending_verify. If
// we survive 30 s of runtime without a crash, we call ota_confirm()
// so the bootloader stops incrementing boot_attempts. Users can also
// invoke `ota confirm` manually at any point.

static btstack_timer_source_t g_ota_confirm_timer;

static void ota_auto_confirm(btstack_timer_source_t *ts) {
    (void)ts;
    ota_confirm();
}

// --- GPIO bindings dispatcher -----------------------------------------------
//
// Bindings fire from GPIO IRQ context, but their command dispatch has
// to happen on the main thread (LwIP + BTStack + flash writes aren't
// IRQ-safe). This 20 ms timer drains the pending flags.

static btstack_timer_source_t g_bindings_timer;

static void bindings_timer_expired(btstack_timer_source_t *ts) {
    bindings_dispatch_pending();
    btstack_run_loop_set_timer(ts, 20);
    btstack_run_loop_add_timer(ts);
}

// ---- Wi-Fi auto-connect ----------------------------------------------------
//
// Fires a couple of seconds after boot so BTStack has already brought
// BLE advertising up. Prefers flash-stored credentials; falls back to
// WIFI_SSID / WIFI_PASSWORD compiled in via -D flags if nothing's
// saved. If neither is present the user does it manually with
// `wifi connect ...`.

static btstack_timer_source_t g_wifi_autoconnect_timer;

static void wifi_autoconnect_expired(btstack_timer_source_t *ts) {
    (void)ts;

    wifi_creds_t creds;
    bool have = false;

    if (config_load_wifi(&creds)) {
        LOGI("boot: auto-connect (flash) to %s", creds.ssid);
        have = true;
    }
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    else {
        memset(&creds, 0, sizeof(creds));
        strncpy(creds.ssid, WIFI_SSID,     CONFIG_SSID_MAXLEN);
        strncpy(creds.psk,  WIFI_PASSWORD, CONFIG_PSK_MAXLEN);
        LOGI("boot: auto-connect (build-time) to %s", creds.ssid);
        have = true;
    }
#endif

    if (!have) {
        LOGI("boot: no wifi creds — run `wifi connect <ssid> <psk>`");
        return;
    }

    wifi_init();
    // Blocks up to 15 s. BTStack keeps running from its async_context
    // worker, so BLE stays advertising and any active connection stays
    // up while the CYW43 joins the AP.
    wifi_connect(creds.ssid, creds.psk, 15000);
}

// ---- entry point -----------------------------------------------------------

int main(void) {
    stdio_init_all();

    log_add_sink(&g_usb_log_sink);
    log_set_level(LOG_LEVEL_INFO);
    LOGI("boot: PicoBLE-Terminal");

    cli_init(&g_ble_ctx, ble_nus_cli_write, NULL);
    cli_init(&g_usb_ctx, usb_cli_write,     NULL);
    cli_init(&g_bg_ctx,  null_cli_write,    NULL);

    ota_init();
    bindings_init(&g_bg_ctx);

    ble_nus_set_rx_callback(on_ble_rx, NULL);
    ble_nus_set_connect_callback(on_ble_connect, NULL);
    ble_nus_set_disconnect_callback(on_ble_disconnect, NULL);
    if (ble_nus_init() != 0) {
        LOGE("boot: BLE init failed — USB CDC shell only");
    }

    // USB CDC poll driven from BTStack's own timer so we don't need a
    // second event loop.
    g_stdio_timer.process = stdio_poll;
    btstack_run_loop_set_timer(&g_stdio_timer, 10);
    btstack_run_loop_add_timer(&g_stdio_timer);

    // Wi-Fi (poll mode) idle pump: 5 ms cadence so incoming packets are
    // processed even without an active CLI wait loop.
    g_net_poll_timer.process = net_poll_expired;
    btstack_run_loop_set_timer(&g_net_poll_timer, 5);
    btstack_run_loop_add_timer(&g_net_poll_timer);

    // Kick off Wi-Fi auto-connect 2 s after boot — by then BTStack is
    // through HCI bring-up and BLE is advertising, so the join blocking
    // doesn't stall discovery.
    g_wifi_autoconnect_timer.process = wifi_autoconnect_expired;
    btstack_run_loop_set_timer(&g_wifi_autoconnect_timer, 2000);
    btstack_run_loop_add_timer(&g_wifi_autoconnect_timer);

    // OTA auto-confirm 30 s after boot.
    g_ota_confirm_timer.process = ota_auto_confirm;
    btstack_run_loop_set_timer(&g_ota_confirm_timer, 30000);
    btstack_run_loop_add_timer(&g_ota_confirm_timer);

    // GPIO bindings pump — 20 ms cadence.
    g_bindings_timer.process = bindings_timer_expired;
    btstack_run_loop_set_timer(&g_bindings_timer, 20);
    btstack_run_loop_add_timer(&g_bindings_timer);

    btstack_run_loop_execute();
    return 0;  // unreachable
}
