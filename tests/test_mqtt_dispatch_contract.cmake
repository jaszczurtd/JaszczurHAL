if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_mqtt_source "${JH_ROOT}/src/hal/network/mqtt/hal_mqtt.cpp")
file(READ "${_mqtt_source}" _mqtt_text)

foreach(_required IN ITEMS
        "static hal_mqtt_rx_slot_t s_dispatch_slot"
        "static bool s_dispatch_active"
        "if (!s_dispatch_active)"
        "s_dispatch_active = false")
    string(FIND "${_mqtt_text}" "${_required}" _required_at)
    if(_required_at EQUAL -1)
        message(FATAL_ERROR
            "MQTT serialized dispatch contract is missing: ${_required}")
    endif()
endforeach()

foreach(_forbidden IN ITEMS "topic_copy" "payload_copy")
    string(FIND "${_mqtt_text}" "${_forbidden}" _forbidden_at)
    if(NOT _forbidden_at EQUAL -1)
        message(FATAL_ERROR
            "MQTT callback dispatch buffer returned to the stack: ${_forbidden}")
    endif()
endforeach()
