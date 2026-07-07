# PicoBLE-Terminal — BLE UART CLI firmware for Raspberry Pi Pico W.
#
# Phase 2 layout: one small bootloader at 0x10000000 and two identical
# copies of the application relinked for slot A (0x10004000) and slot
# B (0x100C4000). All three are produced as separate .uf2 files.
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

# --- App sources -------------------------------------------------------------

file(GLOB CLI_COMMAND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/cli/commands/*.c"
)

set(APP_SOURCES
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

# --- Per-slot app target -----------------------------------------------------
#
# Same source, two builds. Every literal address the compiler emits
# is baked in at link time — the two binaries are therefore NOT
# interchangeable. The bootloader picks whichever slot's UF2 the user
# most recently downloaded, so we always produce both.

function(add_app_slot slot_letter slot_id)
    set(TARGET picoble_terminal_${slot_letter})

    add_executable(${TARGET} ${APP_SOURCES})

    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/src
    )

    target_compile_definitions(${TARGET} PRIVATE
        PICOBLE_SLOT=${slot_id}
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
        hardware_watchdog
        hardware_clocks
        hardware_flash
        hardware_sync
        pico_flash
    )

    pico_set_linker_script(${TARGET}
        "${CMAKE_CURRENT_LIST_DIR}/linker/memmap_app_slot_${slot_letter}.ld"
    )

    pico_enable_stdio_usb(${TARGET} 1)
    pico_enable_stdio_uart(${TARGET} 0)

    pico_add_extra_outputs(${TARGET})
endfunction()

add_app_slot(a 0)
add_app_slot(b 1)

# --- Bootloader --------------------------------------------------------------

add_subdirectory(bootloader)
