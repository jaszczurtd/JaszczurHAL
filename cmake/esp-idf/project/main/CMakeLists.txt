if(NOT DEFINED ENV{JH_ESP_IDF_PROJECT_CONFIG})
    message(FATAL_ERROR "JH_ESP_IDF_PROJECT_CONFIG is required")
endif()
file(TO_CMAKE_PATH "$ENV{JH_ESP_IDF_PROJECT_CONFIG}"
    _jh_esp_idf_project_config)
include("${_jh_esp_idf_project_config}")
file(TO_CMAKE_PATH "$ENV{JH_ESP_IDF_GENERATED_DIR}"
    _jh_esp_idf_generated_dir)

set(_jh_project_sources
    ${JH_ESP_IDF_PROJECT_SOURCES}
    "${_jh_esp_idf_generated_dir}/jh_link_contract_reference.c")

idf_component_register(
    SRCS ${_jh_project_sources}
    INCLUDE_DIRS ${JH_ESP_IDF_PROJECT_INCLUDE_DIRS}
    REQUIRES jaszczurhal esp_system esp_psram spi_flash)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ${JH_ESP_IDF_COMPILE_DEFINITIONS})
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
