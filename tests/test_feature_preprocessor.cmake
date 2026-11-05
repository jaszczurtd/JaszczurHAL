cmake_minimum_required(VERSION 3.22)
# hal_config.h and the generated feature header through the C preprocessor
# (GCC or Clang): the closure of every implying feature matches between both,
# the direct compiler keeps presence semantics for =0, and the facade of
# hal_config.h exposes its closures and rejects invalid configurations.
if(NOT JH_ROOT OR NOT JH_CC OR NOT JH_TEST_BINARY_DIR)
    message(FATAL_ERROR "JH_ROOT, JH_CC and JH_TEST_BINARY_DIR are required")
endif()
include("${JH_ROOT}/cmake/jh_jhbase.cmake")
include("${JH_ROOT}/cmake/jh_project_features.cmake")
jh_jhbase_dir(_jhbase)
set(_work "${JH_TEST_BINARY_DIR}/feature_preprocessor")
file(REMOVE_RECURSE "${_work}")
file(MAKE_DIRECTORY "${_work}")

# Preprocess the lines in ARGN with the HAL include directories; with DUMP the
# macro dump (-dM). Sets <out_code> and <out_text> (stdout and stderr).
function(_preprocess out_code out_text dump)
    list(JOIN ARGN "\n" _text)
    file(WRITE "${_work}/probe.c" "${_text}\n")
    set(_flags -std=c11 -E -x c)
    if(dump)
        list(APPEND _flags -dM)
    endif()
    execute_process(COMMAND "${JH_CC}" ${_flags} -I "${JH_ROOT}/src" -I "${_jhbase}/include"
            "${_work}/probe.c"
        RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    set(${out_code} "${_code}" PARENT_SCOPE)
    set(${out_text} "${_out}${_err}" PARENT_SCOPE)
endfunction()

# The #define lines of NAME=VALUE definitions.
function(_definitions out)
    set(_lines "")
    foreach(_definition IN LISTS ARGN)
        string(FIND "${_definition}" "=" _equal)
        if(_equal EQUAL -1)
            list(APPEND _lines "#define ${_definition}")
        else()
            string(SUBSTRING "${_definition}" 0 ${_equal} _name)
            math(EXPR _value_start "${_equal} + 1")
            string(SUBSTRING "${_definition}" ${_value_start} -1 _value)
            list(APPEND _lines "#define ${_name} ${_value}")
        endif()
    endforeach()
    set(${out} "${_lines}" PARENT_SCOPE)
endfunction()

# The HAL_ENABLE_* macros a dump defines.
function(_enabled_in out text)
    string(REGEX MATCHALL "#define HAL_ENABLE_[A-Z0-9_]+[ \n]" _macros "${text}\n")
    string(REGEX REPLACE "#define |[ \n]" "" _macros "${_macros}")
    list(REMOVE_DUPLICATES _macros)
    list(SORT _macros)
    set(${out} "${_macros}" PARENT_SCOPE)
endfunction()

# The HAL_ENABLE_* macros hal_config.h defines for the definitions in ARGN.
function(_hal_config_features out)
    _definitions(_lines ${ARGN})
    _preprocess(_code _text TRUE ${_lines} "#include \"hal/core/hal_config.h\"")
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "hal_config.h probe failed for ${ARGN}:\n${_text}")
    endif()
    _enabled_in(_features "${_text}")
    set(${out} "${_features}" PARENT_SCOPE)
endfunction()

function(_require_features label have)
    foreach(_feature IN LISTS ARGN)
        if(NOT _feature IN_LIST have)
            message(FATAL_ERROR "hal_config.h did not expose ${_feature} for ${label}")
        endif()
    endforeach()
endfunction()

# hal_config.h stops with message for the definitions in ARGN.
function(_hal_config_rejects message)
    _definitions(_lines ${ARGN})
    _preprocess(_code _text FALSE ${_lines} "#include \"hal/core/hal_config.h\"")
    string(FIND "${_text}" "${message}" _found)
    if(_code EQUAL 0 OR _found EQUAL -1)
        message(FATAL_ERROR "hal_config.h did not report '${message}' for ${ARGN}:\n${_text}")
    endif()
endfunction()

# The verbose report of hal_config.h for the definitions in ARGN.
function(_hal_config_verbose out)
    _definitions(_lines ${ARGN})
    _preprocess(_code _text FALSE "#define HAL_CONFIG_VERBOSE 1" ${_lines}
        "#include \"hal/core/hal_config.h\"")
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "verbose hal_config.h probe failed for ${ARGN}:\n${_text}")
    endif()
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()

# ---- Closures: hal_config.h against the generated header ----

foreach(_symbol IN LISTS JH_HAL_FEATURE_SYMBOLS)
    if("${JH_HAL_FEATURE_${_symbol}_IMPLIES}" STREQUAL "")
        continue()
    endif()
    set(_support "")
    if(_symbol STREQUAL "HAL_ENABLE_TFT")
        set(_support "#define HAL_ENABLE_ILI9341 1")
    elseif(_symbol STREQUAL "HAL_ENABLE_LORA_LINK" OR _symbol STREQUAL "HAL_ENABLE_LORA_COMMANDS")
        set(_support "#define HAL_ENABLE_SX126X 1")
    endif()
    set(_target HAL_TARGET_MOCK)
    if(_symbol STREQUAL "HAL_ENABLE_STM32G474_FDCAN")
        set(_target HAL_TARGET_STM32G474)
    endif()
    set(_closures "")
    foreach(_header IN ITEMS hal/core/hal_config.h hal/generated/jh_hal_features.h)
        _preprocess(_code _text TRUE "#define HAL_EEPROM_TYPE 4" "#define ${_target} 1"
            "#define ${_symbol} 1" ${_support} "#include \"${_header}\"")
        if(NOT _code EQUAL 0)
            message(FATAL_ERROR "preprocessor failed for ${_symbol} through ${_header}:\n${_text}")
        endif()
        _enabled_in(_features "${_text}")
        set(_registered "")
        foreach(_feature IN LISTS _features)
            if(_feature IN_LIST JH_HAL_FEATURE_SYMBOLS)
                list(APPEND _registered "${_feature}")
            endif()
        endforeach()
        list(JOIN _registered "," _registered)
        list(APPEND _closures "${_registered}")
    endforeach()
    list(GET _closures 0 _legacy)
    list(GET _closures 1 _generated)
    if(NOT _legacy STREQUAL _generated)
        message(FATAL_ERROR "The closure of ${_symbol} differs: hal_config.h ${_legacy}, "
            "generated header ${_generated}")
    endif()
endforeach()

# ---- Presence semantics ----

# The direct compiler keeps the unsupported =0 spelling and still applies
# presence semantics to what the symbol implies.
_preprocess(_code _text TRUE "#define HAL_TARGET_MOCK 1" "#define HAL_ENABLE_TIME 0"
    "#include \"hal/core/hal_config.h\"")
if(NOT _code EQUAL 0 OR NOT _text MATCHES "#define HAL_ENABLE_TIME 0\n")
    message(FATAL_ERROR "The direct compiler changed the unsupported =0 spelling:\n${_text}")
endif()
foreach(_implied IN ITEMS HAL_ENABLE_UDP HAL_ENABLE_WIFI)
    if(NOT _text MATCHES "#define ${_implied}[ \n]")
        message(FATAL_ERROR "The direct compiler no longer applies presence semantics to ${_implied}")
    endif()
endforeach()

# ---- The facade of hal_config.h ----

_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_SERIAL_COMMANDS=1)
_require_features("Serial commands" "${_have}" HAL_ENABLE_COMMAND_ROUTER HAL_ENABLE_SERIAL_COMMANDS)
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_BLE_COMMANDS=1)
_require_features("BLE commands" "${_have}" HAL_ENABLE_BLE HAL_ENABLE_BLE_COMMANDS
    HAL_ENABLE_BLE_STREAM HAL_ENABLE_COMMAND_ROUTER HAL_ENABLE_CRYPTO)
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_BLE_STREAM=1)
_require_features("BLE Stream" "${_have}" HAL_ENABLE_BLE_STREAM HAL_ENABLE_BLE HAL_ENABLE_CRYPTO)
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_BLUETOOTH_GAMEPAD=1)
_require_features("Bluetooth gamepad" "${_have}" HAL_ENABLE_BLUETOOTH_GAMEPAD
    HAL_ENABLE_BLUETOOTH_HID_HOST HAL_ENABLE_BLUETOOTH_CLASSIC)
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_SX126X=1)
_require_features("SX126x" "${_have}" HAL_ENABLE_SX126X HAL_ENABLE_LORA HAL_ENABLE_SPI)
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_SX127X=1)
_require_features("SX127x" "${_have}" HAL_ENABLE_SX127X HAL_ENABLE_LORA HAL_ENABLE_SPI)
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_SX126X=1 HAL_ENABLE_LORA_LINK=1)
_require_features("LoRa link" "${_have}" HAL_ENABLE_LORA_LINK HAL_ENABLE_LORA HAL_ENABLE_CRC)
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_SX126X=1 HAL_ENABLE_LORA_COMMANDS=1)
_require_features("LoRa commands" "${_have}" HAL_ENABLE_COMMAND_ROUTER
    HAL_ENABLE_LORA_COMMANDS HAL_ENABLE_LORA_LINK HAL_ENABLE_LORA HAL_ENABLE_CRC)

_hal_config_rejects("HAL_ENABLE_BLE_STREAM is not supported by the ESP32-S3 NimBLE backend"
    HAL_TARGET_ESP32_S3=1 HAL_ENABLE_BLE_STREAM=1)
_hal_config_rejects("HAL_ENABLE_SX126X conflicts with HAL_ENABLE_SX127X"
    HAL_TARGET_MOCK=1 HAL_ENABLE_SX126X=1 HAL_ENABLE_SX127X=1)
_hal_config_rejects("HAL_ENABLE_LORA requires exactly one provider" HAL_TARGET_MOCK=1 HAL_ENABLE_LORA=1)
foreach(_value 0 256)
    _hal_config_rejects("HAL_LORA_RADIO_MAX_INSTANCES must be in range 1..255"
        HAL_TARGET_MOCK=1 HAL_ENABLE_SX126X=1 HAL_LORA_RADIO_MAX_INSTANCES=${_value})
endforeach()
_hal_config_rejects("HAL_LORA_LINK_MAX_INSTANCES must be in range 1..255"
    HAL_TARGET_MOCK=1 HAL_ENABLE_SX126X=1 HAL_ENABLE_LORA_LINK=1 HAL_LORA_LINK_MAX_INSTANCES=0)
_hal_config_rejects("HAL_LORA_LINK_MAX_MESSAGE_SIZE must be in range 1..4096"
    HAL_TARGET_MOCK=1 HAL_ENABLE_SX126X=1 HAL_ENABLE_LORA_LINK=1 HAL_LORA_LINK_MAX_MESSAGE_SIZE=4097)
_hal_config_rejects("HAL_LORA_LINK_MAX_PEERS must be in range 1..32"
    HAL_TARGET_MOCK=1 HAL_ENABLE_SX126X=1 HAL_ENABLE_LORA_LINK=1 HAL_LORA_LINK_MAX_PEERS=33)
_hal_config_rejects("HAL_COMMAND_ROUTER_MAX_INSTANCES must be in range 1..16"
    HAL_TARGET_MOCK=1 HAL_COMMAND_ROUTER_MAX_INSTANCES=0)
foreach(_timeout 0 1000001)
    _hal_config_rejects("HAL_RP_I2C_TIMEOUT_US must be in 1..1000000"
        HAL_TARGET_MOCK=1 HAL_RP_I2C_TIMEOUT_US=${_timeout})
endforeach()
foreach(_transfer 63 1048577)
    _hal_config_rejects("HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES must be 0 or in range 64..1048576"
        HAL_TARGET_MOCK=1 HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES=${_transfer})
endforeach()
foreach(_transfer 0 64 1048576)
    _hal_config_verbose(_text HAL_TARGET_MOCK=1 HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES=${_transfer})
endforeach()
_hal_config_rejects("HAL_COMMAND_ROUTER_MAX_COMMANDS must be in range 1..64"
    HAL_TARGET_MOCK=1 HAL_COMMAND_ROUTER_MAX_COMMANDS=65)
_hal_config_rejects("HAL_COMMAND_ROUTER_NAME_MAX must be in range 2..256"
    HAL_TARGET_MOCK=1 HAL_COMMAND_ROUTER_NAME_MAX=1)
_hal_config_rejects("HAL_COMMAND_MESSAGE_MAX_PAYLOAD must be in range 1..65535"
    HAL_TARGET_MOCK=1 HAL_COMMAND_MESSAGE_MAX_PAYLOAD=65536)
foreach(_value 0 60001)
    _hal_config_rejects("HAL_LORA_SX126X_BUSY_TIMEOUT_MS must be in range 1..60000"
        HAL_TARGET_MOCK=1 HAL_ENABLE_SX126X=1 HAL_LORA_SX126X_BUSY_TIMEOUT_MS=${_value})
endforeach()

# The residual rules: an AT24 EEPROM brings I2C, flash does not; GPS brings
# UART unless it uses the software serial.
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_EEPROM_TYPE=1 HAL_ENABLE_EEPROM=1)
_require_features("AT24C256 EEPROM" "${_have}" HAL_ENABLE_I2C)
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_EEPROM_TYPE=4 HAL_ENABLE_EEPROM=1)
if(HAL_ENABLE_I2C IN_LIST _have)
    message(FATAL_ERROR "A flash EEPROM enables I2C")
endif()
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_GPS=1)
_require_features("GPS" "${_have}" HAL_ENABLE_UART)
_hal_config_features(_have HAL_TARGET_MOCK=1 HAL_ENABLE_GPS=1 HAL_ENABLE_SWSERIAL=1)
if(HAL_ENABLE_UART IN_LIST _have)
    message(FATAL_ERROR "GPS with the software serial enables UART")
endif()

# The verbose report names what the residual rules and the closures add.
foreach(_case
        "HAL_ENABLE_GPS|HAL_ENABLE_GPS;HAL_ENABLE_UART"
        "HAL_ENABLE_BLE_STREAM|HAL_ENABLE_BLE_STREAM;HAL_ENABLE_BLE;HAL_ENABLE_CRYPTO"
        "HAL_ENABLE_BLE_COMMANDS|HAL_ENABLE_BLE;HAL_ENABLE_BLE_COMMANDS;HAL_ENABLE_BLE_STREAM;HAL_ENABLE_COMMAND_ROUTER;HAL_ENABLE_CRYPTO")
    string(FIND "${_case}" "|" _bar)
    string(SUBSTRING "${_case}" 0 ${_bar} _request)
    math(EXPR _rest "${_bar} + 1")
    string(SUBSTRING "${_case}" ${_rest} -1 _reported)
    _hal_config_verbose(_text HAL_TARGET_MOCK=1 ${_request}=1)
    foreach(_feature IN LISTS _reported)
        string(FIND "${_text}" "HAL_CONFIG: ${_feature}" _found)
        if(_found EQUAL -1)
            message(FATAL_ERROR "The verbose report for ${_request} omitted ${_feature}")
        endif()
    endforeach()
endforeach()
