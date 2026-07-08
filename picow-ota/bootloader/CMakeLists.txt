# pico-ota bootloader — the small executable that lives at 0x10000000
# with its own boot2 and dispatches to the currently-active slot.

set(BOOT_TARGET pico_ota_bootloader)

add_executable(${BOOT_TARGET} main.c)

target_link_libraries(${BOOT_TARGET} PRIVATE
    pico_stdlib
    pico_bootrom
    hardware_flash
    hardware_sync
    hardware_watchdog
    pico_ota_metadata
)

pico_set_linker_script(${BOOT_TARGET}
    "${CMAKE_CURRENT_LIST_DIR}/../linker/memmap_bootloader.ld"
)

# No shell, no logging — bootloader is silent.
pico_enable_stdio_usb(${BOOT_TARGET} 0)
pico_enable_stdio_uart(${BOOT_TARGET} 0)

pico_add_extra_outputs(${BOOT_TARGET})
