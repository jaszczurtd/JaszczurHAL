cmake_minimum_required(VERSION 3.22)
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
if(NOT _symbol_count EQUAL 116)
    message(FATAL_ERROR "Expected 116 registered symbols, got ${_symbol_count}")
endif()
if(NOT "${JH_HAL_FEATURE_DERIVED_SYMBOLS}" STREQUAL
       "HAL_ENABLE_NETWORK_CORE")
    message(FATAL_ERROR "Unexpected derived feature set")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_BLUETOOTH_GAMEPAD_IMPLIES}" STREQUAL
       "HAL_ENABLE_BLUETOOTH_HID_HOST" OR
   NOT "${JH_HAL_FEATURE_HAL_ENABLE_BLUETOOTH_GAMEPAD_TRANSITIVE_IMPLIES}" STREQUAL
       "HAL_ENABLE_BLUETOOTH_CLASSIC;HAL_ENABLE_BLUETOOTH_HID_HOST;HAL_ENABLE_CRC" OR
   NOT "${JH_HAL_FEATURE_HAL_ENABLE_BLUETOOTH_HID_HOST_IMPLIES}" STREQUAL
       "HAL_ENABLE_BLUETOOTH_CLASSIC")
    message(FATAL_ERROR "Bluetooth gamepad dependency table drifted")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_MQTT_IMPLIES}" STREQUAL
       "HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_TCP;HAL_ENABLE_WIFI")
    message(FATAL_ERROR "MQTT direct dependency table drifted")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_NET_COMMANDS_TRANSITIVE_IMPLIES}" STREQUAL
       "HAL_ENABLE_CJSON;HAL_ENABLE_COMMAND_ROUTER;HAL_ENABLE_HTTP_SERVER;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_TCP;HAL_ENABLE_WEBSOCKET;HAL_ENABLE_WIFI")
    message(FATAL_ERROR "NET_COMMANDS transitive dependency table drifted")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_BLE_COMMANDS_IMPLIES}" STREQUAL
       "HAL_ENABLE_BLE_STREAM;HAL_ENABLE_COMMAND_ROUTER" OR
   NOT "${JH_HAL_FEATURE_HAL_ENABLE_BLE_COMMANDS_TRANSITIVE_IMPLIES}" STREQUAL
       "HAL_ENABLE_BLE;HAL_ENABLE_BLE_STREAM;HAL_ENABLE_COMMAND_ROUTER;HAL_ENABLE_CRYPTO")
    message(FATAL_ERROR "BLE_COMMANDS dependency table drifted")
endif()
if(NOT "${JH_HAL_FEATURE_HAL_ENABLE_SERIAL_COMMANDS_IMPLIES}" STREQUAL
       "HAL_ENABLE_COMMAND_ROUTER" OR
   NOT "${JH_HAL_FEATURE_HAL_ENABLE_SERIAL_COMMANDS_TRANSITIVE_IMPLIES}"
       STREQUAL "HAL_ENABLE_COMMAND_ROUTER")
    message(FATAL_ERROR "SERIAL_COMMANDS dependency table drifted")
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
    HAL_ENABLE_BLE_COMMANDS HAL_ENABLE_BLUETOOTH_GAMEPAD
    HAL_ENABLE_LITTLEFS HAL_ENABLE_MQTT
    HAL_ENABLE_SERIAL_COMMANDS HAL_ENABLE_TLS HAL_ENABLE_UNITY)
if(NOT "${_effect_dependencies}" STREQUAL "bearssl;littlefs")
    message(FATAL_ERROR "Resolved managed build dependencies drifted")
endif()
list(FIND _effect_sources "src/utils/unity.c" _unity_source_index)
if(_unity_source_index EQUAL -1)
    message(FATAL_ERROR "Unity feature source effect is missing")
endif()
list(FIND _portable_sources
    "src/hal/bluetooth/hal_ble_commands.cpp" _ble_commands_source_index)
if(_ble_commands_source_index EQUAL -1)
    message(FATAL_ERROR "BLE command portable source effect is missing")
endif()
list(FIND _portable_sources
    "src/hal/bluetooth/hal_gamepad.cpp" _gamepad_source_index)
if(_gamepad_source_index EQUAL -1)
    message(FATAL_ERROR "Bluetooth gamepad portable source effect is missing")
endif()
list(FIND _portable_sources
    "src/hal/serial/hal_serial_commands.cpp" _serial_commands_source_index)
if(_serial_commands_source_index EQUAL -1)
    message(FATAL_ERROR "Serial command portable source effect is missing")
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

jh_hal_resolve_features(_ble_commands_requested _ble_commands_resolved
    HAL_ENABLE_BLE_COMMANDS)
if(NOT "${_ble_commands_requested}" STREQUAL "HAL_ENABLE_BLE_COMMANDS" OR
   NOT "${_ble_commands_resolved}" STREQUAL
       "HAL_ENABLE_BLE;HAL_ENABLE_BLE_COMMANDS;HAL_ENABLE_BLE_STREAM;HAL_ENABLE_COMMAND_ROUTER;HAL_ENABLE_CRYPTO")
    message(FATAL_ERROR
        "Unexpected BLE command feature set: ${_ble_commands_resolved}")
endif()

jh_hal_resolve_features(_gamepad_requested _gamepad_resolved
    HAL_ENABLE_BLUETOOTH_GAMEPAD)
if(NOT "${_gamepad_requested}" STREQUAL
       "HAL_ENABLE_BLUETOOTH_GAMEPAD" OR
   NOT "${_gamepad_resolved}" STREQUAL
       "HAL_ENABLE_BLUETOOTH_CLASSIC;HAL_ENABLE_BLUETOOTH_GAMEPAD;HAL_ENABLE_BLUETOOTH_HID_HOST;HAL_ENABLE_CRC")
    message(FATAL_ERROR
        "Unexpected Bluetooth gamepad feature set: ${_gamepad_resolved}")
endif()

jh_hal_resolve_features(_serial_commands_requested _serial_commands_resolved
    HAL_ENABLE_SERIAL_COMMANDS)
if(NOT "${_serial_commands_requested}" STREQUAL
       "HAL_ENABLE_SERIAL_COMMANDS" OR
   NOT "${_serial_commands_resolved}" STREQUAL
       "HAL_ENABLE_COMMAND_ROUTER;HAL_ENABLE_SERIAL_COMMANDS")
    message(FATAL_ERROR
        "Unexpected Serial command feature set: ${_serial_commands_resolved}")
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
list(FIND _rp2040_features HAL_ENABLE_BLE_COMMANDS _rp2040_ble_commands_index)
list(FIND _rp2040_features HAL_ENABLE_OTA _rp2040_ota_index)
if(_rp2040_ble_index EQUAL -1 OR _rp2040_ble_commands_index EQUAL -1 OR
   _rp2040_ota_index EQUAL -1)
    message(FATAL_ERROR
        "RP2040 all-features profile omitted BLE commands or OTA")
endif()

jh_all_features_for_target(_rp2350_arm_features rp2350-arm)
list(FIND _rp2350_arm_features HAL_ENABLE_BLE _rp2350_arm_ble_index)
list(FIND _rp2350_arm_features HAL_ENABLE_BLE_COMMANDS
    _rp2350_arm_ble_commands_index)
list(FIND _rp2350_arm_features HAL_ENABLE_OTA _rp2350_arm_ota_index)
if(_rp2350_arm_ble_index EQUAL -1 OR
   _rp2350_arm_ble_commands_index EQUAL -1 OR
   _rp2350_arm_ota_index EQUAL -1)
    message(FATAL_ERROR
        "RP2350 ARM all-features profile omitted BLE commands or OTA")
endif()

jh_all_features_for_target(_rp2350_riscv_features rp2350-riscv)
foreach(_unsupported IN ITEMS
        HAL_ENABLE_BLE
        HAL_ENABLE_BLE_COMMANDS
        HAL_ENABLE_BLE_STREAM
        HAL_ENABLE_OTA)
    list(FIND _rp2350_riscv_features "${_unsupported}" _unsupported_index)
    if(NOT _unsupported_index EQUAL -1)
        message(FATAL_ERROR
            "RP2350 RISC-V all-features profile retained ${_unsupported}")
    endif()
endforeach()

# A device tree brings the radio of its assembly, so the STM32 feature list
# keeps networking and Bluetooth. No STM32 board carries a radio: without a
# device tree they stay out, together with every backend define.
jh_all_features_for_target(_stm32_tree_features stm32g474 FEATURES_ONLY)
foreach(_supported IN ITEMS
        HAL_ENABLE_BLE
        HAL_ENABLE_BLE_COMMANDS
        HAL_ENABLE_OTA
        HAL_ENABLE_STM32G474_FDCAN)
    list(FIND _stm32_tree_features "${_supported}" _supported_index)
    if(_supported_index EQUAL -1)
        message(FATAL_ERROR
            "STM32G474 device-tree feature list omitted ${_supported}")
    endif()
endforeach()

jh_all_features_for_target(_stm32_features stm32g474)
list(FIND _stm32_features HAL_ENABLE_STM32G474_FDCAN _stm32_fdcan_index)
if(_stm32_fdcan_index EQUAL -1)
    message(FATAL_ERROR "STM32G474 all-features profile omitted FDCAN")
endif()
foreach(_radio IN ITEMS
        HAL_ENABLE_NETWORK_CORE
        HAL_ENABLE_WIFI
        HAL_ENABLE_OTA
        HAL_ENABLE_BLE
        HAL_ENABLE_BLE_COMMANDS
        HAL_ENABLE_BLUETOOTH_CLASSIC)
    list(FIND _stm32_features "${_radio}" _radio_index)
    if(NOT _radio_index EQUAL -1)
        message(FATAL_ERROR
            "STM32G474 all-features profile kept ${_radio} without a radio")
    endif()
endforeach()
foreach(_feature IN LISTS _stm32_features)
    if(_feature MATCHES "^HAL_(NETWORK_BACKEND|CYW43)_")
        message(FATAL_ERROR
            "STM32G474 all-features profile defines ${_feature} without a radio")
    endif()
endforeach()

# ---- The registry against the documentation and the sources ----

# Every symbol a project may request is in the public catalog, and only those.
file(STRINGS "${JH_ROOT}/doc/api/en/02_module_flags.md" _catalog_rows
    REGEX "^\\| `HAL_(ENABLE|DISABLE)_[A-Z0-9_]+`[ ]+\\|")
set(_catalog "")
foreach(_row IN LISTS _catalog_rows)
    string(REGEX MATCH "HAL_(ENABLE|DISABLE)_[A-Z0-9_]+" _symbol "${_row}")
    list(APPEND _catalog "${_symbol}")
endforeach()
set(_public ${JH_HAL_FEATURE_SYMBOLS})
list(REMOVE_ITEM _public ${JH_HAL_FEATURE_DERIVED_SYMBOLS})
list(SORT _catalog)
list(REMOVE_DUPLICATES _catalog)
if(NOT "${_catalog}" STREQUAL "${_public}")
    message(FATAL_ERROR "The public feature catalog drifted from config/features:\n"
        "catalog ${_catalog}\nregistry ${_public}")
endif()

# The size of the implies graph, and the edges of the providers.
set(_implying 0)
set(_edges 0)
foreach(_symbol IN LISTS JH_HAL_FEATURE_SYMBOLS)
    list(LENGTH JH_HAL_FEATURE_${_symbol}_IMPLIES _count)
    if(_count GREATER 0)
        math(EXPR _implying "${_implying} + 1")
        math(EXPR _edges "${_edges} + ${_count}")
    endif()
endforeach()
if(NOT _implying EQUAL 75 OR NOT _edges EQUAL 134)
    message(FATAL_ERROR "The implies graph drifted: ${_implying} sources, ${_edges} edges")
endif()
foreach(_edge
        "HAL_ENABLE_KV|HAL_ENABLE_CRC;HAL_ENABLE_EEPROM"
        "HAL_ENABLE_LORA|"
        "HAL_ENABLE_SX126X|HAL_ENABLE_LORA;HAL_ENABLE_SPI"
        "HAL_ENABLE_SX127X|HAL_ENABLE_LORA;HAL_ENABLE_SPI"
        "HAL_ENABLE_LORA_LINK|HAL_ENABLE_CRC;HAL_ENABLE_LORA"
        "HAL_ENABLE_LORA_COMMANDS|HAL_ENABLE_COMMAND_ROUTER;HAL_ENABLE_LORA_LINK")
    string(FIND "${_edge}" "|" _bar)
    string(SUBSTRING "${_edge}" 0 ${_bar} _symbol)
    math(EXPR _rest "${_bar} + 1")
    string(SUBSTRING "${_edge}" ${_rest} -1 _expected)
    if(NOT "${JH_HAL_FEATURE_${_symbol}_IMPLIES}" STREQUAL "${_expected}")
        message(FATAL_ERROR "${_symbol} implies drifted: ${JH_HAL_FEATURE_${_symbol}_IMPLIES}")
    endif()
endforeach()

# The build effects of a set of features, and every registered source exists.
jh_hal_resolve_build_effects(_effect_sources _portable_sources _effect_dependencies
    HAL_ENABLE_BLE_COMMANDS HAL_ENABLE_BLUETOOTH_GAMEPAD HAL_ENABLE_LITTLEFS
    HAL_ENABLE_MQTT HAL_ENABLE_SX126X HAL_ENABLE_SERIAL_COMMANDS HAL_ENABLE_TLS
    HAL_ENABLE_UNITY)
if(NOT "${_effect_sources}" STREQUAL
        "src/hal/network/mqtt/PubSubClient/src/PubSubClient.cpp;src/utils/unity.c"
    OR NOT "${_effect_dependencies}" STREQUAL "bearssl;littlefs;sx126x")
    message(FATAL_ERROR "Feature build effects drifted: ${_effect_sources} ${_effect_dependencies}")
endif()
foreach(_source IN ITEMS src/hal/bluetooth/hal_ble_commands.cpp
        src/hal/bluetooth/hal_gamepad.cpp src/hal/serial/hal_serial_commands.cpp
        src/hal/network/mqtt/hal_mqtt.cpp src/hal/network/tls/hal_tls.cpp)
    if(NOT _source IN_LIST _portable_sources)
        message(FATAL_ERROR "The portable source effect ${_source} is missing")
    endif()
endforeach()
foreach(_symbol IN LISTS JH_HAL_FEATURE_SYMBOLS)
    foreach(_source IN LISTS JH_HAL_FEATURE_${_symbol}_BUILD_EFFECT_FEATURE_SOURCES
            JH_HAL_FEATURE_${_symbol}_BUILD_EFFECT_PORTABLE_SOURCES)
        if(NOT EXISTS "${JH_ROOT}/${_source}")
            message(FATAL_ERROR "The registered build source ${_source} is missing")
        endif()
    endforeach()
endforeach()

# The HAL sources and the hand-written feature lists use registered symbols;
# the lists request none that is derived.
file(GLOB_RECURSE _hal_sources "${JH_ROOT}/src/hal/*.c" "${JH_ROOT}/src/hal/*.cc"
    "${JH_ROOT}/src/hal/*.cpp" "${JH_ROOT}/src/hal/*.h" "${JH_ROOT}/src/hal/*.hpp")
list(FILTER _hal_sources EXCLUDE REGEX "/generated/")
function(_registry_symbols_in OUT_VAR PATTERN)
    set(_found "")
    foreach(_path IN LISTS ARGN)
        file(STRINGS "${_path}" _lines REGEX "${PATTERN}")
        foreach(_line IN LISTS _lines)
            string(REGEX MATCHALL "${PATTERN}" _matches "${_line}")
            list(APPEND _found ${_matches})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _found)
    set(${OUT_VAR} "${_found}" PARENT_SCOPE)
endfunction()
_registry_symbols_in(_source_symbols "HAL_ENABLE_[A-Z0-9_]+" ${_hal_sources})
_registry_symbols_in(_manual_symbols "HAL_(ENABLE|DISABLE)_[A-Z0-9_]+"
    "${JH_ROOT}/CMakeLists.txt" "${JH_ROOT}/tests/CMakeLists.txt" "${JH_ROOT}/runalltests.sh")
foreach(_symbol IN LISTS _source_symbols _manual_symbols)
    if(NOT _symbol IN_LIST JH_HAL_FEATURE_SYMBOLS)
        message(FATAL_ERROR "${_symbol} is used but not registered")
    endif()
endforeach()
foreach(_symbol IN LISTS _manual_symbols)
    if(_symbol IN_LIST JH_HAL_FEATURE_DERIVED_SYMBOLS)
        message(FATAL_ERROR "A hand-written feature list requests the derived ${_symbol}")
    endif()
endforeach()

# hal_config.h includes the generated graph after hal_board.h, once for the
# closure and once for the report, and keeps only its marked residual rules
# and provider checks.
file(READ "${JH_ROOT}/src/hal/core/hal_config.h" _hal_config)
if(NOT _hal_config MATCHES "#include \"hal/system/hal_board\\.h\"[^\n]*\n#define JH_HAL_FEATURE_VERBOSE_REPORT_DEFERRED 1\n#include \"hal/generated/jh_hal_features\\.h\"")
    message(FATAL_ERROR "hal_config.h must include the generated feature header after hal_board.h")
endif()
string(REGEX MATCHALL "#include \"hal/generated/jh_hal_features\\.h\"" _includes "${_hal_config}")
string(REGEX MATCHALL "residual outside feature registry v1" _residual_marks "${_hal_config}")
string(REGEX MATCHALL "(^|\n)#error[ \t\n]" _errors "${_hal_config}")
string(REGEX MATCHALL "(^|\n)#define HAL_ENABLE_[A-Z0-9_]+[ \t\n]" _definitions "${_hal_config}")
string(REGEX REPLACE "[\n \t]|#define " "" _definitions "${_definitions}")
list(LENGTH _includes _include_count)
list(LENGTH _residual_marks _residual_count)
list(LENGTH _errors _error_count)
string(FIND "${_hal_config}" "#pragma message" _pragma)
if(NOT _include_count EQUAL 2 OR NOT _residual_count EQUAL 2 OR NOT _pragma EQUAL -1
    OR NOT "${_definitions}" STREQUAL "HAL_ENABLE_I2C;HAL_ENABLE_UART" OR NOT _error_count EQUAL 81)
    message(FATAL_ERROR "hal_config.h drifted: ${_include_count} includes, ${_residual_count} "
        "residual marks, definitions ${_definitions}, ${_error_count} #error checks")
endif()
foreach(_facade IN ITEMS HAL_ENABLE_RTC HAL_ENABLE_CELLULAR_MODEM HAL_ENABLE_LORA
        HAL_ENABLE_THERMOCOUPLE HAL_ENABLE_CAN HAL_ENABLE_DIGIPOT HAL_ENABLE_GPS
        HAL_ENABLE_DISPLAY HAL_ENABLE_TFT)
    string(FIND "${_hal_config}" "#if defined(${_facade})" _check)
    if(_check EQUAL -1)
        message(FATAL_ERROR "hal_config.h lost the provider check for ${_facade}")
    endif()
endforeach()

# The tracked artifacts: the verbose report names every symbol, the header the
# closures and rules, the CMake data its build effects, and both freeze the
# schema digest.
file(READ "${JH_ROOT}/src/hal/generated/jh_hal_features.h" _header)
string(REGEX MATCHALL "#pragma message\\(\"HAL_CONFIG: HAL_(ENABLE|DISABLE)_[A-Z0-9_]+\"\\)" _reports "${_header}")
string(REGEX REPLACE "#pragma message\\(\"HAL_CONFIG: |\"\\)" "" _reports "${_reports}")
if(NOT "${_reports}" STREQUAL "${JH_HAL_FEATURE_SYMBOLS}")
    message(FATAL_ERROR "The generated verbose report differs from the registry")
endif()
foreach(_text IN ITEMS "Resolved implications of HAL_ENABLE_SERIAL_COMMANDS"
        "Resolved implications of HAL_ENABLE_BLE_COMMANDS"
        "Resolved implications of HAL_ENABLE_BLE_STREAM"
        "Resolved implications of HAL_ENABLE_BLUETOOTH_GAMEPAD"
        "Resolved implications of HAL_ENABLE_MQTT" "#define HAL_ENABLE_CRYPTO 1"
        "[JH-CFG-DERIVED]" "JH_HAL_FEATURE_SCHEMA_DIGEST \"${JH_HAL_FEATURE_SCHEMA_DIGEST}\"")
    string(FIND "${_header}" "${_text}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "The generated header lacks ${_text}")
    endif()
endforeach()
file(READ "${JH_ROOT}/cmake/generated/jh_hal_features.cmake" _cmake_data)
string(FIND "${_cmake_data}" "function(jh_hal_resolve_build_effects" _function)
if(_function EQUAL -1 OR NOT JH_HAL_FEATURE_SCHEMA_DIGEST MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "The generated CMake data lacks its build effects or schema digest")
endif()

# ---- The CMake resolver against jh ----

# The closure of every implying symbol is what jh feature list reports.
include("${JH_ROOT}/cmake/jh_project_config.cmake")
file(MAKE_DIRECTORY "${JH_TEST_BINARY_DIR}/feature_registry_cmake")
jh_project_config_tool(_jh "${JH_ROOT}" "${JH_TEST_BINARY_DIR}/feature_registry_cmake")
execute_process(COMMAND "${_jh}" feature list --root "${JH_ROOT}" --json
    RESULT_VARIABLE _code OUTPUT_VARIABLE _list ERROR_VARIABLE _error)
if(NOT _code EQUAL 0)
    message(FATAL_ERROR "jh feature list failed: ${_error}")
endif()
string(JSON _registry_digest GET "${_list}" registryDigest)
string(JSON _schema_digest GET "${_list}" registrySchemaDigest)
if(NOT _registry_digest STREQUAL JH_HAL_FEATURE_REGISTRY_DIGEST OR
   NOT _schema_digest STREQUAL JH_HAL_FEATURE_SCHEMA_DIGEST)
    message(FATAL_ERROR "The generated digests differ from jh feature list")
endif()
string(JSON _count LENGTH "${_list}" features)
math(EXPR _last "${_count} - 1")
foreach(_index RANGE ${_last})
    string(JSON _symbol GET "${_list}" features ${_index} name)
    string(JSON _closure_count LENGTH "${_list}" features ${_index} closure)
    if(_closure_count EQUAL 0)
        continue()
    endif()
    set(_expected "${_symbol}")
    math(EXPR _closure_last "${_closure_count} - 1")
    foreach(_item RANGE ${_closure_last})
        string(JSON _implied GET "${_list}" features ${_index} closure ${_item})
        list(APPEND _expected "${_implied}")
    endforeach()
    list(SORT _expected)
    jh_hal_resolve_features(_requested _resolved ${_symbol})
    if(NOT "${_resolved}" STREQUAL "${_expected}")
        message(FATAL_ERROR "${_symbol}: the CMake resolver gives ${_resolved}, jh ${_expected}")
    endif()
endforeach()

# The resolver rejects =0 and a derived request.
foreach(_case "HAL_ENABLE_WIFI=0|JH-CFG-VALUE" "HAL_ENABLE_NETWORK_CORE|JH-CFG-DERIVED")
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _request)
    list(GET _parts 1 _code_text)
    set(_script "${JH_TEST_BINARY_DIR}/feature_registry_cmake/${_code_text}.cmake")
    file(WRITE "${_script}" "include(\"${JH_ROOT}/cmake/generated/jh_hal_features.cmake\")\n"
        "jh_hal_resolve_features(requested resolved ${_request})\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -P "${_script}"
        RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(_code EQUAL 0 OR NOT "${_out}${_err}" MATCHES "\\[${_code_text}\\]")
        message(FATAL_ERROR "The CMake resolver accepted ${_request}: ${_out}${_err}")
    endif()
endforeach()
