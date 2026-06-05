cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_STM32G474 C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# ── HAL implementation sources (STM32G474 skeleton) ────────────────────────
file(GLOB STM32_IMPL_SOURCES ${SRC}/hal/impl/stm32g474/*.cpp)
# SoC-specific drivers under impl/stm32g474/drivers/<soc>/
file(GLOB STM32_DRIVER_SOURCES ${SRC}/hal/impl/stm32g474/drivers/*/*.cpp)
list(APPEND STM32_IMPL_SOURCES ${STM32_DRIVER_SOURCES})
# Backend-agnostic implementations shared with the RP2040 backend
# (e.g. the portable NMEA GPS engine + parser).
file(GLOB_RECURSE STM32_SHARED_SOURCES ${SRC}/hal/impl/shared/*.cpp)
list(APPEND STM32_IMPL_SOURCES ${STM32_SHARED_SOURCES})

# ── Common HAL sources (backend-agnostic wrappers) ──────────────────────────
set(HAL_COMMON_SOURCES
    ${SRC}/hal/hal_config.cpp
    ${SRC}/hal/hal_can_util.cpp
    ${SRC}/hal/hal_crypto.cpp
    ${SRC}/hal/hal_digipot.cpp
    ${SRC}/hal/hal_kv.cpp
    ${SRC}/hal/hal_timer_ext.cpp
    ${SRC}/hal/hal_soft_timer.cpp
    ${SRC}/hal/hal_pid_controller.cpp
    ${SRC}/hal_app_entry.cpp
)

# ── Utility sources safe outside Arduino ────────────────────────────────────
set(UTIL_SOURCES
    ${SRC}/utils/SmartTimers.cpp
    ${SRC}/utils/pidController.cpp
    ${SRC}/utils/draw7Segment.cpp
    ${SRC}/utils/cJSON.c
    ${SRC}/utils/cJSON_Utils.c
)

# Unity is opt-in: enable it via -DHAL_ENABLE_UNITY=ON when configuring.
if(HAL_ENABLE_UNITY)
    list(APPEND UTIL_SOURCES ${SRC}/utils/unity.c)
endif()

add_library(JaszczurHAL STATIC
    ${STM32_IMPL_SOURCES}
    ${HAL_COMMON_SOURCES}
    ${UTIL_SOURCES}
)

target_include_directories(JaszczurHAL PUBLIC
    ${SRC}
    ${SRC}/arduino_host_stubs
)

# Default STM32G474 profile for the initial skeleton backend.
#
# Opt-in flag model: modules are OFF by default; only the bus / UART /
# system-time pieces and shared HAL-level drivers that the STM32 backend
# currently provides are
# enabled here. Application projects can enable extra modules via
# EXTRA_HAL_DEFINES (e.g. -DEXTRA_HAL_DEFINES=HAL_ENABLE_CAN) once their
# STM32G474 backend lands.
target_compile_definitions(JaszczurHAL PUBLIC
    HAL_TARGET_STM32G474=1   # canonical backend selection (see src/hal/hal_target.h)
    JH_TARGET_STM32G474=1    # legacy alias, kept until all refs migrate
    HAL_ENABLE_I2C
    HAL_ENABLE_SPI
    HAL_ENABLE_CAN
    HAL_ENABLE_UART
    HAL_ENABLE_DAC
    HAL_ENABLE_PCNT
    HAL_ENABLE_MCP401X   # propagates HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C
    HAL_ENABLE_MAX5395   # second digipot backend
    HAL_ENABLE_MCP9600   # shared MCP9600/MCP9601 thermocouple backend over HAL I2C
    HAL_ENABLE_MAX6675   # shared MAX6675 thermocouple backend over HAL GPIO
    HAL_ENABLE_EXTERNAL_ADC  # shared ADS1X15/ADS1115 backend over HAL I2C
    HAL_ENABLE_DS18B20   # shared DS18B20 backend over HAL GPIO OneWire
    HAL_ENABLE_GPS       # portable NMEA engine; transport = UART on this target
)

if(DEFINED EXTRA_HAL_DEFINES)
    target_compile_definitions(JaszczurHAL PUBLIC ${EXTRA_HAL_DEFINES})
endif()

if(DEFINED HAL_PROJECT_CONFIG_DIR)
    target_include_directories(JaszczurHAL PUBLIC ${HAL_PROJECT_CONFIG_DIR})
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
