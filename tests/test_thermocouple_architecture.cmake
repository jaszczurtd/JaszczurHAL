if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_facade "${JH_ROOT}/src/hal/temperature/hal_thermocouple.cpp")
set(_provider_header
    "${JH_ROOT}/src/hal/temperature/jh_thermocouple_provider.h")
set(_hardware_provider
    "${JH_ROOT}/src/hal/temperature/jh_thermocouple_hardware_provider.cpp")
set(_mock_provider
    "${JH_ROOT}/src/hal/impl/.mock/hal_thermocouple.cpp")
set(_rp_anchor
    "${JH_ROOT}/src/hal/impl/rp2040/hal_thermocouple.cpp")
set(_stm_anchor
    "${JH_ROOT}/src/hal/impl/stm32g474/hal_thermocouple.cpp")

foreach(_required IN ITEMS
        "${_facade}"
        "${_provider_header}"
        "${_hardware_provider}"
        "${_mock_provider}"
        "${_rp_anchor}"
        "${_stm_anchor}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR
            "Thermocouple facade/provider source is missing: ${_required}")
    endif()
endforeach()

file(READ "${_facade}" _facade_contents)
foreach(_owned_symbol IN ITEMS
        hal_thermocouple_init_ex
        hal_thermocouple_deinit
        hal_thermocouple_read_ex
        hal_thermocouple_read_ambient_ex
        hal_thermocouple_read_adc_raw_ex
        hal_thermocouple_set_type
        hal_thermocouple_get_type_ex
        hal_thermocouple_set_filter
        hal_thermocouple_get_filter_ex
        hal_thermocouple_set_adc_resolution
        hal_thermocouple_get_adc_resolution_ex
        hal_thermocouple_set_ambient_resolution
        hal_thermocouple_enable
        hal_thermocouple_is_enabled_ex
        hal_thermocouple_set_alert
        hal_thermocouple_get_alert_temp_ex
        hal_thermocouple_get_status_ex)
    if(NOT _facade_contents MATCHES "${_owned_symbol}[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "Shared thermocouple facade does not own ${_owned_symbol}")
    endif()
endforeach()

if(NOT _facade_contents MATCHES "jh_thermocouple_provider_get" OR
   NOT _facade_contents MATCHES "hal_mutex_lock" OR
   _facade_contents MATCHES "hal_mcp9600_|hal_max6675_|HAL_TARGET_IS_")
    message(FATAL_ERROR
        "Thermocouple facade lost provider dispatch/locking or gained backend coupling")
endif()

foreach(_provider IN ITEMS
        "${_hardware_provider}"
        "${_mock_provider}"
        "${_rp_anchor}"
        "${_stm_anchor}")
    file(READ "${_provider}" _provider_contents)
    foreach(_public_symbol IN ITEMS
            hal_thermocouple_init
            hal_thermocouple_deinit
            hal_thermocouple_read
            hal_thermocouple_set_type
            hal_thermocouple_get_type
            hal_thermocouple_enable
            hal_thermocouple_set_alert
            hal_thermocouple_get_status)
        if(_provider_contents MATCHES
           "${_public_symbol}[ \t\r\n]*\\(")
            message(FATAL_ERROR
                "Thermocouple provider exports public facade operation: ${_provider}")
        endif()
    endforeach()
endforeach()

file(READ "${_mock_provider}" _mock_contents)
if(NOT _mock_contents MATCHES "jh_thermocouple_provider_get" OR
   NOT _mock_contents MATCHES "jh_thermocouple_provider_visit_context")
    message(FATAL_ERROR
        "Mock thermocouple provider lost provider selection or safe injection")
endif()

file(READ "${JH_ROOT}/CMakeLists.txt" _root_cmake)
file(READ "${JH_ROOT}/cmake/jh_rp_hal_sources.cmake" _rp_sources)
file(READ "${JH_ROOT}/stm32_lib/jh_stm32g474_firmware.cmake" _stm_sources)
if(NOT _root_cmake MATCHES "hal/temperature/hal_thermocouple\\.cpp" OR
   NOT _rp_sources MATCHES "SRC_DIR}/hal/\\*\\.cpp" OR
   NOT _stm_sources MATCHES "_jh_src}/hal/\\*\\.cpp")
    message(FATAL_ERROR
        "Shared thermocouple facade is missing from a source inventory")
endif()
