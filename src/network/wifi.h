// Wi-Fi station bring-up + status.
//
// cyw43_arch itself is already initialized by ble_nus_init() because
// the same chip runs BT. wifi_init() therefore only flips the CYW43
// into STA mode; the actual join happens in wifi_connect().
//
// Blocking model: wifi_connect() sits on the calling thread until the
// join completes or times out. On this firmware that's fine — the
// caller is a CLI command handler that already owns the shell.
#ifndef PICOBLE_NETWORK_WIFI_H
#define PICOBLE_NETWORK_WIFI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// One-time STA-mode enable. Safe to call more than once.
int wifi_init(void);

// Attempt to join an AP. Returns 0 on success, non-zero on failure
// (transient failure codes from cyw43_arch or -1 for bad arguments).
int wifi_connect(const char *ssid, const char *psk, uint32_t timeout_ms);

// Explicitly leave the AP. Idempotent.
void wifi_disconnect(void);

// True when the STA link is up and has an IP.
bool wifi_is_connected(void);

// Current IPv4 in network byte order (big-endian). Zero if not up.
uint32_t wifi_get_ip_be(void);

// Convenience: dotted-quad into caller buffer. Returns bytes written.
size_t wifi_format_ip(char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_NETWORK_WIFI_H
