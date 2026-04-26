cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_Arduino C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

# ── Paths (set by toolchain file or caller) ──────────────────────────────────
if(NOT DEFINED ARDUINO_CORE_PATH)
    message(FATAL_ERROR "ARDUINO_CORE_PATH is not set. "
            "Use the toolchain file or pass -DARDUINO_CORE_PATH=...")
endif()
if(NOT DEFINED ARDUINO_VARIANT_PATH)
    message(FATAL_ERROR "ARDUINO_VARIANT_PATH is not set. "
            "Use the toolchain file or pass -DARDUINO_VARIANT_PATH=...")
endif()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# ── Collect core include paths from Arduino platform files ───────────────────
# The platform ships two @-response files listing -iwithprefixbefore paths.
# We translate them into real include dirs for CMake.

set(ARDUINO_PLATFORM_DIR "${ARDUINO_CORE_PATH}/../..")   # .../rp2040/<ver>

function(parse_platform_inc_file FILEPATH PREFIX OUT_VAR)
    if(NOT EXISTS "${FILEPATH}")
        return()
    endif()
    file(STRINGS "${FILEPATH}" _lines)
    set(_result "")
    foreach(_line IN LISTS _lines)
        string(STRIP "${_line}" _line)
        if(_line MATCHES "^-iwithprefixbefore/(.*)")
            set(_relpath "${CMAKE_MATCH_1}")
            list(APPEND _result "${PREFIX}/${_relpath}")
        endif()
    endforeach()
    set(${OUT_VAR} ${_result} PARENT_SCOPE)
endfunction()

parse_platform_inc_file(
    "${ARDUINO_PLATFORM_DIR}/lib/core_inc.txt"
    "${ARDUINO_PLATFORM_DIR}"
    CORE_INC_DIRS
)
parse_platform_inc_file(
    "${ARDUINO_PLATFORM_DIR}/lib/${ARDUINO_CHIP}/platform_inc.txt"
    "${ARDUINO_PLATFORM_DIR}"
    PLATFORM_INC_DIRS
)

# ── Arduino platform libraries (Wire, SPI, EEPROM, WiFi, …) ─────────────────
# arduino-cli resolves these on the fly; we include all of them so that HAL
# modules can find <Wire.h>, <SPI.h>, etc.
file(GLOB _arduino_lib_dirs "${ARDUINO_PLATFORM_DIR}/libraries/*/src")
set(ARDUINO_LIB_INC_DIRS ${_arduino_lib_dirs})

# ── Collect defines from platform def files ──────────────────────────────────
function(parse_platform_def_file FILEPATH OUT_VAR)
    if(NOT EXISTS "${FILEPATH}")
        return()
    endif()
    file(STRINGS "${FILEPATH}" _lines)
    set(_result "")
    foreach(_line IN LISTS _lines)
        string(STRIP "${_line}" _line)
        if(_line MATCHES "^-D(.*)")
            list(APPEND _result "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${OUT_VAR} ${_result} PARENT_SCOPE)
endfunction()

parse_platform_def_file(
    "${ARDUINO_PLATFORM_DIR}/lib/platform_def.txt"
    PLATFORM_DEFS
)
parse_platform_def_file(
    "${ARDUINO_PLATFORM_DIR}/lib/${ARDUINO_CHIP}/platform_def.txt"
    CHIP_DEFS
)

# ── HAL implementation sources (Arduino) ─────────────────────────────────────
file(GLOB ARDUINO_IMPL_SOURCES ${SRC}/hal/impl/arduino/*.cpp)

# ── Driver sources (third-party libraries bundled with HAL) ──────────────────
file(GLOB_RECURSE DRIVER_SOURCES
    ${SRC}/hal/impl/arduino/drivers/*.cpp
    ${SRC}/hal/impl/arduino/drivers/*.c
)

# ── Common HAL sources (not implementation-specific) ─────────────────────────
set(HAL_COMMON_SOURCES
    ${SRC}/hal/hal_config.cpp
    ${SRC}/hal/hal_can_util.cpp
    ${SRC}/hal/hal_crypto.cpp
    ${SRC}/hal/hal_kv.cpp
    ${SRC}/hal/hal_soft_timer.cpp
    ${SRC}/hal/hal_pid_controller.cpp
)

# ── Utility sources ──────────────────────────────────────────────────────────
set(UTIL_SOURCES
    ${SRC}/utils/tools.cpp
    ${SRC}/utils/multicoreWatchdog.cpp
    ${SRC}/utils/draw7Segment.cpp
    ${SRC}/utils/SmartTimers.cpp
    ${SRC}/utils/pidController.cpp
    ${SRC}/utils/cJSON.c
    ${SRC}/utils/cJSON_Utils.c
)

# Unity is optionally included (can be disabled via HAL_DISABLE_UNITY)
if(NOT HAL_DISABLE_UNITY)
    list(APPEND UTIL_SOURCES ${SRC}/utils/unity.c)
endif()

# ── Static library target ────────────────────────────────────────────────────
add_library(JaszczurHAL STATIC
    ${ARDUINO_IMPL_SOURCES}
    ${DRIVER_SOURCES}
    ${HAL_COMMON_SOURCES}
    ${UTIL_SOURCES}
)

target_include_directories(JaszczurHAL PUBLIC
    ${SRC}
    ${ARDUINO_CORE_PATH}
    ${ARDUINO_CORE_PATH}/api/deprecated-avr-comp
    ${ARDUINO_VARIANT_PATH}
    ${ARDUINO_PLATFORM_DIR}/include
    ${CORE_INC_DIRS}
    ${PLATFORM_INC_DIRS}
    ${ARDUINO_LIB_INC_DIRS}
)

# ── Compile definitions ─────────────────────────────────────────────────────
# Minimal board-agnostic set.  Board-specific values (USB VID/PID, flash
# sizes, LED pin, etc.) can be overridden via EXTRA_HAL_DEFINES.

set(BOARD_NAME "RASPBERRY_PI_PICO" CACHE STRING "Arduino board macro name")
set(ARDUINO_CHIP "rp2040" CACHE STRING "Target chip (rp2040 / rp2350)")
set(ARDUINO_F_CPU "125000000" CACHE STRING "CPU frequency in Hz")
set(ARDUINO_FLASH_TOTAL "2097152" CACHE STRING "Total flash in bytes")
set(ARDUINO_FS_START "270528512" CACHE STRING "Filesystem start address")
set(ARDUINO_FS_END "270528512" CACHE STRING "Filesystem end address")
set(HAL_DISPLAY_DRIVER "HAL_DISPLAY_ILI9341" CACHE STRING
    "TFT display driver macro (HAL_DISPLAY_ILI9341 / HAL_DISPLAY_ST7789 / ...)")

target_compile_definitions(JaszczurHAL PUBLIC
    ARDUINO=10607
    ARDUINO_BOARD="${BOARD_NAME}"
    ARDUINO_ARCH_RP2040
    ARDUINO_VARIANT="${ARDUINO_VARIANT}"
    F_CPU=${ARDUINO_F_CPU}
    PICO_FLASH_SIZE_BYTES=${ARDUINO_FLASH_TOTAL}
    FS_START=${ARDUINO_FS_START}
    FS_END=${ARDUINO_FS_END}
    ${HAL_DISPLAY_DRIVER}   # TFT backend selection
    # lwIP / network defines (from compiler.netdefines in platform.txt)
    LWIP_IGMP=1
    LWIP_CHECKSUM_CTRL_PER_NETIF=1
    ${PLATFORM_DEFS}
    ${CHIP_DEFS}
)

# Extra user-supplied defines (e.g. HAL_DISABLE_WIFI, PICO_W, etc.)
if(DEFINED EXTRA_HAL_DEFINES)
    target_compile_definitions(JaszczurHAL PUBLIC ${EXTRA_HAL_DEFINES})
endif()

# Project-level config header search path (for hal_project_config.h)
if(DEFINED HAL_PROJECT_CONFIG_DIR)
    target_include_directories(JaszczurHAL PUBLIC ${HAL_PROJECT_CONFIG_DIR})
endif()

# ── Compile options ──────────────────────────────────────────────────────────
set(_common_flags -ffunction-sections -fdata-sections -Wall -Werror=return-type -Wno-psabi -pipe -g)

target_compile_options(JaszczurHAL PRIVATE
    ${_common_flags}
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
)

# ── Install rules ────────────────────────────────────────────────────────────
install(TARGETS JaszczurHAL ARCHIVE DESTINATION lib)
install(DIRECTORY ${SRC}/ DESTINATION include
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "impl/.mock" EXCLUDE)
