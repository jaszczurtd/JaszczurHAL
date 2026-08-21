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
if(NOT JH_ESP_IDF_COMPONENT_SOURCES)
    message(FATAL_ERROR "Generated ESP-IDF component source graph is empty")
endif()
if(NOT JH_ESP_IDF_COMPONENT_REQUIRES)
    message(FATAL_ERROR "Generated ESP-IDF public dependency graph is empty")
endif()
set(_jh_sources ${JH_ESP_IDF_COMPONENT_SOURCES})

set(_jh_link_definition
    "${_jh_generated_dir}/jh_link_contract_definition.c")
list(APPEND _jh_sources "${_jh_link_definition}")
foreach(_required_file
    ${_jh_sources}
    "${_jh_generated_dir}/jh_board_config.h"
    "${_jh_generated_dir}/jh_link_contract.h")
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "Required ESP-IDF input is missing: ${_required_file}")
    endif()
endforeach()

idf_component_register(
    SRCS ${_jh_sources}
    INCLUDE_DIRS
        "${JH_ROOT}/src"
        "${_jh_generated_dir}"
        ${JH_ESP_IDF_PROJECT_INCLUDE_DIRS}
    REQUIRES ${JH_ESP_IDF_COMPONENT_REQUIRES}
    PRIV_REQUIRES ${JH_ESP_IDF_COMPONENT_PRIV_REQUIRES})

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    JH_ESP_IDF_COMPONENT_BUILD=1
    ${JH_ESP_IDF_COMPILE_DEFINITIONS})

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
