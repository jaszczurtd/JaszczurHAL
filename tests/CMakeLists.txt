# Helper: create a test executable linked against hal_mock
function(add_hal_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE hal_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_hal_test(test_hal_gpio)
add_hal_test(test_hal_can)
add_hal_test(test_hal_thermocouple)
add_executable(test_max6675_driver
    test_max6675_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/max6675/max6675_driver.cpp
)
target_link_libraries(test_max6675_driver PRIVATE hal_mock)
add_test(NAME test_max6675_driver COMMAND test_max6675_driver)
add_executable(test_mcp9600_driver
    test_mcp9600_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/mcp9600/mcp9600_driver.cpp
)
target_link_libraries(test_mcp9600_driver PRIVATE hal_mock)
add_test(NAME test_mcp9600_driver COMMAND test_mcp9600_driver)
add_executable(test_ads1x15_driver
    test_ads1x15_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/ads1x15/ads1x15_driver.cpp
)
target_link_libraries(test_ads1x15_driver PRIVATE hal_mock)
add_test(NAME test_ads1x15_driver COMMAND test_ads1x15_driver)
add_executable(test_mcp2515_driver
    test_mcp2515_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/mcp2515/mcp2515_driver.cpp
)
target_link_libraries(test_mcp2515_driver PRIVATE hal_mock)
add_test(NAME test_mcp2515_driver COMMAND test_mcp2515_driver)
add_hal_test(test_hal_ds18b20)
add_hal_test(test_hal_eeprom)
add_hal_test(test_hal_timer)
add_hal_test(test_hal_pwm)
add_hal_test(test_hal_adc)
add_hal_test(test_hal_dac)
add_hal_test(test_hal_digipot)
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
add_hal_test(test_hal_uart)
add_hal_test(test_hal_display)
add_hal_test(test_hal_wifi)
add_hal_test(test_hal_littlefs)
add_hal_test(test_hal_sdlogger)
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
add_hal_test(test_SmartTimers)
add_hal_test(test_hal_soft_timer)
add_hal_test(test_pidController)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/gps/gps_nmea_parser.cpp
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
