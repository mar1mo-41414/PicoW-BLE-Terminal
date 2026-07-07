// Persistent configuration — currently just Wi-Fi credentials.
//
// Stored in the last 4 KB sector of flash so we don't collide with the
// OTA staging slot (0x0C0000..0x180000) or the running image. Layout is
// a fixed struct with a magic and CRC-32 so we can distinguish a valid
// record from an erased sector or arbitrary garbage.
#ifndef PICOBLE_STORAGE_CONFIG_H
#define PICOBLE_STORAGE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_SSID_MAXLEN 32   // 802.11 SSID max
#define CONFIG_PSK_MAXLEN  63   // WPA2 PSK max (min is 8)

typedef struct {
    char ssid[CONFIG_SSID_MAXLEN + 1];  // NUL-terminated
    char psk[CONFIG_PSK_MAXLEN + 1];
} wifi_creds_t;

// Load credentials. Returns true if a valid record is present; otherwise
// leaves *out untouched and returns false.
bool config_load_wifi(wifi_creds_t *out);

// Persist credentials. Erases the sector and writes a fresh record.
// Returns true on success. Failure paths: flash lock timeout, over-length
// strings.
bool config_save_wifi(const wifi_creds_t *creds);

// Wipe any stored Wi-Fi credentials.
bool config_clear_wifi(void);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_STORAGE_CONFIG_H
