cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_STM32G474 C CXX)

# The backend also compiles with the host compiler for sanity and static
# analysis (hal_target.h leaves JH_STM32G474_HW undefined there). That mode is
# opt-in so a missing cross toolchain cannot silently yield a host library.
option(JH_STM32_HOST_SANITY
    "Configure the STM32G474 backend with the host compiler for analysis"
    OFF)
if(CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH
        "STM32 toolchain used by the static-library build" FORCE)
elseif(JH_STM32_HOST_SANITY)
    message(STATUS
        "stm32_lib: host-compiler sanity build; JH_STM32G474_HW stays undefined")
else()
    message(FATAL_ERROR
        "CMAKE_TOOLCHAIN_FILE is required for stm32_lib. Pass "
        "-DJH_STM32_HOST_SANITY=ON for the host-compiler analysis build.")
endif()

set(JH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(JH_TARGET "stm32g474" CACHE STRING "JaszczurHAL target ID")
set(JH_BOARD "nucleo-g474re" CACHE STRING "Physical board profile")
set(EXTRA_HAL_DEFINES "" CACHE STRING "Extra HAL compile definitions")
include("${JH_ROOT}/cmake/jh_board_profiles.cmake")
jh_generate_board_config(
    ROOT "${JH_ROOT}"
    TARGET "${JH_TARGET}"
    BOARD "${JH_BOARD}"
    OUTPUT_DIR
        "${CMAKE_BINARY_DIR}/generated/boards/${JH_TARGET}/${JH_BOARD}"
    OUTPUT_ROOT "${CMAKE_BINARY_DIR}"
    DEFINES ${EXTRA_HAL_DEFINES})
if(NOT JH_RESOLVED_TARGET STREQUAL "stm32g474")
    message(FATAL_ERROR "stm32_lib requires target stm32g474")
endif()

include("${JH_ROOT}/cmake/jh_board_components.cmake")
jh_apply_board_components(PROVIDER "${JH_BOARD_PROVIDER}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
include(${CMAKE_CURRENT_SOURCE_DIR}/freertos_stm32g474.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_bearssl.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_managed_frameworks.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_cyw43_driver.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_btstack.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_littlefs.cmake)
jh_bearssl_source_manifest(
    _jh_stm32_bearssl_sources
    _jh_stm32_bearssl_include_dirs)
jh_managed_framework_include_dirs(_jh_stm32_framework_include_dirs)
jh_managed_framework_configure_sources()
jh_littlefs_source_manifest(
    STM32_LITTLEFS_SOURCES
    STM32_LITTLEFS_INCLUDE_DIRS)

# ── HAL implementation sources (STM32G474 skeleton) ────────────────────────
file(GLOB STM32_IMPL_SOURCES ${SRC}/hal/impl/stm32g474/*.cpp)
# SoC-specific drivers under impl/stm32g474/drivers/<soc>/
file(GLOB STM32_DRIVER_SOURCES ${SRC}/hal/impl/stm32g474/drivers/*/*.cpp)
list(APPEND STM32_IMPL_SOURCES ${STM32_DRIVER_SOURCES})
# Backend-agnostic implementations shared with the RP2040 backend
# (e.g. the portable NMEA GPS engine + parser).
file(GLOB_RECURSE STM32_SHARED_SOURCES
    ${SRC}/hal/impl/shared/*.cpp
    ${SRC}/hal/impl/shared/*.c
)
list(FILTER STM32_SHARED_SOURCES EXCLUDE REGEX "/frameworks/PubSubClient/")
list(FILTER STM32_SHARED_SOURCES EXCLUDE REGEX "/bluetooth/")
list(APPEND STM32_IMPL_SOURCES ${STM32_SHARED_SOURCES})
set(STM32_MQTT_INCLUDE_DIRS)
if(DEFINED EXTRA_HAL_DEFINES)
    jh_cmake_defines_contain(_jh_stm32_has_mqtt HAL_ENABLE_MQTT
        ${EXTRA_HAL_DEFINES})
    if(_jh_stm32_has_mqtt)
        list(APPEND STM32_IMPL_SOURCES
            ${SRC}/hal/impl/shared/frameworks/PubSubClient/src/PubSubClient.cpp)
        list(APPEND STM32_MQTT_INCLUDE_DIRS
            ${SRC}/hal/impl/shared/frameworks/PubSubClient/src)
    endif()
endif()
list(APPEND STM32_IMPL_SOURCES ${STM32_LITTLEFS_SOURCES})

# ── Common HAL sources (backend-agnostic wrappers) ──────────────────────────
set(HAL_COMMON_SOURCES
    ${SRC}/hal/hal_assert.cpp
    ${SRC}/hal/hal_board.cpp
    ${SRC}/hal/hal_ble.cpp
    ${SRC}/hal/hal_config.cpp
    ${SRC}/hal/hal_network_status.cpp
    ${SRC}/hal/hal_tls.cpp
    ${SRC}/hal/hal_can_util.cpp
    ${SRC}/hal/hal_crypto.cpp
    ${SRC}/hal/hal_crc.cpp
    ${SRC}/hal/hal_pga2311.cpp
    ${SRC}/hal/hal_digipot.cpp
    ${SRC}/hal/hal_kv.cpp
    ${SRC}/hal/hal_timer_ext.cpp
    ${SRC}/hal/hal_soft_timer.cpp
    ${SRC}/hal/hal_pid_controller.cpp
    ${SRC}/hal_app_entry.cpp
)

# ── Target-independent utility sources ──────────────────────────────────────
set(UTIL_SOURCES
    ${SRC}/utils/pidController.cpp
    ${SRC}/utils/draw7Segment.cpp
)

# Unity is opt-in: enable it via -DHAL_ENABLE_UNITY=ON when configuring.
if(HAL_ENABLE_UNITY)
    list(APPEND UTIL_SOURCES ${SRC}/utils/unity.c)
endif()

add_library(JaszczurHAL STATIC
    ${STM32_IMPL_SOURCES}
    ${HAL_COMMON_SOURCES}
    ${UTIL_SOURCES}
    "${JH_BOARD_GENERATED_DIR}/jh_link_contract_definition.c"
)

set_source_files_properties(${STM32_LITTLEFS_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-w"
)

target_include_directories(JaszczurHAL PUBLIC
    ${SRC}
    ${JH_BOARD_GENERATED_DIR}
    ${STM32_LITTLEFS_INCLUDE_DIRS}
    ${STM32_MQTT_INCLUDE_DIRS}
    ${_jh_stm32_bearssl_include_dirs}
    ${_jh_stm32_framework_include_dirs}
)
add_custom_command(TARGET JaszczurHAL POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_BINARY_DIR}/include/generated"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${JH_BOARD_GENERATED_DIR}/jh_board_config.h"
        "${JH_BOARD_GENERATED_DIR}/jh_board_registry.h"
        "${JH_BOARD_GENERATED_DIR}/jh_link_contract.h"
        "${CMAKE_BINARY_DIR}/include/generated"
    VERBATIM)

set(_jh_extra_hal_defines ${JH_BOARD_COMPILE_DEFINITIONS})
if(DEFINED EXTRA_HAL_DEFINES)
    list(APPEND _jh_extra_hal_defines ${EXTRA_HAL_DEFINES})
endif()

jh_cmake_defines_contain(_jh_stm32_has_littlefs HAL_ENABLE_LITTLEFS ${_jh_extra_hal_defines})
jh_cmake_defines_contain(_jh_stm32_has_littlefs_size HAL_STM32_FLASH_LITTLEFS_SIZE ${_jh_extra_hal_defines})
set(_jh_stm32_littlefs_link_size "")
if(_jh_stm32_has_littlefs)
    if(NOT _jh_stm32_has_littlefs_size)
        list(APPEND _jh_extra_hal_defines HAL_STM32_FLASH_LITTLEFS_SIZE=65536u)
        set(_jh_stm32_littlefs_link_size "65536")
    else()
        foreach(_def IN LISTS _jh_extra_hal_defines)
            if("${_def}" MATCHES "^HAL_STM32_FLASH_LITTLEFS_SIZE=(.+)$")
                set(_jh_stm32_littlefs_link_size "${CMAKE_MATCH_1}")
                string(REGEX REPLACE "[uUlL]+$" "" _jh_stm32_littlefs_link_size "${_jh_stm32_littlefs_link_size}")
            endif()
        endforeach()
    endif()
endif()

# Default STM32G474 profile for the initial skeleton backend.
#
# Opt-in flag model: modules are OFF by default; only the bus / UART /
# system-time pieces and shared HAL-level drivers that the STM32 backend
# currently provides are
# enabled here. Application projects can enable extra modules via
# EXTRA_HAL_DEFINES (e.g. -DEXTRA_HAL_DEFINES=HAL_ENABLE_MCP2515) once their
# STM32G474 backend lands.
target_compile_definitions(JaszczurHAL PUBLIC
    HAL_TARGET_STM32G474=1   # exact backend selection (see src/hal/hal_target.h)
    JH_TARGET_STM32G474=1    # legacy alias, kept until all refs migrate
    HAL_ENABLE_I2C
    HAL_ENABLE_I2C_SLAVE
    HAL_ENABLE_SPI
    HAL_ENABLE_MCP2515
    HAL_ENABLE_UART
    HAL_ENABLE_DAC
    HAL_ENABLE_DACLESS   # shared DACless PWM-audio engine over HAL DMA/PWM/ADC
    HAL_ENABLE_PCNT
    HAL_ENABLE_MCP401X   # propagates HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C
    HAL_ENABLE_MAX5395   # second digipot backend
    HAL_ENABLE_MCP9600   # shared MCP9600/MCP9601 thermocouple backend over HAL I2C
    HAL_ENABLE_MAX6675   # shared MAX6675 thermocouple backend over HAL GPIO
    HAL_ENABLE_BH1750    # shared BH1750 ambient-light sensor over HAL I2C
    HAL_ENABLE_TSC2007   # shared TSC2007 touch controller over HAL I2C
    HAL_ENABLE_STMPE610  # shared STMPE610 touch controller over HAL I2C/SPI
    HAL_ENABLE_IRSMALL_DECODER  # shared IR receiver decoder over HAL GPIO interrupts
    HAL_ENABLE_EXTERNAL_ADC  # shared ADS1X15/ADS1115 backend over HAL I2C
    HAL_ENABLE_DS18B20   # shared DS18B20 backend over HAL GPIO OneWire
    HAL_ENABLE_DHT       # shared DHT11/DHT22 GPIO temperature/humidity driver
    HAL_ENABLE_GPS       # portable NMEA engine; transport = UART on this target
    HAL_ENABLE_HD44780  # shared HD44780 character LCD over HAL GPIO
)

if(_jh_extra_hal_defines)
    target_compile_definitions(JaszczurHAL PUBLIC ${_jh_extra_hal_defines})
endif()

jh_cmake_defines_contain(_jh_stm32_has_cyw43_gspi HAL_CYW43_BUS_STM32_GSPI
    ${_jh_extra_hal_defines})
if(_jh_stm32_has_cyw43_gspi)
    jh_cmake_defines_contain(_jh_stm32_has_cyw43_lwip HAL_CYW43_STACK_LWIP
        ${_jh_extra_hal_defines})
    jh_cmake_defines_contain(_jh_stm32_has_bluetooth_stage1
        JH_BLUETOOTH_STAGE1_PROBE ${_jh_extra_hal_defines})
    jh_cmake_defines_contain(_jh_stm32_has_ble
        HAL_ENABLE_BLE ${_jh_extra_hal_defines})
    jh_cmake_defines_contain(_jh_stm32_has_ble_stream
        HAL_ENABLE_BLE_STREAM ${_jh_extra_hal_defines})
    # hal_config.h propagates the stream flag to BLE; mirror it for source
    # selection.
    if(_jh_stm32_has_ble_stream)
        set(_jh_stm32_has_ble TRUE)
    endif()
    if(_jh_stm32_has_bluetooth_stage1 AND _jh_stm32_has_ble)
        message(FATAL_ERROR
            "Select either JH_BLUETOOTH_STAGE1_PROBE or HAL_ENABLE_BLE")
    endif()
    set(_jh_stm32_cyw43_options)
    if(_jh_stm32_has_cyw43_lwip)
        list(APPEND _jh_stm32_cyw43_options LWIP)
    endif()
    if(_jh_stm32_has_bluetooth_stage1 OR _jh_stm32_has_ble)
        list(APPEND _jh_stm32_cyw43_options BLUETOOTH)
    endif()
    jh_target_enable_cyw43_driver(JaszczurHAL ${_jh_stm32_cyw43_options})
    if(_jh_stm32_has_bluetooth_stage1)
        jh_target_enable_btstack_stage1(JaszczurHAL)
    elseif(_jh_stm32_has_ble_stream)
        jh_target_enable_btstack_ble_stream(JaszczurHAL)
    elseif(_jh_stm32_has_ble)
        jh_target_enable_btstack_ble(JaszczurHAL)
    endif()
endif()

jh_cmake_defines_contain(_jh_stm32_has_tls HAL_ENABLE_TLS
    ${_jh_extra_hal_defines})
if(_jh_stm32_has_tls)
    jh_add_bearssl_source_library(jh_bearssl_stm32)
    target_link_libraries(JaszczurHAL PUBLIC jh_bearssl_stm32)
endif()

if(DEFINED HAL_PROJECT_CONFIG_DIR)
    target_include_directories(JaszczurHAL PUBLIC ${HAL_PROJECT_CONFIG_DIR})
endif()

if(DEFINED JH_STM32_FREERTOS)
    message(FATAL_ERROR
        "JH_STM32_FREERTOS has been removed as a user-facing switch. "
        "Use -DEXTRA_HAL_DEFINES=HAL_ENABLE_FREERTOS instead.")
endif()

set(_jh_stm32_freertos_enabled FALSE)
if(DEFINED EXTRA_HAL_DEFINES)
    jh_cmake_defines_contain(_jh_extra_has_freertos HAL_ENABLE_FREERTOS ${EXTRA_HAL_DEFINES})
    if(_jh_extra_has_freertos)
        set(_jh_stm32_freertos_enabled TRUE)
    endif()
endif()

if(_jh_stm32_freertos_enabled)
    jh_stm32g474_enable_freertos(JaszczurHAL)
endif()

if(NOT "${_jh_stm32_littlefs_link_size}" STREQUAL "")
    target_link_options(JaszczurHAL PUBLIC
        "-Wl,--defsym=HAL_STM32_FLASH_LITTLEFS_SIZE=${_jh_stm32_littlefs_link_size}"
    )
endif()

set(_common_flags
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Wall
    -Wextra
    -Werror=return-type
    -pipe
)

target_compile_options(JaszczurHAL PRIVATE
    ${_common_flags}
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
)

install(TARGETS JaszczurHAL ARCHIVE DESTINATION lib)
install(DIRECTORY ${SRC}/ DESTINATION include
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "impl/.mock" EXCLUDE)
