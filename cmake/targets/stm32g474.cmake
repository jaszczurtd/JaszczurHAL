# ─────────────────────────────────────────────────────────────────────────────
# JaszczurHAL stm32g474 firmware recipe.
#
# Included by cmake/jh_firmware_project after enable_language(C CXX ASM) with the
# STM32 cross toolchain (CMAKE_TOOLCHAIN_FILE=stm32_lib/toolchain_stm32g474.cmake,
# passed via the manifest cache). Delegates the link to the reusable L1 recipe
# and exposes the canonical firmware* targets.
#
# Entry contract:
#   - Projects that define app_start/app_task0[/app_task1] directly (canonical)
#     are built as-is; HAL_ENABLE_APP_TASK1 comes from their hal_project_config.h.
#   - Projects using the Fiesta initialization/looper convention (they ship a
#     firmware_entry.h) get a generated adapter (build dir only), with core1
#     read straight from firmware_entry.h.
#
# Optional manifest cache inputs (target-unaware, declared by the project):
#   JH_EXTRA_INCLUDES  extra include dirs (';'-separated)
#   JH_EXTRA_DEFINES   extra compile definitions (';'-separated)
#   OPENOCD_BIN / OPENOCD_INTERFACE / OPENOCD_TARGET
# ─────────────────────────────────────────────────────────────────────────────

include("${JH_ROOT}/stm32_lib/jh_stm32g474_firmware.cmake")
include("${JH_ROOT}/cmake/jh_entry_adapter.cmake")

set(JH_EXTRA_INCLUDES "" CACHE STRING "Extra include dirs for the firmware")
set(JH_EXTRA_DEFINES "" CACHE STRING "Extra compile definitions for the firmware")
set(JH_EXTRA_LIBRARIES "" CACHE STRING "Extra flat library dirs to compile+include (';'-separated), e.g. Credentials")
set(OPENOCD_BIN "openocd" CACHE STRING "OpenOCD executable")
set(OPENOCD_INTERFACE "interface/stlink.cfg" CACHE STRING "OpenOCD interface script")
set(OPENOCD_TARGET "target/stm32g4x.cfg" CACHE STRING "OpenOCD target script")

file(GLOB _sources CONFIGURE_DEPENDS
    "${JH_PROJECT_DIR}/*.c"
    "${JH_PROJECT_DIR}/*.cpp"
)

# Extra flat libraries (the STM32 counterpart of the RP2040 recipe's
# --libraries): each dir's C/C++ sources are compiled and its root is added to
# the include path. This is how an external library like Credentials is linked
# into a bare-metal STM32 build (arduino-cli discovers it automatically on RP2040).
set(_extra_lib_includes "")
foreach(_lib IN LISTS JH_EXTRA_LIBRARIES)
    if(_lib AND IS_DIRECTORY "${_lib}")
        file(GLOB _lib_sources CONFIGURE_DEPENDS
            "${_lib}/*.c" "${_lib}/*.cpp" "${_lib}/*.cc")
        list(APPEND _sources ${_lib_sources})
        list(APPEND _extra_lib_includes "${_lib}")
    elseif(_lib)
        message(WARNING "jh-firmware[stm32g474]: JH_EXTRA_LIBRARIES entry not a dir: ${_lib}")
    endif()
endforeach()

set(_defines HAL_PROVIDE_APP_ENTRY=1 ${JH_EXTRA_DEFINES})

# Fiesta-convention projects: bridge initialization/looper -> app_* via the
# shared generated adapter. Canonical-entry projects define app_start directly.
jh_generate_entry_adapter("${JH_PROJECT_DIR}" "${CMAKE_BINARY_DIR}/generated" _adapter _core1)
if(_adapter)
    if(_core1)
        list(APPEND _defines HAL_ENABLE_APP_TASK1=1)
    endif()
    list(APPEND _sources "${_adapter}")
endif()

jh_add_stm32g474_firmware(firmware
    JH_ROOT "${JH_ROOT}"
    SOURCES ${_sources}
    INCLUDES "${JH_PROJECT_DIR}" ${JH_EXTRA_INCLUDES} ${_extra_lib_includes}
    DEFINES ${_defines}
)

add_custom_target(firmware_debug DEPENDS firmware)
add_custom_target(firmware_compile_db COMMAND "${CMAKE_COMMAND}" -E true)
jh_stm32g474_add_openocd_upload(firmware_upload
    ELF_TARGET firmware
    OPENOCD_BIN "${OPENOCD_BIN}"
    OPENOCD_INTERFACE "${OPENOCD_INTERFACE}"
    OPENOCD_TARGET "${OPENOCD_TARGET}"
)

message(STATUS "jh-firmware[stm32g474]: sources=${JH_PROJECT_DIR} extra_inc=${JH_EXTRA_INCLUDES}")
