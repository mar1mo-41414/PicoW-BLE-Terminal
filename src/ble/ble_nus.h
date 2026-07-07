// Nordic UART Service over BLE.
//
// One connection at a time. On connect + notification-subscribe we call
// the connect callback so the app can push a greeting into the CLI.
// Incoming writes on the RX characteristic get forwarded to the RX
// callback. Outgoing bytes (CLI responses, log lines that opt in) are
// queued via ble_nus_send() and shipped out as notifications on the
// TX characteristic.
#ifndef PICOBLE_BLE_NUS_H
#define PICOBLE_BLE_NUS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// RX = bytes arriving from the central. Delivered as-is, no line
// framing here — the CLI does its own newline handling.
typedef void (*ble_nus_rx_cb_t)(const uint8_t *data, size_t len, void *user);

// Connect fires once the central is present *and* has subscribed to
// notifications (i.e. the shell can start writing). Disconnect fires on
// any tear-down; on entry the transmit path is already reset.
typedef void (*ble_nus_state_cb_t)(void *user);

// One-time BLE bring-up: init CYW43, l2cap, sm, ATT server, set up
// advertising. Idempotent guard inside — safe to call multiple times.
// Returns 0 on success, negative on cyw43 init failure.
int ble_nus_init(void);

// Callbacks — install before ble_nus_init() finishes powering on so
// events aren't dropped.
void ble_nus_set_rx_callback(ble_nus_rx_cb_t cb, void *user);
void ble_nus_set_connect_callback(ble_nus_state_cb_t cb, void *user);
void ble_nus_set_disconnect_callback(ble_nus_state_cb_t cb, void *user);

// Queue bytes for transmission. Returns the number of bytes accepted;
// less than `len` if the send buffer is full (excess is dropped — the
// terminal is a best-effort channel).
size_t ble_nus_send(const uint8_t *data, size_t len);

// True while a central is connected and has subscribed to notifications.
bool ble_nus_is_connected(void);

// Adapter matching cli.h's cli_output_fn signature. Wire this into the
// BLE-side cli_ctx_t so command output flows over notifications.
int ble_nus_cli_write(const char *data, size_t len, void *user);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_BLE_NUS_H
