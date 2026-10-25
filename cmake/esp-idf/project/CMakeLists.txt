cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED ENV{JH_ESP_IDF_PROJECT_CONFIG})
    message(FATAL_ERROR "JH_ESP_IDF_PROJECT_CONFIG is required")
endif()
file(TO_CMAKE_PATH "$ENV{JH_ESP_IDF_PROJECT_CONFIG}"
    _jh_esp_idf_project_config)
if(NOT EXISTS "${_jh_esp_idf_project_config}")
    message(FATAL_ERROR
        "Generated ESP-IDF project config is missing: "
        "${_jh_esp_idf_project_config}")
endif()
include("${_jh_esp_idf_project_config}")

if(NOT DEFINED IDF_TARGET)
    message(FATAL_ERROR "The JaszczurHAL ESP-IDF build requires IDF_TARGET")
endif()
if(NOT IDF_TARGET STREQUAL JH_ESP_IDF_TARGET)
    message(FATAL_ERROR
        "IDF_TARGET=${IDF_TARGET} conflicts with the generated target "
        "${JH_ESP_IDF_TARGET}")
endif()

get_filename_component(JH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${JH_ROOT}/cmake/esp-idf/components")
set(SDKCONFIG_DEFAULTS ${JH_ESP_IDF_SDKCONFIG_DEFAULTS})
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(${JH_ESP_IDF_PROJECT_NAME})

# Share the status package and HAL installation with the other build families.
include("${JH_ROOT}/cmake/jh_jhbase.cmake")
jh_add_jhbase()
file(TO_CMAKE_PATH "$ENV{JH_ESP_IDF_GENERATED_DIR}" JH_BOARD_GENERATED_DIR)
set(JH_DEVICE_TREE_BUILD "${JH_ESP_IDF_DEVICE_TREE}")
if(JH_DEVICE_TREE_BUILD)
    include("${JH_BOARD_GENERATED_DIR}/jh_hardware_config.cmake")
endif()
include("${JH_ROOT}/cmake/jh_library_package.cmake")
jh_add_library_package(__idf_jaszczurhal "${JH_ROOT}/src"
    ARCHIVE_NAME libJaszczurHAL.a)
