if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_session_header "${JH_ROOT}/src/hal/serial/hal_serial_session.h")
set(_session_source "${JH_ROOT}/src/hal/serial/hal_serial_session.cpp")
set(_auth_header "${JH_ROOT}/src/hal/security/hal_sc_auth.h")
set(_auth_source "${JH_ROOT}/src/hal/security/hal_sc_auth.cpp")
set(_security_header
    "${JH_ROOT}/src/hal/security/jh_secure_random.h")
set(_security_source
    "${JH_ROOT}/src/hal/security/jh_secure_random.cpp")
set(_ble_api "${JH_ROOT}/src/hal/bluetooth/hal_ble_stream.cpp")
set(_ble_session
    "${JH_ROOT}/src/hal/bluetooth/jh_ble_stream_session.c")

foreach(_required IN ITEMS
        "${_session_header}"
        "${_session_source}"
        "${_auth_header}"
        "${_auth_source}"
        "${_security_header}"
        "${_security_source}"
        "${_ble_api}"
        "${_ble_session}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Security/session source is missing: ${_required}")
    endif()
endforeach()

file(READ "${_session_header}" _session_header_contents)
if(_session_header_contents MATCHES "static[ \t\r\n]+inline" OR
   _session_header_contents MATCHES
   "hal_serial_session_poll[^;]*[ \t\r\n]*\\{")
    message(FATAL_ERROR
        "Serial Session protocol engine returned to the public header")
endif()

file(READ "${_session_source}" _session_source_contents)
foreach(_operation IN ITEMS
        hal_serial_session_init_with_vocabulary
        hal_serial_session_init
        hal_serial_session_set_unknown_handler
        hal_serial_session_is_active
        hal_serial_session_is_authenticated
        hal_serial_session_id
        hal_serial_session_println
        hal_serial_session_poll)
    if(NOT _session_source_contents MATCHES
       "${_operation}[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "Compiled Serial Session engine does not own ${_operation}")
    endif()
endforeach()
if(NOT _session_source_contents MATCHES "jh_secure_random_bytes" OR
   NOT _session_source_contents MATCHES "jh_secure_zeroize")
    message(FATAL_ERROR
        "Serial Session bypasses shared entropy or secure zeroization")
endif()
if(_session_source_contents MATCHES "hal_sha256|hal_micros64")
    message(FATAL_ERROR
        "Deterministic Serial Session challenge derivation returned")
endif()

file(READ "${_auth_header}" _auth_header_contents)
if(_auth_header_contents MATCHES "static[ \t\r\n]+inline")
    message(FATAL_ERROR "Authentication implementation returned to header")
endif()
file(READ "${_auth_source}" _auth_source_contents)
if(NOT _auth_source_contents MATCHES "jh_constant_time_compare" OR
   NOT _auth_source_contents MATCHES "jh_secure_zeroize")
    message(FATAL_ERROR
        "SC authentication bypasses shared security primitives")
endif()

file(READ "${_security_source}" _security_source_contents)
foreach(_primitive IN ITEMS
        jh_secure_random_bytes
        jh_secure_zeroize
        jh_constant_time_compare)
    if(NOT _security_source_contents MATCHES "${_primitive}[ \t\r\n]*\\(")
        message(FATAL_ERROR "Shared security primitive is missing: ${_primitive}")
    endif()
endforeach()

foreach(_ble_file IN ITEMS "${_ble_api}" "${_ble_session}")
    file(READ "${_ble_file}" _ble_contents)
    if(NOT _ble_contents MATCHES "jh_secure_zeroize")
        message(FATAL_ERROR "BLE Stream bypasses shared zeroization: ${_ble_file}")
    endif()
    if(_ble_contents MATCHES
       "(static[ \t\r\n]+)?void[ \t\r\n]+zeroize[ \t\r\n]*\\(" OR
       _ble_contents MATCHES "jh_ble_stream_equal_ct")
        message(FATAL_ERROR
            "BLE Stream contains a local security primitive: ${_ble_file}")
    endif()
endforeach()
file(READ "${_ble_session}" _ble_session_contents)
if(NOT _ble_session_contents MATCHES "jh_constant_time_compare")
    message(FATAL_ERROR "BLE Stream bypasses shared constant-time comparison")
endif()

file(READ "${JH_ROOT}/CMakeLists.txt" _root_cmake)
file(READ "${JH_ROOT}/stm32_lib/CMakeLists.txt" _stm32_cmake)
if(NOT _root_cmake MATCHES "hal/serial/hal_serial_session\\.cpp" OR
   NOT _root_cmake MATCHES "hal/security/hal_sc_auth\\.cpp" OR
   NOT _stm32_cmake MATCHES "hal/\\*\\.cpp")
    message(FATAL_ERROR
        "Compiled Serial Session/auth sources are missing from a manifest")
endif()
