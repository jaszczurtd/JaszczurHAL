cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_STM32G474 C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# ── HAL implementation sources (STM32G474 skeleton) ────────────────────────
file(GLOB STM32_IMPL_SOURCES ${SRC}/hal/impl/stm32g474/*.cpp)

# ── Common HAL sources (backend-agnostic wrappers) ──────────────────────────
set(HAL_COMMON_SOURCES
    ${SRC}/hal/hal_config.cpp
    ${SRC}/hal/hal_can_util.cpp
    ${SRC}/hal/hal_kv.cpp
    ${SRC}/hal/hal_soft_timer.cpp
    ${SRC}/hal/hal_pid_controller.cpp
)

# ── Utility sources safe outside Arduino ────────────────────────────────────
set(UTIL_SOURCES
    ${SRC}/utils/SmartTimers.cpp
    ${SRC}/utils/pidController.cpp
    ${SRC}/utils/draw7Segment.cpp
    ${SRC}/utils/cJSON.c
    ${SRC}/utils/cJSON_Utils.c
)

if(NOT HAL_DISABLE_UNITY)
    list(APPEND UTIL_SOURCES ${SRC}/utils/unity.c)
endif()

add_library(JaszczurHAL STATIC
    ${STM32_IMPL_SOURCES}
    ${HAL_COMMON_SOURCES}
    ${UTIL_SOURCES}
)

target_include_directories(JaszczurHAL PUBLIC ${SRC})

# Default STM32G474 profile for the initial skeleton backend.
target_compile_definitions(JaszczurHAL PUBLIC
    JH_TARGET_STM32G474=1
    HAL_DISABLE_WIFI
    HAL_DISABLE_TIME
    HAL_DISABLE_EEPROM
    HAL_DISABLE_GPS
    HAL_DISABLE_THERMOCOUPLE
    HAL_DISABLE_SWSERIAL
    HAL_DISABLE_I2C_SLAVE
    HAL_DISABLE_EXTERNAL_ADC
    HAL_DISABLE_PWM_FREQ
    HAL_DISABLE_RGB_LED
    HAL_DISABLE_CAN
    HAL_DISABLE_DISPLAY
    HAL_DISABLE_UNITY
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
