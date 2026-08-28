if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_facade "${JH_ROOT}/src/hal/gps/hal_gps.cpp")
set(_engine
    "${JH_ROOT}/src/hal/gps/hal_gps_core.cpp")
set(_parser
    "${JH_ROOT}/src/hal/gps/gps_nmea_parser.cpp")
set(_mock_injection "${JH_ROOT}/src/hal/impl/.mock/hal_gps.cpp")

foreach(_required IN ITEMS
        "${_facade}"
        "${_engine}"
        "${_parser}"
        "${_mock_injection}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "GPS facade/engine source is missing: ${_required}")
    endif()
endforeach()

foreach(_removed IN ITEMS
        "${JH_ROOT}/src/hal/impl/rp2040/hal_gps.cpp"
        "${JH_ROOT}/src/hal/impl/stm32g474/hal_gps.cpp")
    if(EXISTS "${_removed}")
        message(FATAL_ERROR "Target-local GPS facade returned: ${_removed}")
    endif()
endforeach()

file(READ "${_facade}" _facade_contents)
foreach(_owned_symbol IN ITEMS
        hal_gps_init
        hal_gps_pause
        hal_gps_resume
        hal_gps_update
        hal_gps_serial_available)
    if(NOT _facade_contents MATCHES "${_owned_symbol}[ \t\r\n]*\\(")
        message(FATAL_ERROR "Portable GPS facade does not own ${_owned_symbol}")
    endif()
endforeach()
if(NOT _facade_contents MATCHES "hal_uart" OR
   NOT _facade_contents MATCHES "hal_swserial" OR
   NOT _facade_contents MATCHES "HAL_GPS_TRANSPORT_UART" OR
   NOT _facade_contents MATCHES "HAL_GPS_TRANSPORT_SWSERIAL")
    message(FATAL_ERROR
        "Portable GPS facade lost UART/SoftwareSerial compile-time dispatch")
endif()
if(_facade_contents MATCHES
   "pico/|hardware/|stm32g4|stm32g474|HAL_TARGET_IS_RP2040")
    message(FATAL_ERROR "Portable GPS facade gained MCU/SDK coupling")
endif()

file(READ "${_engine}" _engine_contents)
foreach(_owned_symbol IN ITEMS
        hal_gps_encode
        hal_gps_location_is_valid
        hal_gps_location_is_updated
        hal_gps_location_age
        hal_gps_latitude
        hal_gps_longitude
        hal_gps_speed_kmph
        hal_gps_altitude_m
        hal_gps_course_deg
        hal_gps_satellites_used
        hal_gps_satellites_in_view
        hal_gps_hdop
        hal_gps_vdop
        hal_gps_pdop
        hal_gps_fix_quality
        hal_gps_fix_mode
        hal_gps_horizontal_accuracy_m
        hal_gps_date_year
        hal_gps_date_month
        hal_gps_date_day
        hal_gps_time_hour
        hal_gps_time_minute
        hal_gps_time_second
        hal_gps_chars_processed
        hal_gps_passed_checksum
        hal_gps_failed_checksum
        hal_gps_sentences_with_fix)
    if(NOT _engine_contents MATCHES "${_owned_symbol}[ \t\r\n]*\\(")
        message(FATAL_ERROR "Shared GPS engine does not own ${_owned_symbol}")
    endif()
endforeach()
if(NOT _engine_contents MATCHES "gps_nmea_encode" OR
   NOT _engine_contents MATCHES "hal_mutex_lock")
    message(FATAL_ERROR "Shared GPS engine lost parser or locking ownership")
endif()

file(READ "${_mock_injection}" _mock_contents)
if(NOT _mock_contents MATCHES "hal_mock_gps_set_location" OR
   NOT _mock_contents MATCHES "hal_mock_gps_reset")
    message(FATAL_ERROR "Mock GPS injection API is incomplete")
endif()
foreach(_forbidden_symbol IN ITEMS
        hal_gps_init
        hal_gps_update
        hal_gps_encode
        hal_gps_location_is_valid
        hal_gps_latitude
        hal_gps_chars_processed
        hal_gps_serial_available)
    if(_mock_contents MATCHES
       "(void|bool|uint32_t|double|int)[ \t\r\n]+${_forbidden_symbol}[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "Mock GPS duplicates public operation ${_forbidden_symbol}")
    endif()
endforeach()

file(READ "${JH_ROOT}/CMakeLists.txt" _root_cmake)
file(READ "${JH_ROOT}/stm32_lib/CMakeLists.txt" _stm32_cmake)
if(NOT _root_cmake MATCHES "hal/gps/hal_gps\\.cpp" OR
   NOT _root_cmake MATCHES "hal/gps/hal_gps_core\\.cpp" OR
   NOT _stm32_cmake MATCHES "hal/\\*\\.cpp")
    message(FATAL_ERROR "Shared GPS sources are missing from a source manifest")
endif()
