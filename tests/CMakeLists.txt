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

add_hal_test(test_hal_gpio)
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
)
target_link_libraries(test_st77xx_driver PRIVATE hal_mock)
add_test(NAME test_st77xx_driver COMMAND test_st77xx_driver)
add_executable(test_ssd1306_driver
    test_ssd1306_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/display/ssd1306_driver.cpp
)
target_link_libraries(test_ssd1306_driver PRIVATE hal_mock)
add_test(NAME test_ssd1306_driver COMMAND test_ssd1306_driver)
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
add_executable(test_wireguard_crypto_shared
    test_wireguard_crypto_shared.cpp
)
target_include_directories(test_wireguard_crypto_shared PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/frameworks/wireguard/crypto
)
target_link_libraries(test_wireguard_crypto_shared PRIVATE hal_mock)
add_test(NAME test_wireguard_crypto_shared COMMAND test_wireguard_crypto_shared)
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
add_hal_test(test_hal_eeprom)
add_hal_test(test_hal_timer)
add_stm32_host_test(test_stm32_hal_timer)
add_stm32_host_test(test_stm32_hal_system)
add_stm32_i2c_slave_host_test(test_stm32_hal_i2c_slave)
add_hal_test(test_hal_pwm)
add_hal_test(test_hal_adc)
add_hal_test(test_hal_dac)
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
add_hal_test(test_hal_wireguard)
add_hal_test(test_hal_mqtt)
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
