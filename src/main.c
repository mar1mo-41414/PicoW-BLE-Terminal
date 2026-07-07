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

#include "btstack.h"

#include "cli/cli.h"
#include "ble/ble_nus.h"
#include "ota/ota.h"
#include "system/log.h"

#include <stdio.h>
#include <string.h>

// ---- shells -----------------------------------------------------------------

static cli_ctx_t g_ble_ctx;
static cli_ctx_t g_usb_ctx;

static int usb_cli_write(const char *data, size_t len, void *user) {
    (void)user;
    if (len == 0) return 0;
    fwrite(data, 1, len, stdout);
    fflush(stdout);
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

// ---- entry point -----------------------------------------------------------

int main(void) {
    stdio_init_all();

    log_add_sink(&g_usb_log_sink);
    log_set_level(LOG_LEVEL_INFO);
    LOGI("boot: PicoBLE-Terminal");

    cli_init(&g_ble_ctx, ble_nus_cli_write, NULL);
    cli_init(&g_usb_ctx, usb_cli_write,     NULL);

    ota_init();

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

    btstack_run_loop_execute();
    return 0;  // unreachable
}
