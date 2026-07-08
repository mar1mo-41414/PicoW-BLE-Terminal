# pico-ota — dual-slot OTA framework for Raspberry Pi Pico W.

# Capture framework's own directory now — CMAKE_CURRENT_LIST_DIR is
# re-evaluated in the caller's scope when a function runs, so if we
# used it inside pico_ota_set_slot() below, it'd resolve to the app's
# CMakeLists directory rather than picow-ota/. Save it as a global so
# the helper always points at the shipped linker scripts.
set(PICO_OTA_FRAMEWORK_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "pico-ota framework root")
#
# Provides:
#   pico_ota_metadata  INTERFACE library — shared metadata struct + CRC-32
#                       (linked by both the bootloader and the application)
#   pico_ota           INTERFACE library — application-side API (ota.c,
#                       sha256.c, weak logging hooks)
#   pico_ota_bootloader  standalone executable target (its own linker
#                        script and UF2, dropped into flash at 0x10000000)
#
# Helper functions:
#   pico_ota_set_slot(<target> <0|1>)     — set slot A (0) or slot B (1)
#                                            linker + PICO_OTA_SLOT define
#
# The application should:
#   add_subdirectory(picow-ota)
#   add_executable(myapp_a ${SOURCES})
#   target_link_libraries(myapp_a PRIVATE pico_ota ...)
#   pico_ota_set_slot(myapp_a 0)
#   pico_add_extra_outputs(myapp_a)
#   # ... same for myapp_b with slot 1 ...
#
# The bootloader target is created here automatically.

# ---- pico_ota_metadata (shared header + CRC) --------------------------------

add_library(pico_ota_metadata INTERFACE)
target_sources(pico_ota_metadata INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/crc32.c
)
target_include_directories(pico_ota_metadata INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# ---- pico_ota (application-side API) ----------------------------------------

add_library(pico_ota INTERFACE)
target_sources(pico_ota INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/ota.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sha256.c
    ${CMAKE_CURRENT_LIST_DIR}/src/log_default.c
)
target_link_libraries(pico_ota INTERFACE
    pico_ota_metadata
    hardware_flash
    hardware_watchdog
    hardware_sync
    pico_flash
)

# ---- slot linker helper -----------------------------------------------------
#
# Applies the correct memmap_app_slot_<a|b>.ld linker script and defines
# PICO_OTA_SLOT so the OTA API knows which slot it is running from.

function(pico_ota_set_slot target slot_id)
    if (slot_id EQUAL 0)
        set(script "${PICO_OTA_FRAMEWORK_DIR}/linker/memmap_app_slot_a.ld")
    elseif (slot_id EQUAL 1)
        set(script "${PICO_OTA_FRAMEWORK_DIR}/linker/memmap_app_slot_b.ld")
    else ()
        message(FATAL_ERROR "pico_ota_set_slot: slot_id must be 0 (A) or 1 (B); got ${slot_id}")
    endif ()
    pico_set_linker_script(${target} "${script}")
    target_compile_definitions(${target} PRIVATE PICO_OTA_SLOT=${slot_id})
endfunction()

# ---- bootloader executable --------------------------------------------------

add_subdirectory(bootloader)
