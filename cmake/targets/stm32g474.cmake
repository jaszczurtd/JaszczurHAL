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
include("${JH_ROOT}/stm32_lib/freertos_stm32g474.cmake")
include("${JH_ROOT}/cmake/jh_entry_adapter.cmake")

set(JH_EXTRA_INCLUDES "" CACHE STRING "Extra include dirs for the firmware")
set(JH_EXTRA_DEFINES "" CACHE STRING "Extra compile definitions for the firmware")
set(JH_EXTRA_LIBRARIES "" CACHE STRING "Extra flat library dirs to compile+include (';'-separated), e.g. Credentials")
set(OPENOCD_BIN "openocd" CACHE STRING "OpenOCD executable")
set(OPENOCD_INTERFACE "interface/stlink.cfg" CACHE STRING "OpenOCD interface script")
set(OPENOCD_TARGET "target/stm32g4x.cfg" CACHE STRING "OpenOCD target script")

jh_resolve_project_sources(_sources)

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
function(_jh_extract_define_value OUT_VAR KEY)
    set(_value "")
    foreach(_def IN LISTS ARGN)
        if("${_def}" MATCHES "^${KEY}=(.+)$")
            set(_value "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${OUT_VAR} "${_value}" PARENT_SCOPE)
endfunction()

_jh_extract_define_value(_stm32_main_stack_size HAL_STM32_MAIN_STACK_SIZE ${_defines})
_jh_extract_define_value(_stm32_littlefs_size HAL_STM32_FLASH_LITTLEFS_SIZE ${_defines})
jh_cmake_defines_contain(_stm32_has_littlefs HAL_ENABLE_LITTLEFS ${_defines})
if(_stm32_has_littlefs AND "${_stm32_littlefs_size}" STREQUAL "")
    list(APPEND _defines HAL_STM32_FLASH_LITTLEFS_SIZE=65536u)
    set(_stm32_littlefs_size "65536")
endif()
if(NOT "${_stm32_littlefs_size}" STREQUAL "")
    string(REGEX REPLACE "[uUlL]+$" "" _stm32_littlefs_size "${_stm32_littlefs_size}")
endif()

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

if(NOT "${_stm32_main_stack_size}" STREQUAL "")
    target_link_options(firmware PRIVATE
        "-Wl,--defsym=HAL_STM32_MIN_STACK_SIZE=${_stm32_main_stack_size}"
    )
endif()
if(NOT "${_stm32_littlefs_size}" STREQUAL "")
    target_link_options(firmware PRIVATE
        "-Wl,--defsym=HAL_STM32_FLASH_LITTLEFS_SIZE=${_stm32_littlefs_size}"
    )
endif()
jh_cmake_defines_contain(_stm32_has_freertos HAL_ENABLE_FREERTOS ${_defines})
if(_stm32_has_freertos)
    jh_stm32g474_enable_freertos(firmware)
endif()

add_custom_target(firmware_debug DEPENDS firmware)
add_custom_target(firmware_compile_db COMMAND "${CMAKE_COMMAND}" -E true)
jh_stm32g474_add_openocd_upload(firmware_upload
    ELF_TARGET firmware
    OPENOCD_BIN "${OPENOCD_BIN}"
    OPENOCD_INTERFACE "${OPENOCD_INTERFACE}"
    OPENOCD_TARGET "${OPENOCD_TARGET}"
)

message(STATUS "jh-firmware[stm32g474]: sources=${JH_PROJECT_DIR} extra_inc=${JH_EXTRA_INCLUDES}")
