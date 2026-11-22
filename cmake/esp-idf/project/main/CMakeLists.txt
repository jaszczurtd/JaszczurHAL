if(NOT DEFINED ENV{JH_ESP_IDF_PROJECT_CONFIG})
    message(FATAL_ERROR "JH_ESP_IDF_PROJECT_CONFIG is required")
endif()
file(TO_CMAKE_PATH "$ENV{JH_ESP_IDF_PROJECT_CONFIG}"
    _jh_esp_idf_project_config)
include("${_jh_esp_idf_project_config}")
file(TO_CMAKE_PATH "$ENV{JH_ESP_IDF_GENERATED_DIR}"
    _jh_esp_idf_generated_dir)
if(JH_ESP_IDF_DEVICE_TREE)
    list(APPEND JH_ESP_IDF_COMPILE_DEFINITIONS
        "JH_HARDWARE_HEADER=\"${_jh_esp_idf_generated_dir}/jh_hardware.h\"")
endif()

set(_jh_project_sources
    ${JH_ESP_IDF_PROJECT_SOURCES}
    "${_jh_esp_idf_generated_dir}/jh_link_contract_reference.c")

# HAL_RAM_CODE_SOURCES: the linker fragment places the code of these
# application objects in IRAM (scheme noflash_text); constants stay in flash.
set(_jh_ram_code_fragments "")
if(DEFINED JH_ESP_IDF_RAM_CODE_SOURCES)
    include("${CMAKE_CURRENT_LIST_DIR}/../../../jh_ram_code.cmake")
    jh_ram_code_parse(_jh_ram_entries "${JH_ESP_IDF_RAM_CODE_SOURCES}")
    jh_ram_code_resolve(_jh_ram_sources BASE_DIR "${JH_ESP_IDF_PROJECT_DIR}"
        ENTRIES ${_jh_ram_entries} SOURCES ${_jh_project_sources})
    jh_ram_code_esp_idf_entries(_jh_ram_objects
        RAM_SOURCES ${_jh_ram_sources} SOURCES ${_jh_project_sources})
    set(_jh_fragment "[mapping:jh_ram_code]\narchive: libmain.a\nentries:\n")
    foreach(_jh_object IN LISTS _jh_ram_objects)
        string(APPEND _jh_fragment "    ${_jh_object} (noflash_text)\n")
    endforeach()
    set(_jh_ram_code_fragments "${_jh_esp_idf_generated_dir}/jh_ram_code.lf")
    file(CONFIGURE OUTPUT "${_jh_ram_code_fragments}"
        CONTENT "${_jh_fragment}" @ONLY)
endif()

idf_component_register(
    SRCS ${_jh_project_sources}
    INCLUDE_DIRS ${JH_ESP_IDF_PROJECT_INCLUDE_DIRS}
    # The project's JH_EXTRA_INCLUDES, for the application only.
    PRIV_INCLUDE_DIRS ${JH_ESP_IDF_PROJECT_EXTRA_INCLUDE_DIRS}
    LDFRAGMENTS ${_jh_ram_code_fragments}
    REQUIRES jaszczurhal esp_system esp_psram spi_flash)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ${JH_ESP_IDF_COMPILE_DEFINITIONS})
# Precompiled archives of the project (JH_LINK_LIBRARIES); they follow the
# application in the link and the build relinks when one changes.
if(JH_ESP_IDF_PROJECT_LINK_LIBRARIES)
    target_link_libraries(${COMPONENT_LIB} PRIVATE
        ${JH_ESP_IDF_PROJECT_LINK_LIBRARIES})
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
