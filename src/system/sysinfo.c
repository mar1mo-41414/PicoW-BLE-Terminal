#include "system/sysinfo.h"
#include "system/version.h"
#include "system/uptime.h"
#include "ble/ble_nus.h"
#include "network/wifi.h"

#include "pico/version.h"
#include "hardware/clocks.h"
#include "hardware/regs/addressmap.h"

// PICO_FLASH_SIZE_BYTES is per-board (pico_w = 2MB); guard so the header
// still builds if we ever compile for a board that doesn't define it.
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES 0
#endif

// pico_version.h emits PICO_SDK_VERSION_STRING as a bare string.
#ifndef PICO_SDK_VERSION_STRING
#define PICO_SDK_VERSION_STRING "unknown"
#endif

static void print_kv(cli_ctx_t *ctx, const char *k, const char *v) {
    cli_printf(ctx, "  %-14s : %s\r\n", k, v);
}

void sysinfo_print(cli_ctx_t *ctx) {
    cli_write(ctx, "system info\r\n");

    print_kv(ctx, "FW Version",  PICOBLE_FW_VERSION);
    print_kv(ctx, "SDK Version", PICO_SDK_VERSION_STRING);
    print_kv(ctx, "Build Date",  PICOBLE_BUILD_DATE);

    // RAM: address-map macros give us the total SRAM range on this chip.
#if defined(SRAM_END) && defined(SRAM_BASE)
    cli_printf(ctx, "  %-14s : %u KB\r\n", "RAM",
               (unsigned)((SRAM_END - SRAM_BASE) / 1024u));
#else
    print_kv(ctx, "RAM", "unimplemented");
#endif

    if (PICO_FLASH_SIZE_BYTES > 0) {
        cli_printf(ctx, "  %-14s : %u KB\r\n", "Flash",
                   (unsigned)(PICO_FLASH_SIZE_BYTES / 1024u));
    } else {
        print_kv(ctx, "Flash", "unimplemented");
    }

    cli_printf(ctx, "  %-14s : %lu Hz\r\n", "CPU Clock",
               (unsigned long)clock_get_hz(clk_sys));

    print_kv(ctx, "BLE",
             ble_nus_is_connected() ? "connected" : "advertising");

    if (wifi_is_connected()) {
        char ip[24];
        wifi_format_ip(ip, sizeof(ip));
        cli_printf(ctx, "  %-14s : connected (%s)\r\n", "Wi-Fi", ip);
    } else {
        print_kv(ctx, "Wi-Fi", "disconnected");
    }

    char up[32];
    uptime_format(up, sizeof(up));
    print_kv(ctx, "Uptime", up);
}
