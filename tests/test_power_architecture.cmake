if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_header "${JH_ROOT}/src/hal/power/hal_power.h")
set(_mock "${JH_ROOT}/src/hal/impl/.mock/hal_power.cpp")
set(_rp "${JH_ROOT}/src/hal/impl/rp2040/hal_power.cpp")
set(_stm32 "${JH_ROOT}/src/hal/impl/stm32g474/hal_power.cpp")
set(_stm32_system
    "${JH_ROOT}/src/hal/impl/stm32g474/port/system_stm32g474.c")

foreach(_required IN ITEMS
        "${_header}" "${_mock}" "${_rp}" "${_stm32}" "${_stm32_system}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Power-management source is missing: ${_required}")
    endif()
endforeach()

file(READ "${_header}" _header_contents)
foreach(_symbol IN ITEMS
        hal_power_get_capabilities_ex
        hal_power_enter_ex
        hal_power_get_last_wake_ex)
    if(NOT _header_contents MATCHES "${_symbol}[ \t\r\n]*\\(")
        message(FATAL_ERROR "Public power API lost ${_symbol}")
    endif()
    foreach(_backend IN ITEMS "${_mock}" "${_rp}" "${_stm32}")
        file(READ "${_backend}" _backend_contents)
        if(NOT _backend_contents MATCHES "${_symbol}[ \t\r\n]*\\(")
            message(FATAL_ERROR
                "Power backend does not implement ${_symbol}: ${_backend}")
        endif()
    endforeach()
endforeach()

file(READ "${_stm32}" _stm32_contents)
foreach(_requirement IN ITEMS
        PWR_CR1_LPMS_STOP0
        PWR_CR1_LPMS_STOP1
        PWR_CR1_LPMS_STANDBY
        stm32g474_system_clock_restore_after_stop
        stm32g474_monotonic_compensate_us
        HAL_ENABLE_FREERTOS)
    if(NOT _stm32_contents MATCHES "${_requirement}")
        message(FATAL_ERROR
            "STM32 power backend lost required behavior: ${_requirement}")
    endif()
endforeach()

file(READ "${_stm32_system}" _stm32_system_contents)
foreach(_requirement IN ITEMS
        stm32g474_system_clock_restore_after_stop
        stm32g474_monotonic_compensate_us
        g_monotonic_offset_us)
    if(NOT _stm32_system_contents MATCHES "${_requirement}")
        message(FATAL_ERROR
            "STM32 system clock/time source lost power hook: ${_requirement}")
    endif()
endforeach()

file(READ "${_rp}" _rp_contents)
if(NOT _rp_contents MATCHES "HAL_POWER_STATE_SLEEP" OR
   NOT _rp_contents MATCHES "HAL_EUNSUPPORTED" OR
   NOT _rp_contents MATCHES "__wfi" OR
   NOT _rp_contents MATCHES "jh_power_request_is_rtc_only")
    message(FATAL_ERROR "RP power backend lost bounded Sleep-only behavior")
endif()

if(NOT _stm32_contents MATCHES "jh_power_request_is_rtc_only")
    message(FATAL_ERROR
        "STM32 power backend no longer filters unrequested wake interrupts")
endif()

file(READ "${JH_ROOT}/config/features/core.json" _features)
if(NOT _features MATCHES "HAL_ENABLE_POWER_MANAGEMENT" OR
   NOT _features MATCHES "HAL_ENABLE_INTERNAL_RTC")
    message(FATAL_ERROR "Power management is missing from the feature graph")
endif()

file(READ "${JH_ROOT}/src/hal/hal.h" _umbrella)
if(NOT _umbrella MATCHES "hal/power/hal_power\\.h")
    message(FATAL_ERROR "HAL umbrella does not expose the power API")
endif()
