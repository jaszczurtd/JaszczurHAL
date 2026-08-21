cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED IDF_TARGET)
    message(FATAL_ERROR "Phase 0 requires an explicit IDF_TARGET")
endif()
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR
        "Phase 0 supports only IDF_TARGET=esp32s3, got '${IDF_TARGET}'")
endif()

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
get_filename_component(JH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT "$ENV{JH_ESP_IDF_PHASE0}" STREQUAL "1" OR
   NOT IS_DIRECTORY "$ENV{JH_PHASE0_GENERATED_DIR}")
    message(FATAL_ERROR "The generated Phase 0 component contract is required")
endif()
set(EXTRA_COMPONENT_DIRS "${JH_ROOT}/cmake/esp-idf/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(jh_esp_idf_phase0)
