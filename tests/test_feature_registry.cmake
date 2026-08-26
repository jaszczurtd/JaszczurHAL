if(NOT JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

include("${JH_ROOT}/cmake/jh_project_features.cmake")

if(NOT JH_HAL_FEATURE_SCHEMA_VERSION EQUAL 1)
    message(FATAL_ERROR "Unexpected feature schema version")
endif()
if(NOT JH_HAL_FEATURE_GENERATOR_VERSION EQUAL 2)
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
if(NOT _symbol_count EQUAL 103)
    message(FATAL_ERROR "Expected 103 registered symbols, got ${_symbol_count}")
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
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_NOTIFY_TELEGRAM_TRANSITIVE_IMPLIES}" STREQUAL
       "HAL_ENABLE_CJSON;HAL_ENABLE_HTTP_CLIENT;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_NOTIFY;HAL_ENABLE_TCP;HAL_ENABLE_TLS;HAL_ENABLE_WIFI")
    message(FATAL_ERROR "NOTIFY_TELEGRAM transitive dependency table drifted")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_SX126X_TRANSITIVE_IMPLIES}" STREQUAL
       "HAL_ENABLE_LORA;HAL_ENABLE_SPI")
    message(FATAL_ERROR "SX126X transitive dependency table drifted")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_SX127X_TRANSITIVE_IMPLIES}" STREQUAL
       "HAL_ENABLE_LORA;HAL_ENABLE_SPI")
    message(FATAL_ERROR "SX127X transitive dependency table drifted")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_TLS_BUILD_EFFECT_DEPENDENCIES}" STREQUAL
       "bearssl")
    message(FATAL_ERROR "TLS managed build dependency drifted")
endif()
jh_hal_resolve_build_effects(
    _effect_sources _portable_sources _effect_dependencies
    HAL_ENABLE_LITTLEFS HAL_ENABLE_MQTT HAL_ENABLE_TLS HAL_ENABLE_UNITY)
if(NOT "${_effect_dependencies}" STREQUAL "bearssl;littlefs")
    message(FATAL_ERROR "Resolved managed build dependencies drifted")
endif()
list(FIND _effect_sources "src/utils/unity.c" _unity_source_index)
if(_unity_source_index EQUAL -1)
    message(FATAL_ERROR "Unity feature source effect is missing")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_LORA_LINK_TRANSITIVE_IMPLIES}" STREQUAL
       "HAL_ENABLE_CRC;HAL_ENABLE_LORA")
    message(FATAL_ERROR "LORA_LINK transitive dependency table drifted")
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

foreach(_target IN ITEMS rp2040 rp2350-arm rp2350-riscv stm32g474)
    jh_all_features_for_target(_all_features "${_target}")
    foreach(_required IN ITEMS
            HAL_ENABLE_FREERTOS
            HAL_ENABLE_LORA_LINK
            HAL_ENABLE_STACK_PROTECTOR
            HAL_ENABLE_SX127X
            HAL_DISPLAY_ILI9341)
        list(FIND _all_features "${_required}" _required_index)
        if(_required_index EQUAL -1)
            message(FATAL_ERROR
                "All-features profile for ${_target} omitted ${_required}")
        endif()
    endforeach()
    list(FIND _all_features HAL_ENABLE_SX126X _sx126x_index)
    if(NOT _sx126x_index EQUAL -1)
        message(FATAL_ERROR
            "All-features profile for ${_target} selected both LoRa providers")
    endif()
endforeach()

jh_all_features_for_target(_rp2040_features rp2040)
list(FIND _rp2040_features HAL_ENABLE_BLE _rp2040_ble_index)
list(FIND _rp2040_features HAL_ENABLE_OTA _rp2040_ota_index)
if(_rp2040_ble_index EQUAL -1 OR _rp2040_ota_index EQUAL -1)
    message(FATAL_ERROR "RP2040 all-features profile omitted BLE or OTA")
endif()

jh_all_features_for_target(_rp2350_arm_features rp2350-arm)
list(FIND _rp2350_arm_features HAL_ENABLE_BLE _rp2350_arm_ble_index)
list(FIND _rp2350_arm_features HAL_ENABLE_OTA _rp2350_arm_ota_index)
if(_rp2350_arm_ble_index EQUAL -1 OR _rp2350_arm_ota_index EQUAL -1)
    message(FATAL_ERROR "RP2350 ARM all-features profile omitted BLE or OTA")
endif()

jh_all_features_for_target(_rp2350_riscv_features rp2350-riscv)
foreach(_unsupported IN ITEMS HAL_ENABLE_BLE HAL_ENABLE_OTA)
    list(FIND _rp2350_riscv_features "${_unsupported}" _unsupported_index)
    if(NOT _unsupported_index EQUAL -1)
        message(FATAL_ERROR
            "RP2350 RISC-V all-features profile retained ${_unsupported}")
    endif()
endforeach()

jh_all_features_for_target(_stm32_features stm32g474)
foreach(_supported IN ITEMS HAL_ENABLE_BLE HAL_ENABLE_OTA HAL_ENABLE_STM32G474_FDCAN)
    list(FIND _stm32_features "${_supported}" _supported_index)
    if(_supported_index EQUAL -1)
        message(FATAL_ERROR
            "STM32G474 all-features profile omitted ${_supported}")
    endif()
endforeach()
