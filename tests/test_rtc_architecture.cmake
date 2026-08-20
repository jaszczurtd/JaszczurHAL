if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_facade "${JH_ROOT}/src/hal/rtc/hal_rtc.cpp")
set(_provider_header
    "${JH_ROOT}/src/hal/rtc/jh_rtc_provider.h")
set(_i2c_provider
    "${JH_ROOT}/src/hal/rtc/jh_rtc_i2c_provider.cpp")
set(_mock_provider
    "${JH_ROOT}/src/hal/impl/.mock/jh_rtc_provider.cpp")
set(_stm32_provider
    "${JH_ROOT}/src/hal/impl/stm32g474/jh_stm32g474_rtc_provider.cpp")
set(_rp_provider
    "${JH_ROOT}/src/hal/impl/rp2040/jh_rp_rtc_provider.cpp")

foreach(_required IN ITEMS
        "${_facade}"
        "${_provider_header}"
        "${_i2c_provider}"
        "${_mock_provider}"
        "${_rp_provider}"
        "${_stm32_provider}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "RTC facade/provider source is missing: ${_required}")
    endif()
endforeach()

foreach(_removed IN ITEMS
        "${JH_ROOT}/src/hal/impl/.mock/hal_rtc.cpp"
        "${JH_ROOT}/src/hal/impl/rp2040/hal_rtc.cpp"
        "${JH_ROOT}/src/hal/impl/stm32g474/hal_rtc.cpp")
    if(EXISTS "${_removed}")
        message(FATAL_ERROR "Target-local RTC facade returned: ${_removed}")
    endif()
endforeach()

file(READ "${_facade}" _facade_contents)
foreach(_owned_symbol IN ITEMS
        hal_rtc_init_ex
        hal_rtc_deinit
        hal_rtc_get_datetime_ex
        hal_rtc_set_datetime_ex
        hal_rtc_get_epoch_ex
        hal_rtc_set_epoch_ex
        hal_rtc_get_clock_integrity_ex
        hal_rtc_get_clock_source_ex
        hal_rtc_set_interrupt_enable_ex
        hal_rtc_get_interrupt_enable_ex
        hal_rtc_get_and_clear_flags_ex
        hal_rtc_get_temperature_ex
        hal_rtc_set_clkout_mode_ex
        hal_rtc_get_clkout_mode_ex
        hal_rtc_set_timer_ex
        hal_rtc_get_timer_ex
        hal_rtc_set_alarm_ex
        hal_rtc_get_alarm_ex
        hal_rtc_wakeup_arm_ex
        hal_rtc_wakeup_cancel_ex
        hal_rtc_wakeup_get_state_ex)
    if(NOT _facade_contents MATCHES
       "${_owned_symbol}[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "Shared RTC facade does not own ${_owned_symbol}")
    endif()
endforeach()
if(NOT _facade_contents MATCHES "jh_calendar_validate_datetime" OR
   NOT _facade_contents MATCHES "hal_mutex_lock" OR
   NOT _facade_contents MATCHES "jh_rtc_provider_get_ops")
    message(FATAL_ERROR
        "RTC facade lost shared validation, locking, or provider dispatch")
endif()

foreach(_provider IN ITEMS
        "${_i2c_provider}"
        "${_mock_provider}"
        "${_rp_provider}"
        "${_stm32_provider}")
    file(READ "${_provider}" _provider_contents)
    if(_provider_contents MATCHES
       "hal_status_t[ \t\r\n]+hal_rtc_[a-z_]+[ \t\r\n]*\\(" OR
       _provider_contents MATCHES
       "bool[ \t\r\n]+hal_rtc_[a-z_]+[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "RTC provider exports public facade operations: ${_provider}")
    endif()
endforeach()

file(READ "${_stm32_provider}" _stm32_provider_contents)
if(NOT _stm32_provider_contents MATCHES "HAL_RTC_CHIP_INTERNAL" OR
   NOT _stm32_provider_contents MATCHES "jh_rtc_i2c_provider_get_ops")
    message(FATAL_ERROR
        "STM32 RTC provider lost internal dispatch or shared I2C delegation")
endif()

file(READ "${_rp_provider}" _rp_provider_contents)
foreach(_rp_requirement IN ITEMS
        "HAL_RTC_CHIP_INTERNAL"
        "pico/aon_timer.h"
        "aon_timer_is_running"
        "aon_timer_enable_alarm"
        "aon_timer_disable_alarm"
        "HAL_RTC_CLOCK_SOURCE_AON"
        "jh_rtc_i2c_provider_get_ops")
    if(NOT _rp_provider_contents MATCHES "${_rp_requirement}")
        message(FATAL_ERROR
            "RP RTC provider lost required behavior: ${_rp_requirement}")
    endif()
endforeach()

file(READ "${_mock_provider}" _mock_contents)
if(_mock_contents MATCHES "jh_calendar_|rtc_validate_datetime")
    message(FATAL_ERROR "Mock RTC provider duplicates facade validation")
endif()

foreach(_driver IN ITEMS
        "${JH_ROOT}/src/hal/rtc/pcf8563/pcf8563.cpp"
        "${JH_ROOT}/src/hal/rtc/ds3231/ds3231.cpp")
    file(READ "${_driver}" _driver_contents)
    if(NOT _driver_contents MATCHES "hal_i2c")
        message(FATAL_ERROR "RTC chip driver bypasses public HAL I2C: ${_driver}")
    endif()
    if(_driver_contents MATCHES
       "hardware/i2c|pico/stdlib|stm32g4|stm32g474|HAL_TARGET_IS_")
        message(FATAL_ERROR "RTC chip driver gained target coupling: ${_driver}")
    endif()
endforeach()

file(READ "${JH_ROOT}/CMakeLists.txt" _root_cmake)
file(READ "${JH_ROOT}/stm32_lib/CMakeLists.txt" _stm32_cmake)
file(READ "${JH_ROOT}/cmake/jh_rp_native_sdk.cmake" _rp_cmake)
if(NOT _root_cmake MATCHES "hal/rtc/hal_rtc\\.cpp" OR
   NOT _stm32_cmake MATCHES "hal/\\*\\.cpp" OR
   NOT _rp_cmake MATCHES "pico_aon_timer")
    message(FATAL_ERROR "Shared RTC facade is missing from a source manifest")
endif()
