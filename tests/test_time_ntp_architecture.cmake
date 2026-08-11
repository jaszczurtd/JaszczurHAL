if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_service "${JH_ROOT}/src/hal/time/hal_time_ntp.cpp")
set(_platform_header "${JH_ROOT}/src/hal/time/jh_time_platform.h")
set(_mock_hook "${JH_ROOT}/src/hal/impl/.mock/hal_time.cpp")
set(_rp_hook "${JH_ROOT}/src/hal/impl/rp2040/hal_time.cpp")
set(_stm_hook "${JH_ROOT}/src/hal/impl/stm32g474/hal_time.cpp")
set(_runtime_test "${JH_ROOT}/tests/test_hal_time.cpp")

foreach(_required IN ITEMS
        "${_service}"
        "${_platform_header}"
        "${_mock_hook}"
        "${_rp_hook}"
        "${_stm_hook}"
        "${_runtime_test}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Time/NTP architecture file is missing: ${_required}")
    endif()
endforeach()

file(READ "${_service}" _service_contents)
foreach(_required_symbol IN ITEMS
        "hal_time_sync_ntp"
        "hal_time_unix"
        "jh_time_runtime_snapshot"
        "jh_ntp_validate_response"
        "hal_mutex_lock"
        "hal_net_service")
    if(NOT _service_contents MATCHES "${_required_symbol}")
        message(FATAL_ERROR
            "Shared time/NTP service lost required behavior: ${_required_symbol}")
    endif()
endforeach()
if(_service_contents MATCHES
   "settimeofday|jh_stm32g474_runtime_gettimeofday")
    message(FATAL_ERROR "Shared time/NTP service gained target runtime coupling")
endif()

foreach(_hook IN ITEMS "${_mock_hook}" "${_rp_hook}" "${_stm_hook}")
    file(READ "${_hook}" _hook_contents)
    if(NOT _hook_contents MATCHES "jh_time_platform_apply_unix" OR
       _hook_contents MATCHES
       "bool[ \t\r\n]+hal_time_|uint64_t[ \t\r\n]+hal_time_")
        message(FATAL_ERROR
            "Target time file is not limited to platform hooks: ${_hook}")
    endif()
endforeach()

file(READ "${_runtime_test}" _test_contents)
foreach(_required_test IN ITEMS
        "test_ntp_timeout_retries_secondary_server"
        "test_network_service_can_reenter_time_getter"
        "test_time_snapshots_are_safe_during_concurrent_updates")
    if(NOT _test_contents MATCHES "${_required_test}")
        message(FATAL_ERROR
            "Time/NTP regression coverage is missing: ${_required_test}")
    endif()
endforeach()
