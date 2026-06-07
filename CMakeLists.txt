cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# ── Valgrind / CTest memcheck configuration ───────────────────────────────────
# Host-only dynamic analysis for the unit-test suite (NOT firmware - Valgrind
# cannot run on MCU targets). Run locally or in CI with:
#
#     ctest --test-dir build -T memcheck --output-on-failure
#
# These variables must be set *before* include(CTest) so the CTest module bakes
# them into DartConfiguration.tcl. --error-exitcode=1 makes Valgrind fail the
# individual test (and therefore the whole ctest run) on a real defect, so the
# CI memcheck job goes red instead of silently passing.
find_program(MEMORYCHECK_COMMAND NAMES valgrind)
set(MEMORYCHECK_COMMAND_OPTIONS
    "--leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --track-origins=yes --error-exitcode=1"
    CACHE STRING "Flags passed to Valgrind by 'ctest -T memcheck'")
set(MEMORYCHECK_SUPPRESSIONS_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind.supp"
    CACHE FILEPATH "Valgrind suppressions for known third-party/system noise")

# Include CTest to generate standard test metadata (including
# DartConfiguration.tcl) and keep test tooling output clean.
include(CTest)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ── Mock implementations ───────────────────────────────────────────────────────
file(GLOB MOCK_SOURCES ${SRC}/hal/impl/.mock/*.cpp)

# ── Utility sources linked into the shared mock test library ──────────────────
# Excluded sources are compiled by dedicated tests when they need local stubs or
# extra fixtures.
set(UTIL_SOURCES
    ${SRC}/hal/hal_config.cpp
    ${SRC}/hal/hal_can_util.cpp
    ${SRC}/hal/hal_crypto.cpp
    ${SRC}/hal/hal_pga2311.cpp
    ${SRC}/hal/impl/shared/wireguard/crypto/blake2s.c
    ${SRC}/hal/impl/shared/wireguard/crypto/chacha20.c
    ${SRC}/hal/impl/shared/wireguard/crypto/chacha20poly1305.c
    ${SRC}/hal/impl/shared/wireguard/crypto/crypto.c
    ${SRC}/hal/impl/shared/wireguard/crypto/poly1305-donna.c
    ${SRC}/hal/impl/shared/wireguard/crypto/x25519.c
    ${SRC}/hal/hal_digipot.cpp
    ${SRC}/hal/impl/shared/digipot/digipot_mcp401x.cpp
    ${SRC}/hal/impl/shared/digipot/digipot_max5395.cpp
    ${SRC}/hal/impl/shared/pga2311/pga2311_driver.cpp
    ${SRC}/hal/hal_kv.cpp
    ${SRC}/hal/hal_modem_at.cpp
    ${SRC}/hal/hal_simcom_a76xx.cpp
    ${SRC}/hal/hal_timer_ext.cpp
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

target_include_directories(hal_mock PUBLIC
    ${SRC}
    ${SRC}/arduino_host_stubs
)

target_compile_definitions(hal_mock PUBLIC
    HAL_TARGET_MOCK         # canonical backend selection (see src/hal/hal_target.h)
    UNITY_INCLUDE_CONFIG_H  # use the project's unity_config.h
    HAL_DISPLAY_ILI9341     # explicit TFT driver choice for host tests
    # Opt-in modules: host tests exercise the full HAL surface, so we
    # enable every module here. Facade modules pull in a backend so
    # the consistency checks in hal_config.h are satisfied.
    HAL_ENABLE_WIFI
    HAL_ENABLE_TIME
    HAL_ENABLE_MQTT
    HAL_ENABLE_UDP
    HAL_ENABLE_OTA
    HAL_ENABLE_WIREGUARD
    HAL_ENABLE_A7670          # propagates HAL_ENABLE_CELLULAR_MODEM + HAL_ENABLE_UART
    HAL_ENABLE_EEPROM
    HAL_ENABLE_KV
    HAL_ENABLE_LITTLEFS
    HAL_ENABLE_SDLOGGER
    HAL_ENABLE_UART
    HAL_ENABLE_SWSERIAL
    HAL_ENABLE_I2C
    HAL_ENABLE_I2C_SLAVE
    HAL_ENABLE_SPI
    HAL_ENABLE_CAN
    HAL_ENABLE_PCF8563        # propagates HAL_ENABLE_RTC + HAL_ENABLE_I2C
    HAL_ENABLE_DS3231         # second RTC backend
    HAL_ENABLE_MCP9600        # propagates HAL_ENABLE_THERMOCOUPLE + HAL_ENABLE_I2C
    HAL_ENABLE_MAX6675        # second thermocouple backend
    HAL_ENABLE_DS18B20        # propagates HAL_ENABLE_ONEWIRE
    HAL_ENABLE_EXTERNAL_ADC   # propagates HAL_ENABLE_I2C
    HAL_ENABLE_MCP401X        # propagates HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C
    HAL_ENABLE_MAX5395        # second digipot backend
    HAL_ENABLE_PGA2311        # shared SPI stereo volume controller
    HAL_ENABLE_GPS            # portable NMEA engine (transport: SWSERIAL/UART, both on below)
    HAL_ENABLE_DAC
    HAL_ENABLE_PCNT
    HAL_ENABLE_PWM_FREQ
    HAL_ENABLE_RGB_LED
    HAL_ENABLE_ILI9341        # propagates HAL_ENABLE_TFT + HAL_ENABLE_DISPLAY
    HAL_ENABLE_SSD1306        # OLED backend
    HAL_ENABLE_CRYPTO
    HAL_ENABLE_CJSON
    HAL_ENABLE_UNITY
)

# std::mutex on Linux requires pthreads
find_package(Threads REQUIRED)
target_link_libraries(hal_mock PUBLIC Threads::Threads)

target_compile_options(hal_mock PRIVATE -Wall -Wextra)

# ── Tests ─────────────────────────────────────────────────────────────────────
add_subdirectory(tests)
