if(NOT "$ENV{JH_ESP_IDF_PHASE0}" STREQUAL "1")
    message(FATAL_ERROR
        "The native JaszczurHAL ESP-IDF backend is not implemented; "
        "this component recipe currently supports only the controlled Phase 0 spike")
endif()
file(TO_CMAKE_PATH "$ENV{JH_PHASE0_GENERATED_DIR}" _jh_generated_dir)
if(NOT _jh_generated_dir OR NOT IS_DIRECTORY "${_jh_generated_dir}")
    message(FATAL_ERROR
        "JH_PHASE0_GENERATED_DIR must name the generated Phase 0 contract directory")
endif()

get_filename_component(JH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(_jh_shared_source "${JH_ROOT}/src/hal/core/jh_handle_pool.cpp")
set(_jh_link_definition
    "${_jh_generated_dir}/jh_link_contract_definition.c")
foreach(_required_file
    "${_jh_shared_source}"
    "${_jh_generated_dir}/jh_esp_idf_phase0_config.h"
    "${_jh_generated_dir}/jh_link_contract.h"
    "${_jh_link_definition}")
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "Required Phase 0 input is missing: ${_required_file}")
    endif()
endforeach()

idf_component_register(
    SRCS
        "${_jh_shared_source}"
        "${_jh_link_definition}"
    INCLUDE_DIRS
        "${JH_ROOT}/src"
        "${_jh_generated_dir}")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
