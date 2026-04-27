# Helper: create a test executable linked against hal_mock
function(add_hal_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE hal_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

set(STUBS ${CMAKE_CURRENT_SOURCE_DIR}/../src/arduino_host_stubs)

add_hal_test(test_hal_gpio)
add_hal_test(test_hal_can)
add_hal_test(test_hal_thermocouple)
add_hal_test(test_hal_eeprom)
add_hal_test(test_hal_timer)
add_hal_test(test_hal_pwm)
add_hal_test(test_hal_adc)
add_hal_test(test_hal_spi)
add_hal_test(test_hal_i2c)
add_hal_test(test_hal_i2c_slave)
add_hal_test(test_hal_rgb_led)
add_hal_test(test_hal_serial)
add_hal_test(test_hal_serial_session)
add_hal_test(test_hal_serial_session_vocabulary)
add_hal_test(test_hal_swserial)
add_hal_test(test_hal_uart)
add_hal_test(test_hal_display)
add_hal_test(test_hal_wifi)
add_hal_test(test_hal_time)
add_hal_test(test_hal_kv)
add_hal_test(test_hal_external_adc)
add_hal_test(test_hal_gps)
add_hal_test(test_hal_system)
add_hal_test(test_hal_bits)
add_hal_test(test_hal_crypto)
add_hal_test(test_SmartTimers)
add_hal_test(test_hal_soft_timer)
add_hal_test(test_pidController)

# test_multicoreWatchdog: compiles multicoreWatchdog.cpp with Arduino/SPI/SD
# stubs to validate watchdog behavior on host.
add_executable(test_multicoreWatchdog
    test_multicoreWatchdog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/multicoreWatchdog.cpp
)
target_link_libraries(test_multicoreWatchdog PRIVATE hal_mock)
target_include_directories(test_multicoreWatchdog BEFORE PRIVATE ${STUBS})
add_test(NAME test_multicoreWatchdog COMMAND test_multicoreWatchdog)

# test_tools: compiles tools.cpp with Arduino/SPI/SD stubs so the pure
# utility functions can be tested on the host without hardware.
add_executable(test_tools
    test_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/tools.cpp
)
target_link_libraries(test_tools PRIVATE hal_mock)
# Stubs must be first so <Arduino.h>, <SPI.h>, <SD.h> resolve to our no-op
# versions before the compiler searches system include paths.
target_include_directories(test_tools BEFORE PRIVATE ${STUBS})
add_test(NAME test_tools COMMAND test_tools)
