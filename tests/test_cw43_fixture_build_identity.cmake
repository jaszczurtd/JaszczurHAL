if(NOT DEFINED JH_CW43_FIXTURE_SETUP_SCRIPT OR
   JH_CW43_FIXTURE_SETUP_SCRIPT STREQUAL "")
    message(FATAL_ERROR "JH_CW43_FIXTURE_SETUP_SCRIPT is required")
endif()

set(_script "${JH_CW43_FIXTURE_SETUP_SCRIPT}")
if(NOT EXISTS "${_script}")
    message(FATAL_ERROR "Missing CW43 setup script: ${_script}")
endif()

file(READ "${_script}" _contents)
foreach(_required IN ITEMS
        "configure_build_identity"
        "SUDO_USER"
        "BUILD_USER_HOME}/.local/bin"
        "sudo -u \"\${BUILD_USER}\""
        "run_firmware_build")
    string(FIND "${_contents}" "${_required}" _required_offset)
    if(_required_offset EQUAL -1)
        message(FATAL_ERROR "Missing privilege-safe build behavior: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
        "GENERATED_TLS_CA_HEADER"
        "start_service tls_server python3"
        "HTTP_FIXTURE}\" serve"
        "--cert \"\${STATE_DIR}/tls-server.pem\""
        "--key \"\${STATE_DIR}/tls-server-key.pem\""
        "HAL_CW43_TEST_TLS_PORT"
        "HAL_CW43_TEST_MQTTS_PORT"
        "mqtt_tls"
        "tls-server.pem")
    string(FIND "${_contents}" "${_required}" _required_offset)
    if(_required_offset EQUAL -1)
        message(FATAL_ERROR "Missing deterministic TLS fixture behavior: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
        "NTP_FIXTURE"
        "cw43_test_ntp_server.py"
        "HAL_CW43_TEST_NTP_HOST"
        "HAL_CW43_TEST_NTP_PORT"
        "start_service ntp python3")
    string(FIND "${_contents}" "${_required}" _required_offset)
    if(_required_offset EQUAL -1)
        message(FATAL_ERROR "Missing deterministic NTP fixture behavior: ${_required}")
    endif()
endforeach()

if(_contents MATCHES
   "log \"building firmware[^\n]*\n[ \t]*\"\${JH_VSCODE}\" build")
    message(FATAL_ERROR "CW43 setup still builds firmware directly as root")
endif()
