if(NOT JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

include("${JH_ROOT}/cmake/generated/jh_hal_features.cmake")

if(NOT JH_HAL_FEATURE_SCHEMA_VERSION EQUAL 1)
    message(FATAL_ERROR "Unexpected feature schema version")
endif()
if(NOT JH_HAL_FEATURE_GENERATOR_VERSION EQUAL 1)
    message(FATAL_ERROR "Unexpected feature generator version")
endif()
if(NOT JH_HAL_FEATURE_REGISTRY_DIGEST MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "Feature registry digest is not hexadecimal")
endif()
string(LENGTH "${JH_HAL_FEATURE_REGISTRY_DIGEST}" _digest_length)
if(NOT _digest_length EQUAL 64)
    message(FATAL_ERROR "Feature registry digest must contain 64 characters")
endif()

list(LENGTH JH_HAL_FEATURE_SYMBOLS _symbol_count)
if(NOT _symbol_count EQUAL 93)
    message(FATAL_ERROR "Expected 93 registered symbols, got ${_symbol_count}")
endif()
if(NOT "${JH_HAL_FEATURE_DERIVED_SYMBOLS}" STREQUAL
       "HAL_ENABLE_NETWORK_CORE")
    message(FATAL_ERROR "Unexpected derived feature set")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_MQTT_IMPLIES}" STREQUAL
       "HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_TCP;HAL_ENABLE_WIFI")
    message(FATAL_ERROR "MQTT direct dependency table drifted")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_NET_COMMANDS_TRANSITIVE_IMPLIES}" STREQUAL
       "HAL_ENABLE_CJSON;HAL_ENABLE_HTTP_SERVER;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_TCP;HAL_ENABLE_WEBSOCKET;HAL_ENABLE_WIFI")
    message(FATAL_ERROR "NET_COMMANDS transitive dependency table drifted")
endif()

jh_hal_resolve_features(_requested _resolved
    HAL_ENABLE_MQTT HAL_ENABLE_BLE_STREAM=1)
if(NOT "${_requested}" STREQUAL
       "HAL_ENABLE_BLE_STREAM;HAL_ENABLE_MQTT")
    message(FATAL_ERROR "Unexpected requested feature set: ${_requested}")
endif()
if(NOT "${_resolved}" STREQUAL
       "HAL_ENABLE_BLE;HAL_ENABLE_BLE_STREAM;HAL_ENABLE_CRYPTO;HAL_ENABLE_MQTT;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_TCP;HAL_ENABLE_WIFI")
    message(FATAL_ERROR "Unexpected resolved feature set: ${_resolved}")
endif()
