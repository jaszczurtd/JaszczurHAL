file(TO_CMAKE_PATH "$ENV{JH_ESP_IDF_GENERATED_DIR}" _jh_generated_dir)
if(NOT _jh_generated_dir OR NOT IS_DIRECTORY "${_jh_generated_dir}")
    message(FATAL_ERROR
        "JH_ESP_IDF_GENERATED_DIR must name the generated board contract directory")
endif()
file(TO_CMAKE_PATH "$ENV{JH_ESP_IDF_PROJECT_CONFIG}"
    _jh_project_config)
if(NOT _jh_project_config OR NOT EXISTS "${_jh_project_config}")
    message(FATAL_ERROR
        "JH_ESP_IDF_PROJECT_CONFIG must name the generated project config")
endif()
include("${_jh_project_config}")

get_filename_component(JH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(_jh_shared_source "${JH_ROOT}/src/hal/core/jh_handle_pool.cpp")
set(_jh_app_entry "${JH_ROOT}/src/hal_app_entry.cpp")
set(_jh_build_config
    "${JH_ROOT}/src/hal/impl/esp32/hal_esp32_build_config.cpp")
set(_jh_link_definition
    "${_jh_generated_dir}/jh_link_contract_definition.c")
foreach(_required_file
    "${_jh_shared_source}"
    "${_jh_app_entry}"
    "${_jh_build_config}"
    "${_jh_generated_dir}/jh_board_config.h"
    "${_jh_generated_dir}/jh_link_contract.h"
    "${_jh_link_definition}")
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "Required ESP-IDF input is missing: ${_required_file}")
    endif()
endforeach()

idf_component_register(
    SRCS
        "${_jh_shared_source}"
        "${_jh_app_entry}"
        "${_jh_build_config}"
        "${_jh_link_definition}"
    INCLUDE_DIRS
        "${JH_ROOT}/src"
        "${_jh_generated_dir}"
        ${JH_ESP_IDF_PROJECT_INCLUDE_DIRS}
    REQUIRES freertos esp_psram)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    JH_ESP_IDF_COMPONENT_BUILD=1
    ${JH_ESP_IDF_COMPILE_DEFINITIONS})

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
