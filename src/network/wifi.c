// Wi-Fi station glue over pico_cyw43_arch_lwip_threadsafe_background.
#include "network/wifi.h"
#include "system/log.h"

#include "pico/cyw43_arch.h"
#include "lwip/netif.h"

#include <stdio.h>
#include <string.h>

static bool g_sta_enabled = false;

int wifi_init(void) {
    if (g_sta_enabled) return 0;
    // The CYW43 chip is already alive (ble_nus_init did cyw43_arch_init).
    // Enabling STA mode here is a lightweight state flip in the driver.
    cyw43_arch_enable_sta_mode();
    g_sta_enabled = true;
    LOGI("wifi: STA mode enabled");
    return 0;
}

int wifi_connect(const char *ssid, const char *psk, uint32_t timeout_ms) {
    if (!ssid || !*ssid) return -1;
    if (!g_sta_enabled) wifi_init();
    LOGI("wifi: joining %s (timeout %u ms)", ssid, (unsigned)timeout_ms);

    // WPA2-PSK covers virtually every home AP. WPA3 requires SAE which
    // this cyw43 firmware handles transparently under the same auth
    // constant on newer builds; fall back to WPA1 with the same call
    // isn't worth the complexity here.
    int rc = cyw43_arch_wifi_connect_timeout_ms(
        ssid, psk ? psk : "", CYW43_AUTH_WPA2_AES_PSK, timeout_ms);
    if (rc != 0) {
        LOGW("wifi: connect failed rc=%d", rc);
        return rc;
    }
    return 0;
}

void wifi_disconnect(void) {
    if (!g_sta_enabled) return;
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    LOGI("wifi: disconnected");
}

bool wifi_is_connected(void) {
    if (!g_sta_enabled) return false;
    int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    return link == CYW43_LINK_UP;
}

uint32_t wifi_get_ip_be(void) {
    if (!wifi_is_connected()) return 0;
    struct netif *nif = netif_default;
    if (!nif) return 0;
    return ip4_addr_get_u32(netif_ip4_addr(nif));
}

size_t wifi_format_ip(char *buf, size_t buflen) {
    if (!buf || buflen < 8) return 0;
    uint32_t be = wifi_get_ip_be();
    if (be == 0) { snprintf(buf, buflen, "0.0.0.0"); return strlen(buf); }
    uint8_t *o = (uint8_t *)&be;
    int n = snprintf(buf, buflen, "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
    return n > 0 ? (size_t)n : 0;
}
