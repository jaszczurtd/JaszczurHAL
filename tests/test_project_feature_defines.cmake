if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()
if(NOT DEFINED JH_TEST_BINARY_DIR)
    message(FATAL_ERROR "JH_TEST_BINARY_DIR is required")
endif()

include("${JH_ROOT}/cmake/jh_project_features.cmake")

if(DEFINED JH_PROJECT_FEATURE_FAILURE_CASE)
    set(_jh_failure_fixture
        "${JH_TEST_BINARY_DIR}/jh_project_feature_failure_${JH_PROJECT_FEATURE_FAILURE_CASE}")
    file(MAKE_DIRECTORY "${_jh_failure_fixture}")
    if(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "header-zero")
        file(WRITE "${_jh_failure_fixture}/hal_project_config.h"
            "#define HAL_ENABLE_WIFI 0\n")
        jh_collect_project_feature_defines(_jh_unused "${_jh_failure_fixture}")
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "header-other")
        file(WRITE "${_jh_failure_fixture}/hal_project_config.h"
            "#define HAL_ENABLE_WIFI /* value follows the comment */ ON\n")
        jh_collect_project_feature_defines(_jh_unused "${_jh_failure_fixture}")
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "header-compact-zero")
        file(WRITE "${_jh_failure_fixture}/hal_project_config.h"
            "#define HAL_ENABLE_WIFI/**/0\n")
        jh_collect_project_feature_defines(_jh_unused "${_jh_failure_fixture}")
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "header-multiline-zero")
        file(WRITE "${_jh_failure_fixture}/hal_project_config.h"
            "#define HAL_ENABLE_WIFI /* value follows\n"
            "the multiline comment */ 0\n")
        jh_collect_project_feature_defines(_jh_unused "${_jh_failure_fixture}")
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "header-string-zero")
        file(WRITE "${_jh_failure_fixture}/hal_project_config.h"
            "#define HAL_COMMENT_MARKER \"/*\"\n"
            "// another marker /*\n"
            "#define HAL_ENABLE_WIFI 0\n")
        jh_collect_project_feature_defines(_jh_unused "${_jh_failure_fixture}")
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "header-spliced-zero")
        file(WRITE "${_jh_failure_fixture}/hal_project_config.h"
            "#define \\\n HAL_ENABLE_WIFI 0\n")
        jh_collect_project_feature_defines(_jh_unused "${_jh_failure_fixture}")
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "header-spliced-comment-zero")
        file(WRITE "${_jh_failure_fixture}/hal_project_config.h"
            "#define HAL_ENABLE_WIFI /\\\n* comment */ 0\n")
        jh_collect_project_feature_defines(_jh_unused "${_jh_failure_fixture}")
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "header-function-like")
        file(WRITE "${_jh_failure_fixture}/hal_project_config.h"
            "#define HAL_ENABLE_WIFI(value) value\n")
        jh_collect_project_feature_defines(_jh_unused "${_jh_failure_fixture}")
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "define-zero")
        jh_validate_feature_defines(HAL_ENABLE_WIFI=0)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "define-other")
        jh_validate_feature_defines(HAL_ENABLE_WIFI=ON)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "define-genex")
        jh_validate_feature_defines(
            "$<1:HAL_$<1:ENABLE>_MQTT=0>")
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "cmake-direct-list")
        set(HAL_ENABLE_UNITY "1;APP_INJECTED=1" CACHE STRING "" FORCE)
        jh_collect_cmake_feature_defines(_jh_unused)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "rp-extra-zero")
        set(EXTRA_HAL_DEFINES HAL_ENABLE_WIFI=0)
        include("${JH_ROOT}/cmake/jh_rp_hal_sources.cmake")
        jh_hal_define_enabled(_jh_unused HAL_ENABLE_WIFI)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "rp-variable-zero")
        set(HAL_ENABLE_WIFI 0)
        include("${JH_ROOT}/cmake/jh_rp_hal_sources.cmake")
        jh_hal_define_enabled(_jh_unused HAL_ENABLE_WIFI)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "rp-variable-list")
        set(HAL_ENABLE_WIFI "1;APP_INJECTED=1")
        set(EXTRA_HAL_DEFINES HAL_ENABLE_WIFI=1)
        include("${JH_ROOT}/cmake/jh_rp_hal_sources.cmake")
        jh_hal_define_enabled(_jh_unused HAL_ENABLE_WIFI)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "rp-unity-other")
        set(HAL_ENABLE_UNITY ON)
        include("${JH_ROOT}/cmake/jh_rp_hal_sources.cmake")
        jh_hal_define_enabled(_jh_unused HAL_ENABLE_UNITY)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "stm-helper-other")
        include("${JH_ROOT}/stm32_lib/freertos_stm32g474.cmake")
        jh_cmake_defines_contain(_jh_unused HAL_ENABLE_WIFI
            HAL_ENABLE_WIFI=ON)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "board-requested-mismatch")
        include("${JH_ROOT}/cmake/jh_board_profiles.cmake")
        jh_generate_board_config(
            ROOT "${JH_ROOT}"
            TARGET mock
            BOARD host-mock
            OUTPUT_DIR "${_jh_failure_fixture}/generated"
            DEFINES HAL_ENABLE_MQTT
            REQUESTED_FEATURES HAL_ENABLE_CRC
            RESOLVED_FEATURES HAL_ENABLE_CRC)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "stm-resolved-mismatch")
        include("${JH_ROOT}/stm32_lib/jh_stm32g474_firmware.cmake")
        jh_add_stm32g474_firmware(parity_probe
            SOURCES "${CMAKE_CURRENT_LIST_FILE}"
            DEFINES HAL_ENABLE_MQTT
            RESOLVED_FEATURES HAL_ENABLE_CRC)
    elseif(JH_PROJECT_FEATURE_FAILURE_CASE STREQUAL "entry-core1-mismatch")
        include("${JH_ROOT}/cmake/jh_entry_adapter.cmake")
        jh_validate_entry_adapter_features(TRUE HAL_ENABLE_WIFI)
    else()
        message(FATAL_ERROR
            "Unknown feature failure case: ${JH_PROJECT_FEATURE_FAILURE_CASE}")
    endif()
    message(FATAL_ERROR
        "Feature failure case unexpectedly passed: ${JH_PROJECT_FEATURE_FAILURE_CASE}")
endif()

set(_jh_fixture "${JH_TEST_BINARY_DIR}/jh_project_feature_fixture")
file(MAKE_DIRECTORY "${_jh_fixture}")
file(WRITE "${_jh_fixture}/hal_project_config.h" [=[
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
#define HAL_ENDPOINT "https://example.invalid/*"
#define HAL_DEBUG_DEFAULT_BAUD 115200u
]=])

jh_collect_project_feature_defines(_jh_actual "${_jh_fixture}")
set(_jh_expected
    HAL_DISABLE_ASSERTS HAL_ENABLE_WIFI HAL_ENABLE_MQTT HAL_ENABLE_TLS)
if(NOT "${_jh_actual}" STREQUAL "${_jh_expected}")
    message(FATAL_ERROR
        "Project feature detection mismatch: expected '${_jh_expected}', "
        "got '${_jh_actual}'")
endif()

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
set(_jh_normalized_expected
    HAL_DISABLE_ASSERTS=1 HAL_ENABLE_WIFI HAL_ENABLE_TLS=1
    DACLESS_EXAMPLE_USE_DMA=0)
if(NOT "${_jh_normalized}" STREQUAL "${_jh_normalized_expected}")
    message(FATAL_ERROR
        "Feature normalization mismatch: '${_jh_normalized}'")
endif()

set(HAL_ENABLE_MQTT 1 CACHE STRING "" FORCE)
set(HAL_DISABLE_ASSERTS 1 CACHE STRING "" FORCE)
jh_collect_cmake_feature_defines(_jh_cmake_features)
if(NOT "${_jh_cmake_features}" STREQUAL
   "HAL_DISABLE_ASSERTS=1;HAL_ENABLE_MQTT=1")
    message(FATAL_ERROR
        "Direct CMake feature normalization mismatch: '${_jh_cmake_features}'")
endif()
unset(HAL_ENABLE_MQTT CACHE)
unset(HAL_ENABLE_MQTT)
unset(HAL_DISABLE_ASSERTS CACHE)
unset(HAL_DISABLE_ASSERTS)

jh_resolve_feature_defines(
    _jh_requested_features _jh_resolved_features HAL_ENABLE_MQTT)
if(NOT "${_jh_requested_features}" STREQUAL "HAL_ENABLE_MQTT" OR
   NOT "${_jh_resolved_features}" STREQUAL
   "HAL_ENABLE_MQTT;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_TCP;HAL_ENABLE_WIFI")
    message(FATAL_ERROR
        "Registry feature resolution mismatch: requested "
        "'${_jh_requested_features}', resolved '${_jh_resolved_features}'")
endif()

set(EXTRA_HAL_DEFINES -DHAL_ENABLE_MQTT=1)
include("${JH_ROOT}/cmake/jh_rp_hal_sources.cmake")
jh_hal_define_enabled(_jh_rp_mqtt_enabled HAL_ENABLE_MQTT)
if(NOT _jh_rp_mqtt_enabled)
    message(FATAL_ERROR
        "RP source selection ignored -DHAL_ENABLE_MQTT=1")
endif()
jh_hal_define_enabled(_jh_rp_tcp_enabled HAL_ENABLE_TCP)
if(NOT _jh_rp_tcp_enabled)
    message(FATAL_ERROR
        "RP source selection ignored MQTT-implied HAL_ENABLE_TCP")
endif()

set(_jh_compact_fixture "${JH_TEST_BINARY_DIR}/jh_project_feature_compact")
file(MAKE_DIRECTORY "${_jh_compact_fixture}")
file(WRITE "${_jh_compact_fixture}/hal_project_config.h"
    "#define HAL_ENABLE_MQTT/**/1\n")
unset(EXTRA_HAL_DEFINES)
set(HAL_PROJECT_CONFIG_DIR "${_jh_compact_fixture}")
jh_hal_define_enabled(_jh_rp_compact_mqtt_enabled HAL_ENABLE_MQTT)
if(NOT _jh_rp_compact_mqtt_enabled)
    message(FATAL_ERROR
        "RP source selection ignored compact project feature definition")
endif()
unset(HAL_PROJECT_CONFIG_DIR)

set(_jh_continued_comment_fixture
    "${JH_TEST_BINARY_DIR}/jh_project_feature_continued_comment")
file(MAKE_DIRECTORY "${_jh_continued_comment_fixture}")
file(WRITE "${_jh_continued_comment_fixture}/hal_project_config.h"
    "// hidden by a continued line comment \\\n"
    "#define HAL_ENABLE_WIFI 0\n")
jh_collect_project_feature_defines(
    _jh_continued_comment_features "${_jh_continued_comment_fixture}")
if(_jh_continued_comment_features)
    message(FATAL_ERROR
        "Continued line comment exposed project feature definitions")
endif()

include("${JH_ROOT}/stm32_lib/freertos_stm32g474.cmake")
jh_cmake_defines_contain(_jh_stm_tls_enabled HAL_ENABLE_TLS
    -DHAL_ENABLE_TLS=1)
if(NOT _jh_stm_tls_enabled)
    message(FATAL_ERROR
        "STM32 source selection ignored -DHAL_ENABLE_TLS=1")
endif()

set(_jh_test_script "${CMAKE_CURRENT_LIST_FILE}")
foreach(_jh_failure_case IN ITEMS
        header-zero
        header-other
        header-compact-zero
        header-multiline-zero
        header-string-zero
        header-spliced-zero
        header-spliced-comment-zero
        header-function-like
        define-zero
        define-other
        define-genex
        cmake-direct-list
        rp-extra-zero
        rp-variable-zero
        rp-variable-list
        rp-unity-other
        stm-helper-other)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}"
            "-DJH_ROOT=${JH_ROOT}"
            "-DJH_TEST_BINARY_DIR=${JH_TEST_BINARY_DIR}"
            "-DJH_PROJECT_FEATURE_FAILURE_CASE=${_jh_failure_case}"
            -P "${_jh_test_script}"
        RESULT_VARIABLE _jh_failure_result
        OUTPUT_VARIABLE _jh_failure_stdout
        ERROR_VARIABLE _jh_failure_stderr
    )
    if(_jh_failure_result EQUAL 0)
        message(FATAL_ERROR
            "Feature failure case passed: ${_jh_failure_case}")
    endif()
    set(_jh_failure_output
        "${_jh_failure_stdout}\n${_jh_failure_stderr}")
    if(NOT _jh_failure_output MATCHES "\\[JH-CFG-VALUE\\]")
        message(FATAL_ERROR
            "Feature failure case lacks [JH-CFG-VALUE]: "
            "${_jh_failure_case}\n${_jh_failure_output}")
    endif()
endforeach()

foreach(_jh_failure_case IN ITEMS
        board-requested-mismatch
        stm-resolved-mismatch
        entry-core1-mismatch)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}"
            "-DJH_ROOT=${JH_ROOT}"
            "-DJH_TEST_BINARY_DIR=${JH_TEST_BINARY_DIR}"
            "-DJH_PROJECT_FEATURE_FAILURE_CASE=${_jh_failure_case}"
            -P "${_jh_test_script}"
        RESULT_VARIABLE _jh_failure_result
        OUTPUT_VARIABLE _jh_failure_stdout
        ERROR_VARIABLE _jh_failure_stderr
    )
    if(_jh_failure_result EQUAL 0)
        message(FATAL_ERROR
            "Feature parity failure case passed: ${_jh_failure_case}")
    endif()
    set(_jh_failure_output
        "${_jh_failure_stdout}\n${_jh_failure_stderr}")
    if(NOT _jh_failure_output MATCHES "\\[JH-CFG-PARITY\\]")
        message(FATAL_ERROR
            "Feature parity failure case lacks [JH-CFG-PARITY]: "
            "${_jh_failure_case}\n${_jh_failure_output}")
    endif()
endforeach()

jh_collect_project_feature_defines(
    _jh_missing "${_jh_fixture}/missing-project")
if(_jh_missing)
    message(FATAL_ERROR
        "Missing project configuration produced features: '${_jh_missing}'")
endif()
