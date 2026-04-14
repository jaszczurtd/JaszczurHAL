cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# Include CTest to generate standard test metadata (including
# DartConfiguration.tcl) and keep test tooling output clean.
include(CTest)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ── Mock implementations ───────────────────────────────────────────────────────
file(GLOB MOCK_SOURCES ${SRC}/hal/impl/.mock/*.cpp)

# ── Utility sources compilable without Arduino ────────────────────────────────
# Excluded: tools.cpp (File/SPI), multicoreWatchdog.cpp,
#           utils/draw7Segment.cpp (#include <Arduino.h>)
set(UTIL_SOURCES
    ${SRC}/hal/hal_config.cpp
    ${SRC}/hal/hal_can_util.cpp
    ${SRC}/hal/hal_kv.cpp
    ${SRC}/hal/hal_soft_timer.cpp
    ${SRC}/utils/SmartTimers.cpp
    ${SRC}/utils/pidController.cpp
)

# ── hal_mock static library ───────────────────────────────────────────────────
add_library(hal_mock STATIC
    ${MOCK_SOURCES}
    ${UTIL_SOURCES}
    ${SRC}/utils/unity.c
)

target_include_directories(hal_mock PUBLIC ${SRC})

target_compile_definitions(hal_mock PUBLIC
    UNITY_INCLUDE_CONFIG_H  # use the project's unity_config.h
    HAL_DISPLAY_ILI9341     # explicit TFT driver choice for host tests
)

# std::mutex on Linux requires pthreads
find_package(Threads REQUIRED)
target_link_libraries(hal_mock PUBLIC Threads::Threads)

target_compile_options(hal_mock PRIVATE -Wall -Wextra)

# ── Tests ─────────────────────────────────────────────────────────────────────
add_subdirectory(tests)
