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
        "hal_time_set_unix_ex"
        "hal_time_get_status_ex"
        "jh_time_platform_clock_changed"
        "hal_time_sync_ntp"
        "hal_time_unix"
        "hal_time_attach_rtc_ex"
        "jh_time_runtime_snapshot"
        "jh_time_libc_gettimeofday"
        "jh_time_libc_settimeofday"
        "jh_ntp_validate_response"
        "hal_micros64"
        "hal_mutex_lock"
        "hal_net_service")
    if(NOT _service_contents MATCHES "${_required_symbol}")
        message(FATAL_ERROR
            "Shared time/NTP service lost required behavior: ${_required_symbol}")
    endif()
endforeach()
if(_service_contents MATCHES
   "jh_stm32g474_runtime_gettimeofday|jh_time_platform_apply_unix")
    message(FATAL_ERROR "Shared time/NTP service gained target runtime coupling")
endif()

file(READ "${_rp_hook}" _rp_hook_contents)
if(NOT _rp_hook_contents MATCHES "_gettimeofday" OR
   NOT _rp_hook_contents MATCHES "settimeofday" OR
   NOT _rp_hook_contents MATCHES "jh_time_libc_gettimeofday" OR
   NOT _rp_hook_contents MATCHES "jh_time_libc_settimeofday")
    message(FATAL_ERROR "RP libc time bridge lost shared-clock delegation")
endif()

file(READ "${_stm_hook}" _stm_hook_contents)
if(NOT _stm_hook_contents MATCHES "jh_stm32g474_runtime_gettimeofday" OR
   NOT _stm_hook_contents MATCHES "settimeofday" OR
   NOT _stm_hook_contents MATCHES "jh_time_libc_gettimeofday" OR
   NOT _stm_hook_contents MATCHES "jh_time_libc_settimeofday")
    message(FATAL_ERROR "STM32 libc time bridge lost shared-clock delegation")
endif()

file(READ "${_mock_hook}" _mock_hook_contents)
if(_mock_hook_contents MATCHES "jh_time_platform_apply_unix")
    message(FATAL_ERROR "Removed platform clock setter returned in mock")
endif()

file(READ "${_runtime_test}" _test_contents)
foreach(_required_test IN ITEMS
        "test_ntp_timeout_retries_secondary_server"
        "test_network_service_can_reenter_time_getter"
        "test_time_snapshots_are_safe_during_concurrent_updates"
        "test_wall_clock_uses_64_bit_monotonic_base_across_millis_wrap"
        "test_valid_rtc_restores_unset_wall_clock"
        "test_successful_ntp_sync_is_written_to_attached_rtc")
    if(NOT _test_contents MATCHES "${_required_test}")
        message(FATAL_ERROR
            "Time/NTP regression coverage is missing: ${_required_test}")
    endif()
endforeach()
