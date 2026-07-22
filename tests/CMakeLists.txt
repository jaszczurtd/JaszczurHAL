# Helper: create a test executable linked against hal_mock
function(add_hal_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE hal_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

function(add_hal_freertos_posix_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE hal_freertos_posix)
    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES
        LABELS "freertos_posix;no_memcheck"
        TIMEOUT 20)
endfunction()

function(add_stm32_host_test NAME)
    set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    add_executable(${NAME}
        ${NAME}.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_timer.cpp
        ${SRC_DIR}/hal/hal_timer_ext.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_sync.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_system.cpp
        ${SRC_DIR}/hal/impl/stm32g474/drivers/stm32g474/stm32g474_system.cpp
        ${SRC_DIR}/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.cpp
        ${SRC_DIR}/hal/hal_config.cpp
        ${SRC_DIR}/utils/unity.c
    )
    target_include_directories(${NAME} PRIVATE
        ${SRC_DIR}
    )
    target_compile_definitions(${NAME} PRIVATE
        HAL_TARGET_STM32G474=1
        UNITY_INCLUDE_CONFIG_H
        HAL_ENABLE_UNITY
    )
    target_compile_options(${NAME} PRIVATE -Wall -Wextra)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

function(add_stm32_i2c_slave_host_test NAME)
    set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    add_executable(${NAME}
        ${NAME}.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_i2c_slave.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_sync.cpp
        ${SRC_DIR}/hal/hal_config.cpp
        ${SRC_DIR}/utils/unity.c
    )
    target_include_directories(${NAME} PRIVATE
        ${SRC_DIR}
    )
    target_compile_definitions(${NAME} PRIVATE
        HAL_TARGET_STM32G474=1
        HAL_ENABLE_I2C_SLAVE
        UNITY_INCLUDE_CONFIG_H
        HAL_ENABLE_UNITY
    )
    target_compile_options(${NAME} PRIVATE -Wall -Wextra)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

function(add_stm32_gpio_host_test NAME)
    set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    add_executable(${NAME}
        ${NAME}.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_gpio.cpp
        ${SRC_DIR}/hal/hal_config.cpp
        ${SRC_DIR}/utils/unity.c
    )
    target_include_directories(${NAME} PRIVATE
        ${SRC_DIR}
    )
    target_compile_definitions(${NAME} PRIVATE
        HAL_TARGET_STM32G474=1
        UNITY_INCLUDE_CONFIG_H
        HAL_ENABLE_UNITY
    )
    target_compile_options(${NAME} PRIVATE -Wall -Wextra)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

# Exercise the real Fiesta-convention CMake generator, not a handwritten copy.
# The secondary initializer must execute through Arduino setup1() on RP2040 so
# its GPIO/IRQ setup retains core-1 affinity.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_entry_adapter.cmake")
set(_entry_fixture_dir
    "${CMAKE_CURRENT_BINARY_DIR}/jh_entry_adapter_fixture")
set(_entry_generated_dir
    "${CMAKE_CURRENT_BINARY_DIR}/jh_entry_adapter_generated")
file(MAKE_DIRECTORY "${_entry_fixture_dir}")
file(WRITE "${_entry_fixture_dir}/firmware_entry.h" [=[
#pragma once
#define FIESTA_ENABLE_CORE1 1

#ifdef __cplusplus
extern "C" {
#endif
void initialization(void);
void initialization1(void);
void looper(void);
void looper1(void);
#ifdef __cplusplus
}
#endif
]=])

jh_generate_entry_adapter(
    "${_entry_fixture_dir}"
    "${_entry_generated_dir}"
    _entry_adapter_source
    _entry_adapter_has_core1
)
if(NOT _entry_adapter_source OR NOT _entry_adapter_has_core1)
    message(FATAL_ERROR "Core-1 entry adapter fixture was not generated")
endif()

add_executable(test_jh_entry_adapter
    test_jh_entry_adapter.cpp
    "${_entry_adapter_source}"
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_jh_entry_adapter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    "${_entry_fixture_dir}"
)
target_compile_definitions(test_jh_entry_adapter PRIVATE
    HAL_TARGET_RP2040=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_jh_entry_adapter PRIVATE -Wall -Wextra)
add_test(NAME test_jh_entry_adapter COMMAND test_jh_entry_adapter)

add_executable(test_lwip_raw_engines
    test_lwip_raw_engines.cpp
    fakes/lwip_fake.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/jh_lwip_udp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/jh_lwip_tcp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_lwip_raw_engines PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_lwip_raw_engines PRIVATE
    HAL_TARGET_MOCK=1
    JH_LWIP_RAW_TEST=1
    HAL_LWIP_UDP_RX_QUEUE_DEPTH=2
    HAL_LWIP_UDP_MAX_PAYLOAD=8
    HAL_LWIP_TCP_RX_LIMIT=8
    HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH=2
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_lwip_raw_engines PRIVATE -Wall -Wextra -Werror)
add_test(NAME test_lwip_raw_engines COMMAND test_lwip_raw_engines)

add_executable(test_network_service
    test_network_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/jh_network_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_network_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_network_service PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_network_service PRIVATE -Wall -Wextra -Werror)
add_test(NAME test_network_service COMMAND test_network_service)

add_executable(test_network_backend
    test_network_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/jh_network_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/jh_network_handle_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_network_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_network_backend PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_WIFI
    HAL_ENABLE_TCP
    HAL_ENABLE_UDP
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_network_backend PRIVATE -Wall -Wextra -Werror)
add_test(NAME test_network_backend COMMAND test_network_backend)

add_executable(test_network_backend_contract_host
    test_network_backend_contract.cpp
    fakes/network_contract_host_adapter.cpp
)
target_link_libraries(test_network_backend_contract_host PRIVATE hal_mock)
target_include_directories(test_network_backend_contract_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_options(test_network_backend_contract_host PRIVATE
    -Wall -Wextra -Werror
)
add_test(NAME test_network_backend_contract_host
    COMMAND test_network_backend_contract_host
)

add_executable(test_network_backend_contract_offload
    test_network_backend_contract.cpp
    fakes/network_contract_offload_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/jh_network_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/jh_network_handle_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_network_backend_contract_offload PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_network_backend_contract_offload PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_WIFI
    HAL_ENABLE_TCP
    HAL_ENABLE_UDP
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_network_backend_contract_offload PRIVATE
    -Wall -Wextra -Werror
)
add_test(NAME test_network_backend_contract_offload
    COMMAND test_network_backend_contract_offload
)

add_test(NAME test_network_backend_selection
    COMMAND ${CMAKE_COMMAND}
        -DJH_CXX=${CMAKE_CXX_COMPILER}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_network_backend_selection.cmake
)

add_library(test_hal_http_client_plaintext_compile OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/http/hal_http_client.cpp
)
target_include_directories(test_hal_http_client_plaintext_compile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_hal_http_client_plaintext_compile PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_HTTP_CLIENT=1
)
target_compile_options(test_hal_http_client_plaintext_compile PRIVATE
    -Wall -Wextra -Werror
)

add_executable(test_pubsub_hal_client
    test_pubsub_hal_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/mqtt/jh_pubsub_hal_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/rp2040/frameworks/PubSubClient/src/PubSubClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_pubsub_hal_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/arduino
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_pubsub_hal_client PRIVATE
    HAL_TARGET_RP2040=1
    HAL_ENABLE_MQTT
    HAL_ENABLE_TLS
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_pubsub_hal_client PRIVATE -Wall -Wextra -Werror)
add_test(NAME test_pubsub_hal_client COMMAND test_pubsub_hal_client)

add_executable(test_wireguard_lwip_port
    test_wireguard_lwip_port.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/network/jh_lwip_extension.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/rp2040/frameworks/arduino-wireguard-pico-w/src/WireGuard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_wireguard_lwip_port BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/arduino
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_wireguard_lwip_port PRIVATE
    HAL_TARGET_RP2040=1
    HAL_ENABLE_WIREGUARD
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_wireguard_lwip_port PRIVATE -Wall -Wextra -Werror)
add_test(NAME test_wireguard_lwip_port COMMAND test_wireguard_lwip_port)

# RP2040 SoftwareSerial must stay on the native Pico SDK PIO backend.  This is a
# source-selection contract rather than a host runtime test: the regression it
# guards against is compiling the portable, GPIO-ISR bit-banger for RP2040.
add_test(NAME test_rp2040_swserial_backend
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_rp2040_swserial_backend.cmake
)

add_test(NAME test_cyw43_dependency_boundary
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_cyw43_dependency_boundary.cmake
)

add_test(NAME test_lwip_dependency_boundary
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_lwip_dependency_boundary.cmake
)

get_filename_component(_jh_workspace_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE)
set(JH_CW43_FIXTURE_SETUP_SCRIPT
    "${_jh_workspace_root}/context-providers/context_JaszczurHAL/cw43_test_host_setup.sh"
    CACHE FILEPATH "Optional external CW43 hardware-fixture setup script")
if(EXISTS "${JH_CW43_FIXTURE_SETUP_SCRIPT}")
    add_test(NAME test_cw43_fixture_build_identity
        COMMAND ${CMAKE_COMMAND}
            "-DJH_CW43_FIXTURE_SETUP_SCRIPT=${JH_CW43_FIXTURE_SETUP_SCRIPT}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_cw43_fixture_build_identity.cmake
    )
else()
    message(STATUS
        "External CW43 fixture not found; fixture identity test is not registered")
endif()

add_hal_test(test_hal_gpio)
add_hal_test(test_cyw43_config)
add_hal_test(test_cyw43_driver)
add_hal_test(test_cyw43_gspi_transport)
add_hal_test(test_cyw43_lwip)
add_hal_test(test_hal_crc)
add_hal_test(test_hal_can)
add_hal_test(test_hal_thermocouple)
add_hal_test(test_hal_critical_section)
add_executable(test_pcf8563_driver
    test_pcf8563_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/pcf8563/pcf8563.cpp
)
target_link_libraries(test_pcf8563_driver PRIVATE hal_mock)
add_test(NAME test_pcf8563_driver COMMAND test_pcf8563_driver)
add_executable(test_ds3231_driver
    test_ds3231_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/ds3231/ds3231.cpp
)
target_link_libraries(test_ds3231_driver PRIVATE hal_mock)
add_test(NAME test_ds3231_driver COMMAND test_ds3231_driver)
add_executable(test_max6675_driver
    test_max6675_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/max6675/max6675_driver.cpp
)
target_link_libraries(test_max6675_driver PRIVATE hal_mock)
add_test(NAME test_max6675_driver COMMAND test_max6675_driver)
add_executable(test_mcp9600_driver
    test_mcp9600_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/mcp9600/mcp9600_driver.cpp
)
target_link_libraries(test_mcp9600_driver PRIVATE hal_mock)
add_test(NAME test_mcp9600_driver COMMAND test_mcp9600_driver)
add_executable(test_bh1750_driver
    test_bh1750_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/bh1750/hal_bh1750.cpp
)
target_link_libraries(test_bh1750_driver PRIVATE hal_mock)
add_test(NAME test_bh1750_driver COMMAND test_bh1750_driver)
add_executable(test_adp5360_driver
    test_adp5360_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/adp5360/hal_adp5360.cpp
)
target_compile_definitions(test_adp5360_driver PRIVATE HAL_ENABLE_ADP5360)
target_link_libraries(test_adp5360_driver PRIVATE hal_mock)
add_test(NAME test_adp5360_driver COMMAND test_adp5360_driver)
add_hal_test(test_simple_io_drivers)
add_executable(test_hd44780_driver
    test_hd44780_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/hd44780/hd44780.cpp
)
target_compile_definitions(test_hd44780_driver PRIVATE HAL_ENABLE_HD44780)
target_link_libraries(test_hd44780_driver PRIVATE hal_mock)
add_test(NAME test_hd44780_driver COMMAND test_hd44780_driver)
add_executable(test_tsc2007_driver
    test_tsc2007_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/tsc2007/tsc2007.cpp
)
target_compile_definitions(test_tsc2007_driver PRIVATE HAL_ENABLE_TSC2007)
target_link_libraries(test_tsc2007_driver PRIVATE hal_mock)
add_test(NAME test_tsc2007_driver COMMAND test_tsc2007_driver)
add_executable(test_stmpe610_driver
    test_stmpe610_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/stmpe610/stmpe610.cpp
)
target_compile_definitions(test_stmpe610_driver PRIVATE HAL_ENABLE_STMPE610)
target_link_libraries(test_stmpe610_driver PRIVATE hal_mock)
add_test(NAME test_stmpe610_driver COMMAND test_stmpe610_driver)
add_executable(test_irsmall_decoder_driver
    test_irsmall_decoder_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/frameworks/irsmall_decoder/irsmall_decoder.cpp
)
target_compile_definitions(test_irsmall_decoder_driver PRIVATE HAL_ENABLE_IRSMALL_DECODER)
target_link_libraries(test_irsmall_decoder_driver PRIVATE hal_mock)
add_test(NAME test_irsmall_decoder_driver COMMAND test_irsmall_decoder_driver)
add_executable(test_ads1x15_driver
    test_ads1x15_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/ads1x15/ads1x15_driver.cpp
)
target_link_libraries(test_ads1x15_driver PRIVATE hal_mock)
add_test(NAME test_ads1x15_driver COMMAND test_ads1x15_driver)
add_hal_test(test_dacless_driver)
add_executable(test_ili9341_driver
    test_ili9341_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/ili9341_driver.cpp
)
target_link_libraries(test_ili9341_driver PRIVATE hal_mock)
add_test(NAME test_ili9341_driver COMMAND test_ili9341_driver)
add_executable(test_st77xx_driver
    test_st77xx_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/st77xx_driver.cpp
)
target_compile_definitions(test_st77xx_driver PRIVATE
    HAL_ENABLE_ST7735
    HAL_ENABLE_ST7789
    HAL_ENABLE_ST7796S
    HAL_ENABLE_GC9A01
)
target_link_libraries(test_st77xx_driver PRIVATE hal_mock)
add_test(NAME test_st77xx_driver COMMAND test_st77xx_driver)
add_executable(test_ssd1306_driver
    test_ssd1306_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/ssd1306_driver.cpp
)
target_link_libraries(test_ssd1306_driver PRIVATE hal_mock)
add_test(NAME test_ssd1306_driver COMMAND test_ssd1306_driver)
add_executable(test_rgb_oled_driver
    test_rgb_oled_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/rgb_oled_driver.cpp
)
target_compile_definitions(test_rgb_oled_driver PRIVATE
    HAL_ENABLE_SSD1331
    HAL_ENABLE_SSD135X
)
target_link_libraries(test_rgb_oled_driver PRIVATE hal_mock)
add_test(NAME test_rgb_oled_driver COMMAND test_rgb_oled_driver)
add_executable(test_st7567_driver
    test_st7567_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/st7567_driver.cpp
)
target_compile_definitions(test_st7567_driver PRIVATE HAL_ENABLE_ST7567)
target_link_libraries(test_st7567_driver PRIVATE hal_mock)
add_test(NAME test_st7567_driver COMMAND test_st7567_driver)

add_executable(test_epd_drivers
    test_epd_drivers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/epd_spi_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/ssd16xx_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/uc81xx_driver.cpp
)
target_compile_definitions(test_epd_drivers PRIVATE
    HAL_ENABLE_SSD16XX
    HAL_ENABLE_UC81XX
)
target_link_libraries(test_epd_drivers PRIVATE hal_mock)
add_test(NAME test_epd_drivers COMMAND test_epd_drivers)

add_executable(test_hal_display_rgb_oled_facade
    test_hal_display_rgb_oled_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/hal_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/rgb_oled_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/jh_gfx.cpp
)
target_include_directories(test_hal_display_rgb_oled_facade PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils
)
target_compile_definitions(test_hal_display_rgb_oled_facade PRIVATE
    HAL_TARGET_RP2040=1
    HAL_ENABLE_SSD1331
    HAL_ENABLE_SSD135X
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_hal_display_rgb_oled_facade PRIVATE -Wall -Wextra)
target_link_libraries(test_hal_display_rgb_oled_facade PRIVATE
    $<TARGET_FILE:hal_mock>
    Threads::Threads
)
add_dependencies(test_hal_display_rgb_oled_facade hal_mock)
add_test(NAME test_hal_display_rgb_oled_facade
         COMMAND test_hal_display_rgb_oled_facade)

add_executable(test_hal_display_st7567_facade
    test_hal_display_st7567_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/hal_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/st7567_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/jh_gfx.cpp
)
target_include_directories(test_hal_display_st7567_facade PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils
)
target_compile_definitions(test_hal_display_st7567_facade PRIVATE
    HAL_TARGET_RP2040=1
    HAL_ENABLE_ST7567
    HAL_ENABLE_SPI
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_hal_display_st7567_facade PRIVATE -Wall -Wextra)
target_link_libraries(test_hal_display_st7567_facade PRIVATE
    $<TARGET_FILE:hal_mock>
    Threads::Threads
)
add_dependencies(test_hal_display_st7567_facade hal_mock)
add_test(NAME test_hal_display_st7567_facade
         COMMAND test_hal_display_st7567_facade)

add_executable(test_hal_display_epd_facade
    test_hal_display_epd_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/hal_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/epd_spi_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/ssd16xx_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/uc81xx_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/jh_gfx.cpp
)
target_include_directories(test_hal_display_epd_facade PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils
)
target_compile_definitions(test_hal_display_epd_facade PRIVATE
    HAL_TARGET_RP2040=1
    HAL_ENABLE_SSD16XX
    HAL_ENABLE_UC81XX
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_hal_display_epd_facade PRIVATE -Wall -Wextra)
target_link_libraries(test_hal_display_epd_facade PRIVATE
    $<TARGET_FILE:hal_mock>
    Threads::Threads
)
add_dependencies(test_hal_display_epd_facade hal_mock)
add_test(NAME test_hal_display_epd_facade
         COMMAND test_hal_display_epd_facade)
add_executable(test_jh_gfx_geometry
    test_jh_gfx_geometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/jh_gfx.cpp
)
target_include_directories(test_jh_gfx_geometry BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display
)
target_link_libraries(test_jh_gfx_geometry PRIVATE hal_mock)
add_test(NAME test_jh_gfx_geometry COMMAND test_jh_gfx_geometry)
add_executable(test_mcp2515_driver
    test_mcp2515_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/mcp2515/mcp2515_driver.cpp
)
target_link_libraries(test_mcp2515_driver PRIVATE hal_mock)
add_test(NAME test_mcp2515_driver COMMAND test_mcp2515_driver)
add_executable(test_mfrc522_driver
    test_mfrc522_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/mfrc522/mfrc522.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/mfrc522/mfrc522_spi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/mfrc522/mfrc522_i2c.cpp
)
target_compile_definitions(test_mfrc522_driver PRIVATE HAL_ENABLE_MFRC522)
target_link_libraries(test_mfrc522_driver PRIVATE hal_mock)
add_test(NAME test_mfrc522_driver COMMAND test_mfrc522_driver)
add_executable(test_pn532_driver
    test_pn532_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/pn532/pn532.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/pn532/pn532_spi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/pn532/pn532_i2c.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/pn532/pn532_uart.cpp
)
target_compile_definitions(test_pn532_driver PRIVATE HAL_ENABLE_PN532)
target_link_libraries(test_pn532_driver PRIVATE hal_mock)
add_test(NAME test_pn532_driver COMMAND test_pn532_driver)
add_executable(test_wireguard_crypto_shared
    test_wireguard_crypto_shared.cpp
)
target_include_directories(test_wireguard_crypto_shared PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/frameworks/wireguard/crypto
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/rp2040/frameworks/arduino-wireguard-pico-w/src
)
target_link_libraries(test_wireguard_crypto_shared PRIVATE hal_mock)
add_test(NAME test_wireguard_crypto_shared COMMAND test_wireguard_crypto_shared)
add_executable(test_wireguard_pbuf
    test_wireguard_pbuf.cpp
    fakes/lwip_fake.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_wireguard_pbuf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_wireguard_pbuf PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_wireguard_pbuf PRIVATE -Wall -Wextra -Werror)
add_test(NAME test_wireguard_pbuf COMMAND test_wireguard_pbuf)
add_executable(test_ntp_client
    test_ntp_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_ntp_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_ntp_client PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_ntp_client PRIVATE -Wall -Wextra -Werror)
add_test(NAME test_ntp_client COMMAND test_ntp_client)
add_executable(test_ff16_memdisk
    test_ff16_memdisk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/frameworks/filesystem/ff16/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_ff16_memdisk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_ff16_memdisk PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_FAT
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_ff16_memdisk PRIVATE -Wall -Wextra)
add_test(NAME test_ff16_memdisk COMMAND test_ff16_memdisk)
add_hal_test(test_hal_ds18b20)
add_hal_test(test_hal_dht)
add_hal_test(test_hal_eeprom)
add_hal_test(test_hal_timer)
add_stm32_host_test(test_stm32_hal_timer)
add_stm32_host_test(test_stm32_hal_system)
add_stm32_i2c_slave_host_test(test_stm32_hal_i2c_slave)
add_stm32_gpio_host_test(test_stm32_hal_gpio)
add_executable(test_stm32_pwm_clock
    test_stm32_pwm_clock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/stm32g474/hal_pwm_stm32g474.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_stm32_pwm_clock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_stm32_pwm_clock PRIVATE
    HAL_TARGET_STM32G474=1
    HAL_ENABLE_PWM_FREQ
    UNITY_INCLUDE_CONFIG_H
    HAL_ENABLE_UNITY
)
target_compile_options(test_stm32_pwm_clock PRIVATE -Wall -Wextra)
add_test(NAME test_stm32_pwm_clock COMMAND test_stm32_pwm_clock)
add_hal_test(test_hal_pwm)
add_hal_test(test_hal_adc)
add_hal_test(test_hal_dac)
add_hal_test(test_hal_dma_pwm_audio)
add_hal_test(test_hal_digipot)
add_hal_test(test_hal_pga2311)
add_hal_test(test_hal_pcnt)
add_hal_test(test_hal_spi)
add_hal_test(test_hal_i2c)
add_hal_test(test_hal_i2c_slave)
add_hal_test(test_hal_rgb_led)
add_hal_test(test_hal_serial)
add_hal_test(test_hal_serial_session)
add_hal_test(test_hal_serial_session_vocabulary)
add_hal_test(test_hal_status)
add_hal_test(test_hal_swserial)
add_hal_test(test_hal_onewire)
# Real bit-banging driver (JHOneWire) compiled straight in, run against mock GPIO
# so the actual reset/read/write pin sequences are covered (the public hal_onewire
# is faked under MOCK). Guards the onewire_drive_high write-before-mode ordering.
add_executable(test_hal_onewire_driver
    test_hal_onewire_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/onewire/onewire_driver.cpp
)
target_link_libraries(test_hal_onewire_driver PRIVATE hal_mock)
add_test(NAME test_hal_onewire_driver COMMAND test_hal_onewire_driver)
add_hal_test(test_hal_uart)
add_hal_test(test_hal_display)
add_hal_test(test_hal_wifi)
add_hal_test(test_hal_littlefs)
add_hal_test(test_hal_sdlogger)
add_hal_test(test_hal_net)
add_executable(test_hal_config_storage_flags
    test_hal_config_storage_flags.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_hal_config_storage_flags PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_hal_config_storage_flags PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_SDLOGGER
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
target_compile_options(test_hal_config_storage_flags PRIVATE -Wall -Wextra)
add_test(NAME test_hal_config_storage_flags COMMAND test_hal_config_storage_flags)
add_hal_test(test_hal_udp)
add_hal_test(test_hal_tcp)
add_hal_test(test_hal_http_server)
add_hal_test(test_hal_http_files)
add_hal_test(test_hal_websocket)
add_hal_test(test_hal_net_console)
add_hal_test(test_hal_net_commands)
add_hal_test(test_bsd_sockets)
add_hal_test(test_hal_tls)
add_hal_test(test_hal_http_client)
add_hal_test(test_bearssl_provider)

find_program(JH_OPENSSL_EXECUTABLE openssl)
if(JH_OPENSSL_EXECUTABLE)
    add_executable(test_bearssl_native_client
        test_bearssl_native_client.cpp
        ${CMAKE_SOURCE_DIR}/src/hal/impl/shared/frameworks/BearSSL/jh_bearssl_bsd_io.cpp
        ${CMAKE_SOURCE_DIR}/src/hal/impl/shared/frameworks/BearSSL/jh_bearssl_engine.cpp
        ${CMAKE_SOURCE_DIR}/src/hal/impl/shared/frameworks/BearSSL/jh_bearssl_provider.cpp)
    target_compile_definitions(test_bearssl_native_client PRIVATE
        HAL_ENABLE_TLS HAL_ENABLE_BSD_SOCKETS)
    target_include_directories(test_bearssl_native_client PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/hal/impl/shared/frameworks/BearSSL/vendor/inc)
    target_link_libraries(test_bearssl_native_client PRIVATE jh_bearssl_host)
    add_test(NAME test_bearssl_native_integration
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_bearssl_native_integration.sh
                $<TARGET_FILE:test_bearssl_native_client>
                ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tls_native)
endif()
add_executable(test_bsd_sockets_c_compile test_bsd_sockets_c_compile.c)
target_link_libraries(test_bsd_sockets_c_compile PRIVATE hal_mock)
set_target_properties(test_bsd_sockets_c_compile PROPERTIES
    LINKER_LANGUAGE CXX)
add_test(NAME test_bsd_sockets_c_compile COMMAND test_bsd_sockets_c_compile)
add_executable(test_hal_tls_c_compile test_hal_tls_c_compile.c)
target_link_libraries(test_hal_tls_c_compile PRIVATE hal_mock)
set_target_properties(test_hal_tls_c_compile PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME test_hal_tls_c_compile COMMAND test_hal_tls_c_compile)
add_executable(test_hal_http_client_c_compile test_hal_http_client_c_compile.c)
target_link_libraries(test_hal_http_client_c_compile PRIVATE hal_mock)
set_target_properties(test_hal_http_client_c_compile PROPERTIES
    LINKER_LANGUAGE CXX)
add_test(NAME test_hal_http_client_c_compile
    COMMAND test_hal_http_client_c_compile)
add_hal_test(test_hal_wireguard)
add_hal_test(test_hal_mqtt)
add_hal_test(test_hal_network_status)
add_hal_test(test_hal_modem_at)
add_hal_test(test_hal_simcom_a76xx)
add_hal_test(test_hal_ota)
add_hal_test(test_hal_time)
add_hal_test(test_hal_rtc)
add_hal_test(test_hal_kv)
add_hal_test(test_hal_external_adc)
add_hal_test(test_hal_gps)
add_hal_test(test_hal_system)
add_hal_test(test_hal_bits)
add_hal_test(test_hal_crypto)
add_executable(test_jpeg
    test_jpeg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/tools.cpp
)
target_link_libraries(test_jpeg PRIVATE hal_mock)
add_test(NAME test_jpeg COMMAND test_jpeg)
add_executable(test_lodepng
    test_lodepng.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/tools.cpp
)
target_link_libraries(test_lodepng PRIVATE hal_mock)
add_test(NAME test_lodepng COMMAND test_lodepng)
add_hal_test(test_SmartTimers)
add_hal_test(test_hal_soft_timer)
add_hal_test(test_pidController)

if(TARGET hal_freertos_posix)
    add_hal_freertos_posix_test(test_freertos_posix_runtime)
endif()

# test_multicoreWatchdog: compiles multicoreWatchdog.cpp directly to validate
# watchdog behavior on host.
add_executable(test_multicoreWatchdog
    test_multicoreWatchdog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/multicoreWatchdog.cpp
)
target_link_libraries(test_multicoreWatchdog PRIVATE hal_mock)
add_test(NAME test_multicoreWatchdog COMMAND test_multicoreWatchdog)

# test_gps_nmea_parser: the portable NMEA parser is backend-agnostic (not part
# of the mock lib), so compile it (with shared tools helpers) straight into the
# test and feed real sentences through it.
add_executable(test_gps_nmea_parser
    test_gps_nmea_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/frameworks/gps/gps_nmea_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/tools.cpp
)
target_link_libraries(test_gps_nmea_parser PRIVATE hal_mock)
add_test(NAME test_gps_nmea_parser COMMAND test_gps_nmea_parser)

# test_tools: compiles tools.cpp directly so the utility functions can be
# tested on the host without hardware.
add_executable(test_tools
    test_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/tools.cpp
)
target_link_libraries(test_tools PRIVATE hal_mock)
add_test(NAME test_tools COMMAND test_tools)
