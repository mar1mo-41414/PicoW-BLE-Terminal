# PicoBLE-Terminal — BLE UART CLI firmware for Raspberry Pi Pico W.
#
# The dual-slot OTA infrastructure (bootloader, linker scripts, ota API,
# metadata / crc32 / sha256) lives in picow-ota/ as a reusable framework.
# This file wires the framework into two application builds — one for
# each slot — and drops the framework's bootloader UF2 into the tree.
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(picoble_terminal C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if (NOT PICO_CYW43_SUPPORTED)
    message(FATAL_ERROR "This project requires a Pico W (or Pico 2 W) board with CYW43 wireless support.")
endif ()

# --- OTA framework (provides pico_ota + pico_ota_bootloader) ----------------
add_subdirectory(picow-ota)

# --- App sources -------------------------------------------------------------

file(GLOB CLI_COMMAND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/cli/commands/*.c"
)

set(APP_SOURCES
    src/main.c
    src/cli/cli.c
    src/cli/parser.c
    src/cli/command.c
    src/cli/duration.c
    src/cli/shell_state.c
    ${CLI_COMMAND_SOURCES}
    src/ble/ble_nus.c
    src/system/log.c
    src/system/uptime.c
    src/system/sysinfo.c
    src/system/vars.c
    src/system/bindings.c
    src/drivers/gpio_ctrl.c
    src/drivers/adc_ctrl.c
    src/drivers/pwm_ctrl.c
    src/drivers/temp_sensor.c
    src/drivers/i2c_ctrl.c
    src/storage/config.c
    src/storage/scripts.c
    src/network/wifi.c
    src/network/http_get.c
)

# --- Per-slot app target -----------------------------------------------------

function(add_app_slot slot_letter slot_id)
    set(TARGET picoble_terminal_${slot_letter})

    add_executable(${TARGET} ${APP_SOURCES})

    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/src
    )

    if (DEFINED WIFI_SSID)
        target_compile_definitions(${TARGET} PRIVATE WIFI_SSID="${WIFI_SSID}")
    endif ()
    if (DEFINED WIFI_PASSWORD)
        target_compile_definitions(${TARGET} PRIVATE WIFI_PASSWORD="${WIFI_PASSWORD}")
    endif ()

    pico_btstack_make_gatt_header(${TARGET} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/ble/nus.gatt"
    )

    target_link_libraries(${TARGET} PRIVATE
        pico_stdlib
        pico_btstack_ble
        pico_btstack_cyw43
        pico_cyw43_arch_lwip_poll
        hardware_adc
        hardware_gpio
        hardware_pwm
        hardware_i2c
        hardware_watchdog
        hardware_clocks
        hardware_flash
        hardware_sync
        pico_flash
        pico_ota
    )

    # The framework's helper sets the linker script + PICO_OTA_SLOT.
    pico_ota_set_slot(${TARGET} ${slot_id})

    pico_enable_stdio_usb(${TARGET} 1)
    pico_enable_stdio_uart(${TARGET} 0)

    pico_add_extra_outputs(${TARGET})
endfunction()

add_app_slot(a 0)
add_app_slot(b 1)
