cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# ── Valgrind / CTest memcheck configuration ───────────────────────────────────
# Host-only dynamic analysis for the unit-test suite (NOT firmware - Valgrind
# cannot run on MCU targets). Run locally or in CI with:
#
#     ctest --test-dir .build/host -T memcheck --output-on-failure
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
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/jh_managed_frameworks.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/jh_host_warnings.cmake)
jh_managed_framework_include_dirs(JH_MANAGED_FRAMEWORK_INCLUDE_DIRS)
jh_managed_framework_configure_sources()

option(JH_ENABLE_FREERTOS_POSIX_TESTS
    "Build host FreeRTOS POSIX scheduler regression tests"
    OFF)
if(WIN32)
    if(JH_ENABLE_FREERTOS_POSIX_TESTS)
        message(STATUS
            "FreeRTOS GCC/Posix tests requested on Windows; disabling them "
            "because the upstream host port requires POSIX APIs.")
    else()
        message(STATUS
            "FreeRTOS GCC/Posix tests: disabled on native Windows hosts.")
    endif()
    set(JH_ENABLE_FREERTOS_POSIX_TESTS OFF CACHE BOOL
        "Build host FreeRTOS POSIX scheduler regression tests" FORCE)
endif()

# ── Mock implementations ───────────────────────────────────────────────────────
file(GLOB MOCK_SOURCES ${SRC}/hal/impl/.mock/*.cpp)

# ── Utility sources linked into the shared mock test library ──────────────────
# Excluded sources are compiled by dedicated tests when they need local stubs or
# extra fixtures.
set(UTIL_SOURCES
    ${SRC}/hal/hal_assert.cpp
    ${SRC}/hal/hal_board.cpp
    ${SRC}/hal/hal_ble.cpp
    ${SRC}/hal/hal_config.cpp
    ${SRC}/hal/hal_network_status.cpp
    ${SRC}/hal/hal_tls.cpp
    ${SRC}/hal/impl/shared/network/http/hal_http_client.cpp
    ${SRC}/hal/impl/shared/network/jh_network_backend.cpp
    ${SRC}/hal/impl/shared/network/jh_network_handle_pool.cpp
    ${SRC}/hal/impl/shared/network/jh_public_network_backend_adapter.cpp
    ${SRC}/hal/impl/shared/network/ota/jh_ota_image.cpp
    ${SRC}/hal/impl/shared/network/ota/jh_ota_swap_engine.cpp
    ${SRC}/hal/hal_can_util.cpp
    ${SRC}/hal/impl/shared/frameworks/BearSSL/jh_bearssl_engine.cpp
    ${SRC}/hal/impl/shared/frameworks/BearSSL/jh_bearssl_hal_tcp_io.cpp
    ${SRC}/hal/impl/shared/frameworks/BearSSL/jh_bearssl_provider.cpp
    ${SRC}/hal/impl/shared/network/services/http_server/hal_http_server.cpp
    ${SRC}/hal/impl/shared/network/services/http_files/hal_http_files.cpp
    ${SRC}/hal/impl/shared/network/services/websocket/hal_websocket.cpp
    ${SRC}/hal/impl/shared/network/services/net_console/hal_net_console.cpp
    ${SRC}/hal/impl/shared/network/services/net_commands/hal_net_commands.cpp
    ${SRC}/hal/impl/shared/debug/hal_debug_format.cpp
    ${SRC}/hal/impl/shared/drivers/mcp2515/hal_can_mcp2515_config.cpp
    ${SRC}/hal/impl/shared/drivers/mcp251xfd/mcp251xfd_driver.cpp
    ${SRC}/hal/hal_crypto.cpp
    ${SRC}/hal/hal_crc.cpp
    ${SRC}/hal/hal_pga2311.cpp
    ${SRC}/hal/impl/shared/frameworks/wireguard/crypto/blake2s.c
    ${SRC}/hal/impl/shared/frameworks/wireguard/crypto/chacha20.c
    ${SRC}/hal/impl/shared/frameworks/wireguard/crypto/chacha20poly1305.c
    ${SRC}/hal/impl/shared/frameworks/wireguard/crypto/crypto.c
    ${SRC}/hal/impl/shared/frameworks/wireguard/crypto/poly1305-donna.c
    ${SRC}/hal/impl/shared/frameworks/wireguard/crypto/x25519.c
    ${SRC}/hal/hal_digipot.cpp
    ${SRC}/hal/impl/shared/drivers/digipot/digipot_mcp401x.cpp
    ${SRC}/hal/impl/shared/drivers/digipot/digipot_max5395.cpp
    ${SRC}/hal/impl/shared/drivers/pga2311/pga2311_driver.cpp
    ${SRC}/hal/impl/shared/drivers/mfrc522/mfrc522.cpp
    ${SRC}/hal/impl/shared/drivers/mfrc522/mfrc522_spi.cpp
    ${SRC}/hal/impl/shared/drivers/mfrc522/mfrc522_i2c.cpp
    ${SRC}/hal/impl/shared/drivers/pn532/pn532.cpp
    ${SRC}/hal/impl/shared/drivers/pn532/pn532_spi.cpp
    ${SRC}/hal/impl/shared/drivers/pn532/pn532_i2c.cpp
    ${SRC}/hal/impl/shared/drivers/pn532/pn532_uart.cpp
    ${SRC}/hal/impl/shared/drivers/simple_io/hal_simple_io_drivers.cpp
    ${SRC}/hal/impl/shared/drivers/dacless/dacless.cpp
    ${SRC}/hal/impl/shared/drivers/dht/hal_dht.cpp
    ${SRC}/hal/impl/shared/drivers/adp5360/hal_adp5360.cpp
    ${SRC}/hal/impl/shared/drivers/cyw43-driver/jh_cyw43_driver.cpp
    ${SRC}/hal/impl/shared/drivers/cyw43-driver/jh_cyw43_gspi_transport.cpp
    ${SRC}/hal/impl/shared/drivers/cyw43-driver/jh_cyw43_lwip.cpp
    ${SRC}/hal/impl/shared/drivers/cyw43-driver/jh_cyw43_radio.cpp
    ${SRC}/hal/impl/shared/drivers/cyw43-driver/jh_cyw43_radio_runtime.cpp
    ${SRC}/hal/impl/shared/drivers/swserial/hal_swserial.cpp
    ${SRC}/hal/impl/shared/drivers/flash/jh_flash_transaction_engine.cpp
    ${SRC}/hal/hal_kv.cpp
    ${SRC}/hal/hal_modem_at.cpp
    ${SRC}/hal/hal_simcom_a76xx.cpp
    ${SRC}/hal/hal_timer_ext.cpp
    ${SRC}/hal/hal_soft_timer.cpp
    ${SRC}/hal/impl/shared/frameworks/smart_timers/SmartTimers.cpp
    ${SRC}/utils/pidController.cpp
    ${SRC}/hal/impl/shared/frameworks/cjson/cJSON.c
    ${SRC}/hal/impl/shared/frameworks/cjson/cJSON_Utils.c
    ${SRC}/hal/impl/shared/frameworks/lodepng/lodepng.cpp
    ${SRC}/hal/impl/shared/frameworks/jpeg/tjpgd.c
)

# ── hal_mock static library ───────────────────────────────────────────────────
add_library(hal_mock STATIC
    ${MOCK_SOURCES}
    ${UTIL_SOURCES}
    ${SRC}/utils/unity.c
)
if(NOT WIN32)
    target_sources(hal_mock PRIVATE
        ${SRC}/hal/impl/shared/network/adapters/bsd/hal_bsd_sockets.cpp
        ${SRC}/hal/impl/shared/frameworks/BearSSL/jh_bearssl_bsd_io.cpp
    )
endif()

target_include_directories(hal_mock PUBLIC
    ${SRC}
    ${JH_MANAGED_FRAMEWORK_INCLUDE_DIRS}
)

target_compile_definitions(hal_mock PUBLIC
    HAL_TARGET_MOCK         # base backend selection (see src/hal/hal_target.h)
    UNITY_INCLUDE_CONFIG_H  # use the project's unity_config.h
    HAL_DISPLAY_ILI9341     # explicit TFT driver choice for host tests
    # Opt-in modules: host tests exercise the full HAL surface, so we
    # enable every module here. Facade modules pull in a backend so
    # the consistency checks in hal_config.h are satisfied.
    HAL_ENABLE_WIFI
    HAL_ENABLE_BLE
    HAL_ENABLE_TIME
    HAL_ENABLE_MQTT
    HAL_ENABLE_UDP
    HAL_ENABLE_TCP            # handle-based TCP client/listener transport
    HAL_ENABLE_HTTP_SERVER    # small HTTP/1.1 server over HAL TCP
    HAL_ENABLE_HTTP_FILES     # static file serving/upload helpers over HTTP
    HAL_ENABLE_WEBSOCKET      # small WebSocket server over HAL TCP
    HAL_ENABLE_NET_CONSOLE    # password-protected serial/debug mirror over TCP
    HAL_ENABLE_NET_COMMANDS   # HTTP/WebSocket JSON/text command dispatch
    HAL_ENABLE_TLS            # portable TLS API + private BearSSL provider
    HAL_ENABLE_HTTP_CLIENT    # portable HTTP/HTTPS client
    HAL_ENABLE_OTA
    HAL_ENABLE_WIREGUARD
    HAL_ENABLE_A7670          # propagates HAL_ENABLE_CELLULAR_MODEM + HAL_ENABLE_UART
    HAL_ENABLE_EEPROM
    HAL_ENABLE_KV
    HAL_ENABLE_LITTLEFS
    HAL_ENABLE_FAT
    HAL_ENABLE_SDLOGGER
    HAL_ENABLE_UART
    HAL_ENABLE_SWSERIAL
    HAL_ENABLE_I2C
    HAL_ENABLE_I2C_SLAVE
    HAL_ENABLE_SPI
    HAL_ENABLE_MCP2515       # propagates HAL_ENABLE_CAN + HAL_ENABLE_SPI
    HAL_ENABLE_MCP251XFD     # second CAN backend with CAN FD support
    HAL_ENABLE_PCF8563        # propagates HAL_ENABLE_RTC + HAL_ENABLE_I2C
    HAL_ENABLE_DS3231         # second RTC backend
    HAL_ENABLE_MCP9600        # propagates HAL_ENABLE_THERMOCOUPLE + HAL_ENABLE_I2C
    HAL_ENABLE_MAX6675        # second thermocouple backend
    HAL_ENABLE_DS18B20        # propagates HAL_ENABLE_ONEWIRE
    HAL_ENABLE_CRC            # generic CRC-8/16/32 (also auto-enabled by ONEWIRE)
    HAL_ENABLE_DHT            # shared DHT11/DHT22 GPIO temperature/humidity driver
    HAL_ENABLE_BH1750         # shared ambient-light sensor over HAL I2C
    HAL_ENABLE_ADP5360        # shared ADP5360 PMIC over HAL I2C
    HAL_ENABLE_MCP3221        # shared MCP3221 12-bit ADC over HAL I2C
    HAL_ENABLE_MCP23017       # shared MCP23017 GPIO expander over HAL I2C
    HAL_ENABLE_PCA9654E       # shared PCA9654E output expander over HAL I2C
    HAL_ENABLE_PCF8574        # shared PCF8574 GPIO expander over HAL I2C
    HAL_ENABLE_HC595          # shared 74HC595 output expander over HAL SPI
    HAL_ENABLE_MCP4725        # shared MCP4725 12-bit DAC over HAL I2C
    HAL_ENABLE_TSC2007        # shared TSC2007 touch controller over HAL I2C
    HAL_ENABLE_STMPE610       # shared STMPE610 touch controller over HAL I2C/SPI
    HAL_ENABLE_IRSMALL_DECODER # shared IR receiver decoder over HAL GPIO interrupts
    HAL_ENABLE_EXTERNAL_ADC   # propagates HAL_ENABLE_I2C
    HAL_ENABLE_MCP401X        # propagates HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C
    HAL_ENABLE_MAX5395        # second digipot backend
    HAL_ENABLE_PGA2311        # shared SPI stereo volume controller
    HAL_ENABLE_MFRC522        # shared MFRC522 RFID reader over HAL SPI/I2C
    HAL_ENABLE_PN532          # shared PN532 NFC/RFID reader over HAL SPI/I2C/UART
    HAL_ENABLE_GPS            # portable NMEA engine (transport: SWSERIAL/UART, both on below)
    HAL_ENABLE_DAC
    HAL_ENABLE_DACLESS       # shared DACless PWM-audio driver
    HAL_ENABLE_DMA_PWM_AUDIO
    HAL_ENABLE_PCNT
    HAL_ENABLE_PWM_FREQ
    HAL_ENABLE_RGB_LED
    HAL_ENABLE_HD44780        # shared HD44780 character LCD over HAL GPIO
    HAL_ENABLE_ILI9341        # propagates HAL_ENABLE_TFT + HAL_ENABLE_DISPLAY
    HAL_ENABLE_SSD1306        # OLED backend
    HAL_ENABLE_CRYPTO
    HAL_ENABLE_CJSON
    HAL_ENABLE_PNG
    HAL_ENABLE_PNG_AS_BASE64
    HAL_ENABLE_JPEG
    HAL_ENABLE_JPEG_AS_BASE64
    HAL_ENABLE_UNITY
)
if(NOT WIN32)
    target_compile_definitions(hal_mock PUBLIC HAL_ENABLE_BSD_SOCKETS)
endif()

# std::mutex on Linux requires pthreads
find_package(Threads REQUIRED)
target_link_libraries(hal_mock PUBLIC Threads::Threads)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/jh_bearssl.cmake)
jh_add_bearssl_source_library(jh_bearssl_host)
target_link_libraries(hal_mock PUBLIC jh_bearssl_host)

jh_enable_host_warnings(hal_mock)

# ── Host FreeRTOS POSIX test library ──────────────────────────────────────────
if(JH_ENABLE_FREERTOS_POSIX_TESTS)
    include(${CMAKE_CURRENT_SOURCE_DIR}/stm32_lib/freertos_stm32g474.cmake)
    jh_ensure_freertos_kernel()

    set(JH_FREERTOS_POSIX_CONFIG_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/freertos_posix)
    set(JH_FREERTOS_POSIX_PORT_DIR
        ${JH_FREERTOS_KERNEL_DIR}/portable/ThirdParty/GCC/Posix)

    set(FREERTOS_POSIX_KERNEL_SOURCES
        ${JH_FREERTOS_KERNEL_DIR}/tasks.c
        ${JH_FREERTOS_KERNEL_DIR}/queue.c
        ${JH_FREERTOS_KERNEL_DIR}/list.c
        ${JH_FREERTOS_KERNEL_DIR}/timers.c
        ${JH_FREERTOS_KERNEL_DIR}/event_groups.c
        ${JH_FREERTOS_KERNEL_DIR}/stream_buffer.c
        ${JH_FREERTOS_KERNEL_DIR}/portable/MemMang/heap_4.c
        ${JH_FREERTOS_POSIX_PORT_DIR}/port.c
        ${JH_FREERTOS_POSIX_PORT_DIR}/utils/wait_for_event.c
    )

    set(FREERTOS_POSIX_REQUIRED_PATHS
        ${JH_FREERTOS_KERNEL_DIR}/include/FreeRTOS.h
        ${JH_FREERTOS_KERNEL_DIR}/include/task.h
        ${JH_FREERTOS_KERNEL_DIR}/include/semphr.h
        ${JH_FREERTOS_POSIX_PORT_DIR}/portmacro.h
        ${JH_FREERTOS_POSIX_CONFIG_DIR}/FreeRTOSConfig.h
        ${FREERTOS_POSIX_KERNEL_SOURCES}
    )

    foreach(_path IN LISTS FREERTOS_POSIX_REQUIRED_PATHS)
        if(NOT EXISTS "${_path}")
            message(FATAL_ERROR
                "JH_ENABLE_FREERTOS_POSIX_TESTS requires a local FreeRTOS-Kernel "
                "with the GCC/Posix port. Missing: ${_path}")
        endif()
    endforeach()

    add_library(freertos_posix_kernel STATIC
        ${FREERTOS_POSIX_KERNEL_SOURCES}
    )
    target_include_directories(freertos_posix_kernel PUBLIC
        ${JH_FREERTOS_POSIX_CONFIG_DIR}
        ${JH_FREERTOS_KERNEL_DIR}/include
        ${JH_FREERTOS_POSIX_PORT_DIR}
    )
    target_link_libraries(freertos_posix_kernel PUBLIC Threads::Threads)
    set_source_files_properties(${FREERTOS_POSIX_KERNEL_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-w")

    add_library(hal_freertos_posix STATIC
        ${SRC}/hal/hal_assert.cpp
        ${SRC}/hal/impl/stm32g474/hal_sync.cpp
        ${SRC}/hal/impl/stm32g474/hal_system.cpp
        ${SRC}/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.cpp
        ${SRC}/hal/impl/stm32g474/drivers/stm32g474/stm32g474_system.cpp
        ${SRC}/hal/hal_config.cpp
        ${SRC}/hal/impl/shared/network/jh_network_service.cpp
        ${SRC}/hal/impl/shared/frameworks/smart_timers/SmartTimers.cpp
        ${SRC}/utils/unity.c
    )
    target_include_directories(hal_freertos_posix PUBLIC
        ${SRC}
    )
    target_compile_definitions(hal_freertos_posix PUBLIC
        HAL_TARGET_STM32G474=1
        JH_TARGET_STM32G474=1
        HAL_ENABLE_FREERTOS=1
        HAL_ENABLE_UNITY
        UNITY_INCLUDE_CONFIG_H
    )
    jh_enable_host_warnings(hal_freertos_posix)
    target_link_libraries(hal_freertos_posix PUBLIC freertos_posix_kernel)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
add_subdirectory(tests)
