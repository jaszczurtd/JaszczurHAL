cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_RP2040 C CXX)

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

# ── Arduino platform libraries (Wire, SPI, EEPROM, WiFi, ...) ─────────────────
# arduino-cli resolves these on the fly; we include all of them so that HAL
# modules can find <Wire.h>, <SPI.h>, etc.
file(GLOB _arduino_platform_lib_dirs "${ARDUINO_PLATFORM_DIR}/libraries/*/src")
set(ARDUINO_LIB_INC_DIRS ${_arduino_platform_lib_dirs})

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

# ── Optional arduino-pico OS mode --------------------------------------------
set(ARDUINO_OS "" CACHE STRING "Arduino-pico OS option: empty/none or freertos")
set_property(CACHE ARDUINO_OS PROPERTY STRINGS "" none freertos)

set(ARDUINO_OS_DEFS "")
set(ARDUINO_OS_INC_DIRS "")
if("${ARDUINO_OS}" STREQUAL "freertos")
    set(ARDUINO_FREERTOS_INCLUDE_DIR "${ARDUINO_CORE_PATH}/freertos" CACHE PATH
        "Arduino-pico FreeRTOS wrapper include directory")
    if(NOT EXISTS "${ARDUINO_FREERTOS_INCLUDE_DIR}/FreeRTOS.h")
        message(FATAL_ERROR
            "ARDUINO_OS=freertos requires ${ARDUINO_FREERTOS_INCLUDE_DIR}/FreeRTOS.h")
    endif()
    list(APPEND ARDUINO_OS_DEFS __FREERTOS)
    list(APPEND ARDUINO_OS_INC_DIRS "${ARDUINO_FREERTOS_INCLUDE_DIR}")
elseif(NOT "${ARDUINO_OS}" STREQUAL "" AND NOT "${ARDUINO_OS}" STREQUAL "none")
    message(FATAL_ERROR "Unsupported ARDUINO_OS='${ARDUINO_OS}' (expected empty, none, or freertos)")
endif()

function(jh_hal_define_enabled OUT_VAR DEFINE_NAME)
    set(_enabled FALSE)
    if(DEFINED ${DEFINE_NAME})
        set(_enabled TRUE)
    endif()
    if(DEFINED EXTRA_HAL_DEFINES)
        foreach(_def IN LISTS EXTRA_HAL_DEFINES)
            if("${_def}" STREQUAL "${DEFINE_NAME}" OR "${_def}" MATCHES "^${DEFINE_NAME}=")
                set(_enabled TRUE)
            endif()
        endforeach()
    endif()
    if(DEFINED HAL_PROJECT_CONFIG_DIR)
        foreach(_config_dir IN LISTS HAL_PROJECT_CONFIG_DIR)
            set(_config_file "${_config_dir}/hal_project_config.h")
            if(EXISTS "${_config_file}")
                file(STRINGS "${_config_file}" _config_hits
                    REGEX "^[ \t]*#[ \t]*define[ \t]+${DEFINE_NAME}([ \t(]|$)"
                )
                if(_config_hits)
                    set(_enabled TRUE)
                endif()
            endif()
        endforeach()
    endif()
    set(${OUT_VAR} ${_enabled} PARENT_SCOPE)
endfunction()

# ── HAL implementation sources (RP2040) ──────────────────────────────────────
file(GLOB RP2040_IMPL_SOURCES ${SRC}/hal/impl/rp2040/*.cpp)
file(GLOB_RECURSE RP2040_SHARED_SOURCES
    ${SRC}/hal/impl/shared/*.cpp
    ${SRC}/hal/impl/shared/*.c
)
list(FILTER RP2040_SHARED_SOURCES EXCLUDE REGEX "/frameworks/lwip/vendor/")

# ── Driver sources (third-party libraries bundled with HAL) ──────────────────
file(GLOB_RECURSE DRIVER_SOURCES
    ${SRC}/hal/impl/rp2040/drivers/*.cpp
    ${SRC}/hal/impl/rp2040/drivers/*.c
)

# ── Framework sources (bundled high-level integrations) ──────────────────────
set(FRAMEWORK_SOURCES)
jh_hal_define_enabled(JH_RP2040_ENABLE_MQTT HAL_ENABLE_MQTT)
jh_hal_define_enabled(JH_RP2040_ENABLE_WIREGUARD HAL_ENABLE_WIREGUARD)

if(JH_RP2040_ENABLE_MQTT)
    file(GLOB_RECURSE RP2040_PUBSUBCLIENT_SOURCES
        ${SRC}/hal/impl/rp2040/frameworks/PubSubClient/*.cpp
        ${SRC}/hal/impl/rp2040/frameworks/PubSubClient/*.c
    )
    list(APPEND FRAMEWORK_SOURCES ${RP2040_PUBSUBCLIENT_SOURCES})
endif()

if(JH_RP2040_ENABLE_WIREGUARD)
    file(GLOB_RECURSE RP2040_WIREGUARD_SOURCES
        ${SRC}/hal/impl/rp2040/frameworks/arduino-wireguard-pico-w/*.cpp
        ${SRC}/hal/impl/rp2040/frameworks/arduino-wireguard-pico-w/*.c
    )
    list(APPEND FRAMEWORK_SOURCES ${RP2040_WIREGUARD_SOURCES})
endif()

# ── Common HAL sources (not implementation-specific) ─────────────────────────
set(HAL_COMMON_SOURCES
    ${SRC}/hal/hal_config.cpp
    ${SRC}/hal/hal_network_status.cpp
    ${SRC}/hal/hal_tls.cpp
    ${SRC}/hal/hal_can_util.cpp
    ${SRC}/hal/hal_crypto.cpp
    ${SRC}/hal/hal_crc.cpp
    ${SRC}/hal/hal_pga2311.cpp
    ${SRC}/hal/hal_kv.cpp
    ${SRC}/hal/hal_timer_ext.cpp
    ${SRC}/hal/hal_soft_timer.cpp
    ${SRC}/hal/hal_pid_controller.cpp
    ${SRC}/hal_app_entry.cpp
)

# ── Utility sources ──────────────────────────────────────────────────────────
set(UTIL_SOURCES
    ${SRC}/utils/tools.cpp
    ${SRC}/utils/multicoreWatchdog.cpp
    ${SRC}/utils/draw7Segment.cpp
    ${SRC}/utils/pidController.cpp
)

# Unity is opt-in: enable it via -DHAL_ENABLE_UNITY=ON when configuring.
if(HAL_ENABLE_UNITY)
    list(APPEND UTIL_SOURCES ${SRC}/utils/unity.c)
endif()

# ── Static library target ────────────────────────────────────────────────────
add_library(JaszczurHAL STATIC
    ${RP2040_IMPL_SOURCES}
    ${RP2040_SHARED_SOURCES}
    ${DRIVER_SOURCES}
    ${FRAMEWORK_SOURCES}
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
    ${ARDUINO_OS_INC_DIRS}
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
    HAL_TARGET_RP2040       # canonical backend selection (see src/hal/hal_target.h)
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
    ${ARDUINO_OS_DEFS}
)

# Extra user-supplied defines (e.g. HAL_ENABLE_WIFI, PICO_W, etc.)
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
