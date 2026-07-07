# PicoBLE-Terminal bootloader — standalone target, produces its own UF2.
# Kept as narrow as possible (no stdio, no cyw43) so it fits well inside
# the 16 KB reserved at the head of flash.

set(BOOT_TARGET picoble_terminal_boot)

add_executable(${BOOT_TARGET} main.c)

target_include_directories(${BOOT_TARGET} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_link_libraries(${BOOT_TARGET} PRIVATE
    pico_stdlib
    pico_bootrom
    hardware_flash
    hardware_sync
    hardware_watchdog
)

pico_set_linker_script(${BOOT_TARGET}
    "${CMAKE_CURRENT_LIST_DIR}/../linker/memmap_bootloader.ld"
)

pico_enable_stdio_usb(${BOOT_TARGET} 0)
pico_enable_stdio_uart(${BOOT_TARGET} 0)

pico_add_extra_outputs(${BOOT_TARGET})
