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

# HAL_RAM_CODE_SOURCES: main/CMakeLists.txt maps their code to IRAM; check
# the linked map so a fragment that matched nothing stops the build.
if(DEFINED JH_ESP_IDF_RAM_CODE_SOURCES)
    include("${JH_ROOT}/cmake/jh_ram_code.cmake")
    jh_ram_code_parse(_jh_ram_entries "${JH_ESP_IDF_RAM_CODE_SOURCES}")
    jh_ram_code_resolve(_jh_ram_sources BASE_DIR "${JH_ESP_IDF_PROJECT_DIR}"
        ENTRIES ${_jh_ram_entries} SOURCES ${JH_ESP_IDF_PROJECT_SOURCES})
    set(_jh_ram_objects "")
    foreach(_jh_source IN LISTS _jh_ram_sources)
        jh_ram_code_object_name(_jh_object "${_jh_source}")
        list(APPEND _jh_ram_objects "libmain.a(${_jh_object})")
    endforeach()
    if(JH_ESP_IDF_TARGET STREQUAL "esp32")
        set(_jh_flash_text 0x400D0000 0x40400000)
    else()
        set(_jh_flash_text 0x42000000 0x44000000)
    endif()
    jh_ram_code_add_map_check(TARGET "${CMAKE_PROJECT_NAME}.elf"
        MAP "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map"
        OBJECTS ${_jh_ram_objects}
        FLASH ${_jh_flash_text})
endif()
