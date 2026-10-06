# The policies of the builds that call these helpers.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()
if(NOT DEFINED JH_TEST_BINARY_DIR)
    message(FATAL_ERROR "JH_TEST_BINARY_DIR is required")
endif()

include("${JH_ROOT}/cmake/jh_project_features.cmake")
include("${JH_ROOT}/cmake/jh_project_config.cmake")

# Evaluate the header in DIRECTORY for rp2040 the way the firmware dispatcher
# does; ARGN adds VARIANT <id>.
macro(read_fixture DIRECTORY)
    jh_read_project_config(
        ROOT "${JH_ROOT}"
        CONFIG_DIR "${DIRECTORY}"
        TARGET rp2040
        OUTPUT_DIR "${DIRECTORY}/generated"
        ${ARGN})
endmacro()

function(write_fixture NAME TEXT)
    set(_jh_fixture "${JH_TEST_BINARY_DIR}/jh_project_feature_${NAME}")
    file(REMOVE_RECURSE "${_jh_fixture}")
    file(MAKE_DIRECTORY "${_jh_fixture}")
    file(WRITE "${_jh_fixture}/hal_project_config.h" "${TEXT}")
    set(FIXTURE "${_jh_fixture}" PARENT_SCOPE)
endfunction()

if(DEFINED JH_PROJECT_FEATURE_FAILURE_CASE)
    set(_jh_case "${JH_PROJECT_FEATURE_FAILURE_CASE}")
    if(_jh_case STREQUAL "header-zero")
        write_fixture(failure "#define HAL_ENABLE_WIFI 0\n")
        read_fixture("${FIXTURE}")
    elseif(_jh_case STREQUAL "header-other")
        write_fixture(failure
            "#define HAL_ENABLE_WIFI /* value follows the comment */ ON\n")
        read_fixture("${FIXTURE}")
    elseif(_jh_case STREQUAL "header-compact-zero")
        write_fixture(failure "#define HAL_ENABLE_WIFI/**/0\n")
        read_fixture("${FIXTURE}")
    elseif(_jh_case STREQUAL "header-multiline-zero")
        write_fixture(failure
            "#define HAL_ENABLE_WIFI /* value follows\nthe multiline comment */ 0\n")
        read_fixture("${FIXTURE}")
    elseif(_jh_case STREQUAL "header-string-zero")
        write_fixture(failure
            "#define HAL_COMMENT_MARKER \"/*\"\n// another marker /*\n#define HAL_ENABLE_WIFI 0\n")
        read_fixture("${FIXTURE}")
    elseif(_jh_case STREQUAL "header-spliced-zero")
        write_fixture(failure "#define \\\n HAL_ENABLE_WIFI 0\n")
        read_fixture("${FIXTURE}")
    elseif(_jh_case STREQUAL "header-spliced-comment-zero")
        write_fixture(failure "#define HAL_ENABLE_WIFI /\\\n* comment */ 0\n")
        read_fixture("${FIXTURE}")
    elseif(_jh_case STREQUAL "header-function-like")
        write_fixture(failure "#define HAL_ENABLE_WIFI(value) value\n")
        read_fixture("${FIXTURE}")
    elseif(_jh_case STREQUAL "variant-zero")
        write_fixture(failure
            "#define JH_PROJECT_VARIANTS(X) X(OFF, \"Off\", HAL_ENABLE_WIFI=0)\n")
        read_fixture("${FIXTURE}" VARIANT OFF)
    elseif(_jh_case STREQUAL "define-zero")
        jh_validate_feature_defines(HAL_ENABLE_WIFI=0)
    elseif(_jh_case STREQUAL "define-other")
        jh_validate_feature_defines(HAL_ENABLE_WIFI=ON)
    elseif(_jh_case STREQUAL "define-genex")
        jh_validate_feature_defines(
            "$<1:HAL_$<1:ENABLE>_MQTT=0>")
    elseif(_jh_case STREQUAL "cmake-direct-list")
        set(HAL_ENABLE_UNITY "1;APP_INJECTED=1" CACHE STRING "" FORCE)
        jh_collect_cmake_feature_defines(_jh_unused)
    elseif(_jh_case STREQUAL "library-define-zero")
        jh_read_library_config(
            ROOT "${JH_ROOT}" TARGET rp2040
            OUTPUT_DIR "${JH_TEST_BINARY_DIR}/jh_project_feature_library"
            DEFINES HAL_ENABLE_WIFI=0)
    elseif(_jh_case STREQUAL "library-variable-zero")
        set(HAL_ENABLE_WIFI 0)
        jh_read_library_config(
            ROOT "${JH_ROOT}" TARGET rp2040
            OUTPUT_DIR "${JH_TEST_BINARY_DIR}/jh_project_feature_library")
    elseif(_jh_case STREQUAL "stm-helper-other")
        include("${JH_ROOT}/link_libraries/stm32_lib/freertos_stm32g474.cmake")
        jh_cmake_defines_contain(_jh_unused HAL_ENABLE_WIFI
            HAL_ENABLE_WIFI=ON)
    elseif(_jh_case STREQUAL "uncertain-value")
        write_fixture(failure
            "#ifdef SDK_ONLY\n#define HAL_RP_CORE1_STACK_SIZE 8192\n#endif\n")
        read_fixture("${FIXTURE}")
        jh_project_value(_jh_unused HAL_RP_CORE1_STACK_SIZE)
    elseif(_jh_case STREQUAL "sdk-value")
        write_fixture(failure
            "#define FLASH_BYTES PICO_FLASH_SIZE_BYTES\n#define HAL_RP_FLASH_EEPROM_SIZE (FLASH_BYTES / 256)\n")
        read_fixture("${FIXTURE}")
        jh_project_value(_jh_unused HAL_RP_FLASH_EEPROM_SIZE)
    elseif(_jh_case STREQUAL "uncertain-feature")
        write_fixture(failure
            "#ifdef SDK_ONLY\n#define HAL_ENABLE_WIFI\n#endif\n")
        read_fixture("${FIXTURE}")
    elseif(_jh_case STREQUAL "unknown-variant")
        write_fixture(failure
            "#define JH_PROJECT_VARIANTS(X) X(PROBE, \"Probe\", PROBE_ONLY=1)\n")
        read_fixture("${FIXTURE}" VARIANT MISSING)
    elseif(_jh_case STREQUAL "board-requested-mismatch")
        include("${JH_ROOT}/cmake/jh_board_profiles.cmake")
        set(_jh_failure_fixture "${JH_TEST_BINARY_DIR}/jh_project_feature_parity")
        jh_generate_board_config(
            ROOT "${JH_ROOT}"
            TARGET mock
            BOARD host-mock
            OUTPUT_DIR "${_jh_failure_fixture}/generated"
            DEFINES HAL_ENABLE_MQTT
            REQUESTED_FEATURES HAL_ENABLE_CRC
            RESOLVED_FEATURES HAL_ENABLE_CRC)
    elseif(_jh_case STREQUAL "stm-resolved-mismatch")
        include("${JH_ROOT}/link_libraries/stm32_lib/jh_stm32g474_firmware.cmake")
        jh_add_stm32g474_firmware(parity_probe
            SOURCES "${CMAKE_CURRENT_LIST_FILE}"
            DEFINES HAL_ENABLE_MQTT
            RESOLVED_FEATURES HAL_ENABLE_CRC)
    elseif(_jh_case STREQUAL "entry-core1-mismatch")
        include("${JH_ROOT}/cmake/jh_entry_adapter.cmake")
        jh_validate_entry_adapter_features(TRUE HAL_ENABLE_WIFI)
    else()
        message(FATAL_ERROR "Unknown feature failure case: ${_jh_case}")
    endif()
    message(FATAL_ERROR "Feature failure case unexpectedly passed: ${_jh_case}")
endif()

function(expect_equal ACTUAL EXPECTED WHAT)
    if(NOT "${ACTUAL}" STREQUAL "${EXPECTED}")
        message(FATAL_ERROR "${WHAT}: expected '${EXPECTED}', got '${ACTUAL}'")
    endif()
endfunction()

# Features and values come from the header as the compiler sees it.
write_fixture(main [=[
#pragma once
#define HAL_DISABLE_ASSERTS
#define HAL_ENABLE_WIFI
 # define HAL_ENABLE_MQTT 1 // explicit enable
#ifndef HAL_ENABLE_TLS
#define \
 HAL_ENABLE_TLS 1
#endif
#define HAL_ENABLE_WIFI
/* #define HAL_ENABLE_OTA */
// #define HAL_ENABLE_WIREGUARD
#if defined(HAL_TARGET_STM32G474)
#define HAL_ENABLE_STM32G474_FDCAN
#endif
#define HAL_ENDPOINT "https://example.invalid/*"
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#define HAL_RP_CORE1_STACK_SIZE (4 * 1024)
#define JH_PROJECT_VARIANTS(X) \
    X(PROBE, "No network", PROBE_ONLY=1) \
    X(AUDIO, "Audio output", HAL_ENABLE_DMA_PWM_AUDIO)
#if PROBE_ONLY
#undef HAL_ENABLE_MQTT
#endif
]=])
read_fixture("${FIXTURE}")
expect_equal("${JH_PROJECT_FEATURES}"
    "HAL_DISABLE_ASSERTS;HAL_ENABLE_WIFI;HAL_ENABLE_MQTT;HAL_ENABLE_TLS"
    "Project features")
expect_equal("${JH_PROJECT_VARIANTS}" "PROBE;AUDIO" "Declared variants")
expect_equal("${JH_PROJECT_DEFINITIONS}" "" "Base build definitions")
jh_project_value(_jh_baud HAL_DEBUG_DEFAULT_BAUD)
expect_equal("${_jh_baud}" "115200" "Integer value")
jh_project_value(_jh_endpoint HAL_ENDPOINT)
expect_equal("${_jh_endpoint}" "\"https://example.invalid/*\"" "Text value")
jh_build_define_value(_jh_stack HAL_RP_CORE1_STACK_SIZE
    HAL_RP_CORE1_STACK_SIZE=2048u)
expect_equal("${_jh_stack}" "4096" "Project value wins over the board")
jh_build_define_value(_jh_board_value HAL_RP_CORE0_STACK_SIZE
    HAL_RP_CORE0_STACK_SIZE=2048u)
expect_equal("${_jh_board_value}" "2048" "Board value without its suffix")
jh_build_defined(_jh_board_defined HAL_NETWORK_BACKEND_CYW43
    HAL_NETWORK_BACKEND_CYW43)
expect_equal("${_jh_board_defined}" "TRUE" "Board definition")
jh_build_defined(_jh_undefined HAL_NETWORK_BACKEND_CYW43)
expect_equal("${_jh_undefined}" "FALSE" "Undefined macro")

read_fixture("${FIXTURE}" VARIANT PROBE)
expect_equal("${JH_PROJECT_VARIANT}" "PROBE" "Selected variant")
expect_equal("${JH_PROJECT_DEFINITIONS}" "PROBE_ONLY=1" "Variant definitions")
expect_equal("${JH_PROJECT_FEATURES}"
    "HAL_DISABLE_ASSERTS;HAL_ENABLE_WIFI;HAL_ENABLE_TLS"
    "A variant switch evaluated in the header")
read_fixture("${FIXTURE}" VARIANT AUDIO)
expect_equal("${JH_PROJECT_FEATURES}"
    "HAL_DISABLE_ASSERTS;HAL_ENABLE_WIFI;HAL_ENABLE_MQTT;HAL_ENABLE_TLS;HAL_ENABLE_DMA_PWM_AUDIO"
    "A variant adds a feature to the base configuration")

jh_read_project_config(
    ROOT "${JH_ROOT}"
    CONFIG_DIR "${FIXTURE}"
    TARGET stm32g474
    OUTPUT_DIR "${FIXTURE}/generated-stm32")
list(FIND JH_PROJECT_FEATURES HAL_ENABLE_STM32G474_FDCAN _jh_fdcan_index)
if(_jh_fdcan_index EQUAL -1)
    message(FATAL_ERROR "The target selector did not reach the header")
endif()

write_fixture(continued_comment
    "// hidden by a continued line comment \\\n#define HAL_ENABLE_WIFI 0\n")
read_fixture("${FIXTURE}")
expect_equal("${JH_PROJECT_FEATURES}" "" "Continued line comment")

read_fixture("${JH_TEST_BINARY_DIR}/jh_project_feature_missing")
expect_equal("${JH_PROJECT_FEATURES}" "" "Missing project configuration")

# Arguments reach the reader unchanged: a macro would read the backslash of a
# Windows path as an escape sequence and stop the configure.
set(_jh_backslash_dir "${JH_TEST_BINARY_DIR}/jh_project_feature_back\\slash")
# Python keeps the backslash on every host; CMake file() makes it a separator.
jh_find_python(_jh_python)
execute_process(
    COMMAND "${_jh_python}" -c
        "import pathlib, sys\ndirectory = pathlib.Path(sys.argv[1])\ndirectory.mkdir(parents=True, exist_ok=True)\n(directory / 'hal_project_config.h').write_text('#define HAL_ENABLE_WIFI\\n')"
        "${_jh_backslash_dir}"
    RESULT_VARIABLE _jh_backslash_result)
expect_equal("${_jh_backslash_result}" "0" "Backslash fixture")
jh_read_project_config(
    ROOT "${JH_ROOT}"
    CONFIG_DIR "${_jh_backslash_dir}"
    TARGET rp2040
    OUTPUT_DIR "${JH_TEST_BINARY_DIR}/jh_project_feature_backslash")
expect_equal("${JH_PROJECT_FEATURES}" "HAL_ENABLE_WIFI"
    "Configuration directory with a backslash")

jh_validate_feature_defines(
    HAL_DISABLE_ASSERTS
    HAL_ENABLE_WIFI
    HAL_ENABLE_MQTT=1
    -DHAL_ENABLE_TLS=1
    DACLESS_EXAMPLE_USE_DMA=0)
jh_normalize_feature_defines(_jh_normalized
    -DHAL_DISABLE_ASSERTS=1
    HAL_ENABLE_WIFI
    -DHAL_ENABLE_TLS=1
    DACLESS_EXAMPLE_USE_DMA=0)
expect_equal("${_jh_normalized}"
    "HAL_DISABLE_ASSERTS=1;HAL_ENABLE_WIFI;HAL_ENABLE_TLS=1;DACLESS_EXAMPLE_USE_DMA=0"
    "Feature normalization")

set(HAL_ENABLE_MQTT 1 CACHE STRING "" FORCE)
set(HAL_DISABLE_ASSERTS 1 CACHE STRING "" FORCE)
jh_collect_cmake_feature_defines(_jh_cmake_features)
expect_equal("${_jh_cmake_features}" "HAL_DISABLE_ASSERTS=1;HAL_ENABLE_MQTT=1"
    "Direct CMake feature normalization")
unset(HAL_ENABLE_MQTT CACHE)
unset(HAL_ENABLE_MQTT)
unset(HAL_DISABLE_ASSERTS CACHE)
unset(HAL_DISABLE_ASSERTS)

jh_resolve_feature_defines(
    _jh_requested_features _jh_resolved_features HAL_ENABLE_MQTT)
expect_equal("${_jh_requested_features}" "HAL_ENABLE_MQTT"
    "Registry requested features")
expect_equal("${_jh_resolved_features}"
    "HAL_ENABLE_MQTT;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_TCP;HAL_ENABLE_WIFI"
    "Registry resolved features")

# A static library build: -D inputs plus the optional project header.
include("${JH_ROOT}/cmake/jh_rp_hal_sources.cmake")
jh_read_library_config(
    ROOT "${JH_ROOT}" TARGET rp2040
    OUTPUT_DIR "${JH_TEST_BINARY_DIR}/jh_project_feature_library"
    DEFINES -DHAL_ENABLE_MQTT=1 -DAPP_BUFFER=64)
jh_hal_define_enabled(_jh_rp_mqtt_enabled HAL_ENABLE_MQTT)
expect_equal("${_jh_rp_mqtt_enabled}" "TRUE" "RP selection of -DHAL_ENABLE_MQTT=1")
jh_hal_define_enabled(_jh_rp_tcp_enabled HAL_ENABLE_TCP)
expect_equal("${_jh_rp_tcp_enabled}" "TRUE" "RP selection of MQTT-implied TCP")
jh_project_value(_jh_buffer APP_BUFFER)
expect_equal("${_jh_buffer}" "64" "A library -D value")

# A configuration without HAL features: the dispatcher's later
# set(JH_RESOLVED_FEATURES ...) of an empty list unsets the variable, and
# source selection must still answer.
jh_read_library_config(
    ROOT "${JH_ROOT}" TARGET rp2040
    OUTPUT_DIR "${JH_TEST_BINARY_DIR}/jh_project_feature_library")
set(JH_RESOLVED_FEATURES ${JH_RESOLVED_FEATURES})
jh_hal_define_enabled(_jh_rp_no_eeprom HAL_ENABLE_EEPROM)
expect_equal("${_jh_rp_no_eeprom}" "FALSE" "RP selection without features")

jh_split_project_definitions(_jh_plain _jh_feature_definitions
    APP_BUFFER=64 HAL_ENABLE_SX127X -DHAL_DISABLE_ASSERTS=1 EXAMPLE_SWITCH)
expect_equal("${_jh_plain}" "APP_BUFFER=64;EXAMPLE_SWITCH" "Plain definitions")
expect_equal("${_jh_feature_definitions}" "HAL_ENABLE_SX127X;-DHAL_DISABLE_ASSERTS=1"
    "Feature definitions")

write_fixture(compact "#define HAL_ENABLE_MQTT/**/1\n")
set(HAL_PROJECT_CONFIG_DIR "${FIXTURE}")
jh_read_library_config(
    ROOT "${JH_ROOT}" TARGET rp2040
    OUTPUT_DIR "${JH_TEST_BINARY_DIR}/jh_project_feature_library")
jh_hal_define_enabled(_jh_rp_compact_mqtt_enabled HAL_ENABLE_MQTT)
expect_equal("${_jh_rp_compact_mqtt_enabled}" "TRUE"
    "RP selection of a compact header feature")
set(HAL_PROJECT_CONFIG_DIR "${_jh_backslash_dir}")
jh_read_library_config(
    ROOT "${JH_ROOT}" TARGET rp2040
    OUTPUT_DIR "${JH_TEST_BINARY_DIR}/jh_project_feature_library")
jh_hal_define_enabled(_jh_rp_backslash_wifi HAL_ENABLE_WIFI)
expect_equal("${_jh_rp_backslash_wifi}" "TRUE"
    "Library configuration directory with a backslash")
unset(HAL_PROJECT_CONFIG_DIR)

include("${JH_ROOT}/link_libraries/stm32_lib/freertos_stm32g474.cmake")
jh_cmake_defines_contain(_jh_stm_tls_enabled HAL_ENABLE_TLS
    -DHAL_ENABLE_TLS=1)
expect_equal("${_jh_stm_tls_enabled}" "TRUE" "STM32 -DHAL_ENABLE_TLS=1")

function(expect_failures DIAGNOSTIC)
    foreach(_jh_failure_case IN LISTS ARGN)
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}"
                "-DJH_ROOT=${JH_ROOT}"
                "-DJH_TEST_BINARY_DIR=${JH_TEST_BINARY_DIR}"
                "-DJH_PROJECT_FEATURE_FAILURE_CASE=${_jh_failure_case}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            RESULT_VARIABLE _jh_failure_result
            OUTPUT_VARIABLE _jh_failure_stdout
            ERROR_VARIABLE _jh_failure_stderr
        )
        if(_jh_failure_result EQUAL 0)
            message(FATAL_ERROR "Feature failure case passed: ${_jh_failure_case}")
        endif()
        set(_jh_failure_output "${_jh_failure_stdout}\n${_jh_failure_stderr}")
        string(FIND "${_jh_failure_output}" "[${DIAGNOSTIC}]" _jh_diagnostic_index)
        if(_jh_diagnostic_index EQUAL -1)
            message(FATAL_ERROR
                "Feature failure case lacks [${DIAGNOSTIC}]: "
                "${_jh_failure_case}\n${_jh_failure_output}")
        endif()
    endforeach()
endfunction()

expect_failures(JH-CFG-VALUE
    header-zero
    header-other
    header-compact-zero
    header-multiline-zero
    header-string-zero
    header-spliced-zero
    header-spliced-comment-zero
    header-function-like
    variant-zero
    define-zero
    define-other
    define-genex
    cmake-direct-list
    library-define-zero
    library-variable-zero
    stm-helper-other)
expect_failures(JH-CFG-SCOPE uncertain-value uncertain-feature sdk-value)
expect_failures(JH-CFG-VARIANT unknown-variant)
expect_failures(JH-CFG-PARITY
    board-requested-mismatch
    stm-resolved-mismatch
    entry-core1-mismatch)

# A project build refuses the inputs that used to carry parts of its
# configuration instead of ignoring them.
set(_jh_dispatcher_probe "${JH_TEST_BINARY_DIR}/jh_project_feature_dispatcher")
foreach(_jh_removed_input IN ITEMS
        "-DJH_EXTRA_DEFINES=EXAMPLE_SWITCH=1"
        "-DJH_PROJECT_SOURCES=app.c"
        "-DEXTRA_HAL_DEFINES=HAL_ENABLE_CRC"
        "-DHAL_ENABLE_CRC=1")
    file(REMOVE_RECURSE "${_jh_dispatcher_probe}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -S "${JH_ROOT}/cmake/jh_firmware_project"
            -B "${_jh_dispatcher_probe}"
            "-DJH_PROJECT_DIR=${JH_TEST_BINARY_DIR}"
            "${_jh_removed_input}"
        RESULT_VARIABLE _jh_dispatcher_result
        OUTPUT_VARIABLE _jh_dispatcher_stdout
        ERROR_VARIABLE _jh_dispatcher_stderr)
    string(FIND "${_jh_dispatcher_stderr}" "[JH-CFG-MANIFEST]" _jh_dispatcher_index)
    if(_jh_dispatcher_result EQUAL 0 OR _jh_dispatcher_index EQUAL -1)
        message(FATAL_ERROR
            "The firmware dispatcher accepted ${_jh_removed_input}:\n"
            "${_jh_dispatcher_stderr}")
    endif()
endforeach()
file(REMOVE_RECURSE "${_jh_dispatcher_probe}")
