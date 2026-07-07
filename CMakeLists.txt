# PicoBLE-Terminal — BLE UART CLI firmware for Raspberry Pi Pico W
cmake_minimum_required(VERSION 3.13)

# Default to the Pico W board — required for BLE
set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(picoble_terminal C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Fail fast if the board isn't a wireless variant.
if (NOT PICO_CYW43_SUPPORTED)
    message(FATAL_ERROR "This project requires a Pico W (or Pico 2 W) board with CYW43 wireless support.")
endif ()

set(APP_TARGET picoble_terminal)

# --- Sources -----------------------------------------------------------------
# Commands are dropped into src/cli/commands/ and picked up by glob so adding
# a new command file is a single-step operation. The trade-off (CMake missing
# additions without a re-configure) is acceptable here — this firmware is
# rebuilt from scratch through `cmake --build build` after any file add.
file(GLOB CLI_COMMAND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/cli/commands/*.c"
)

add_executable(${APP_TARGET}
    src/main.c
    src/cli/cli.c
    src/cli/parser.c
    src/cli/command.c
    ${CLI_COMMAND_SOURCES}
    src/ble/ble_nus.c
    src/system/log.c
    src/system/uptime.c
    src/system/sysinfo.c
    src/system/sha256.c
    src/drivers/gpio_ctrl.c
    src/drivers/adc_ctrl.c
    src/ota/ota.c
    src/storage/config.c
    src/network/wifi.c
    src/network/http_get.c
)

# Optional build-time Wi-Fi fallback (used only if no flash-saved creds).
if (DEFINED WIFI_SSID)
    target_compile_definitions(${APP_TARGET} PRIVATE WIFI_SSID="${WIFI_SSID}")
endif ()
if (DEFINED WIFI_PASSWORD)
    target_compile_definitions(${APP_TARGET} PRIVATE WIFI_PASSWORD="${WIFI_PASSWORD}")
endif ()

target_include_directories(${APP_TARGET} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Build the GATT database header from the NUS definition.
pico_btstack_make_gatt_header(${APP_TARGET} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src/ble/nus.gatt"
)

target_link_libraries(${APP_TARGET} PRIVATE
    pico_stdlib
    pico_btstack_ble
    pico_btstack_cyw43
    # Wi-Fi + BT coexist: the lwip_threadsafe_background arch drives LwIP
    # from an async_context worker, freeing us from polling in the main
    # loop while still letting BTStack use its own run loop.
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_adc
    hardware_gpio
    hardware_watchdog
    hardware_clocks
    hardware_flash
    hardware_sync
    pico_flash
)

# stdio: USB CDC on for local debug, UART off (BLE is the primary console).
pico_enable_stdio_usb(${APP_TARGET} 1)
pico_enable_stdio_uart(${APP_TARGET} 0)

pico_add_extra_outputs(${APP_TARGET})
