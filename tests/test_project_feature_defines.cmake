if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()
if(NOT DEFINED JH_TEST_BINARY_DIR)
    message(FATAL_ERROR "JH_TEST_BINARY_DIR is required")
endif()

include("${JH_ROOT}/cmake/jh_project_features.cmake")

set(_jh_fixture "${JH_TEST_BINARY_DIR}/jh_project_feature_fixture")
file(MAKE_DIRECTORY "${_jh_fixture}")
file(WRITE "${_jh_fixture}/hal_project_config.h" [=[
#pragma once
#define HAL_ENABLE_WIFI
 # define HAL_ENABLE_MQTT 1
#ifndef HAL_ENABLE_TLS
#define HAL_ENABLE_TLS
#endif
#define HAL_ENABLE_WIFI
/* #define HAL_ENABLE_OTA */
// #define HAL_ENABLE_WIREGUARD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
]=])

jh_collect_project_feature_defines(_jh_actual "${_jh_fixture}")
set(_jh_expected HAL_ENABLE_WIFI HAL_ENABLE_MQTT HAL_ENABLE_TLS)
if(NOT "${_jh_actual}" STREQUAL "${_jh_expected}")
    message(FATAL_ERROR
        "Project feature detection mismatch: expected '${_jh_expected}', "
        "got '${_jh_actual}'")
endif()

jh_collect_project_feature_defines(
    _jh_missing "${_jh_fixture}/missing-project")
if(_jh_missing)
    message(FATAL_ERROR
        "Missing project configuration produced features: '${_jh_missing}'")
endif()
