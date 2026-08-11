cmake_minimum_required(VERSION 3.16)

set(JH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(JH_TARGET "rp2040" CACHE STRING
    "Target ID: rp2040, rp2350-arm, or rp2350-riscv")
set(JH_BOARD "" CACHE STRING
    "Physical board profile; defaults to the target defaultBoard")
set(EXTRA_HAL_DEFINES "" CACHE STRING "Extra HAL compile definitions")
set(JH_RP_NATIVE_APP_DIR "" CACHE PATH
    "Optional portable app directory containing app.c or app.cpp")
set(JH_RP_NATIVE_APP_SOURCES "" CACHE STRING
    "Optional source list relative to JH_RP_NATIVE_APP_DIR")
if(JH_RP_NATIVE_APP_DIR)
    get_filename_component(JH_RP_NATIVE_APP_DIR
        "${JH_RP_NATIVE_APP_DIR}" ABSOLUTE BASE_DIR "${JH_ROOT}")
    if(NOT IS_DIRECTORY "${JH_RP_NATIVE_APP_DIR}")
        message(FATAL_ERROR
            "JH_RP_NATIVE_APP_DIR is not a directory: "
            "${JH_RP_NATIVE_APP_DIR}")
    endif()
    if(NOT DEFINED HAL_PROJECT_CONFIG_DIR OR
       "${HAL_PROJECT_CONFIG_DIR}" STREQUAL "")
        set(HAL_PROJECT_CONFIG_DIR "${JH_RP_NATIVE_APP_DIR}")
    endif()
endif()

include("${JH_ROOT}/cmake/jh_project_features.cmake")
jh_collect_cmake_feature_defines(_jh_cmake_feature_defines)
list(APPEND EXTRA_HAL_DEFINES ${_jh_cmake_feature_defines})
jh_normalize_feature_defines(EXTRA_HAL_DEFINES ${EXTRA_HAL_DEFINES})
set(_jh_project_features "")
if(DEFINED HAL_PROJECT_CONFIG_DIR)
    foreach(_jh_project_config_dir IN LISTS HAL_PROJECT_CONFIG_DIR)
        jh_collect_project_feature_defines(_jh_project_config_features
            "${_jh_project_config_dir}")
        list(APPEND _jh_project_features ${_jh_project_config_features})
    endforeach()
endif()
jh_resolve_feature_defines(
    JH_REQUESTED_FEATURES
    JH_RESOLVED_FEATURES
    ${EXTRA_HAL_DEFINES}
    ${_jh_project_features})
include("${JH_ROOT}/cmake/jh_board_profiles.cmake")
jh_generate_board_config(
    ROOT "${JH_ROOT}"
    TARGET "${JH_TARGET}"
    BOARD "${JH_BOARD}"
    OUTPUT_DIR
        "${CMAKE_BINARY_DIR}/generated/boards/${JH_TARGET}/${JH_BOARD}"
    OUTPUT_ROOT "${CMAKE_BINARY_DIR}"
    DEFINES ${EXTRA_HAL_DEFINES} ${_jh_project_features}
    REQUESTED_FEATURES ${JH_REQUESTED_FEATURES}
    RESOLVED_FEATURES ${JH_RESOLVED_FEATURES})
set(JH_BOARD "${JH_RESOLVED_BOARD}" CACHE STRING
    "Resolved physical board profile" FORCE)
set(PICO_PLATFORM "${PICO_PLATFORM}" CACHE STRING
    "Pico SDK platform selected by target" FORCE)
set(PICO_BOARD "${PICO_BOARD}" CACHE STRING
    "Pico SDK board selected by board profile" FORCE)
set(JH_RP_BOARD_DEFINES ${JH_BOARD_COMPILE_DEFINITIONS})

include("${JH_ROOT}/cmake/jh_board_components.cmake")
jh_apply_board_components(PROVIDER "${JH_BOARD_PROVIDER}")

if(JH_TARGET STREQUAL "rp2040")
    set(JH_RP_TARGET_DEFINE HAL_TARGET_RP2040)
elseif(JH_TARGET STREQUAL "rp2350-arm")
    set(JH_RP_TARGET_DEFINE HAL_TARGET_RP2350_ARM)
elseif(JH_TARGET STREQUAL "rp2350-riscv")
    set(JH_RP_TARGET_DEFINE HAL_TARGET_RP2350_RISCV)
else()
    message(FATAL_ERROR "Unsupported native RP target '${JH_TARGET}'")
endif()

if(DEFINED PICO_SDK_PATH)
    set(_jh_pico_sdk_import "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
elseif(DEFINED ENV{PICO_SDK_PATH})
    set(_jh_pico_sdk_import
        "$ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake")
else()
    message(FATAL_ERROR
        "PICO_SDK_PATH is not set. Run scripts/ensure_pico_sdk.sh and pass "
        "-DPICO_SDK_PATH=<path>.")
endif()
if(NOT EXISTS "${_jh_pico_sdk_import}")
    message(FATAL_ERROR "Pico SDK import file not found: ${_jh_pico_sdk_import}")
endif()

include("${_jh_pico_sdk_import}")

if(DEFINED PICO_FLASH_SIZE_BYTES)
    math(EXPR _jh_pico_flash_bytes "${PICO_FLASH_SIZE_BYTES}")
    if(NOT "${_jh_pico_flash_bytes}" STREQUAL
       "${JH_BOARD_EXPECTED_FLASH_BYTES}")
        message(FATAL_ERROR
            "Board ${JH_BOARD} requires ${JH_BOARD_EXPECTED_FLASH_BYTES} "
            "bytes of flash, but Pico SDK reports ${_jh_pico_flash_bytes}")
    endif()
endif()

# JaszczurHAL vendors and pins its own CYW43 driver. Preserve the physical
# board fact before disabling Pico SDK's separate CYW43/lwIP/btstack owner.
set(_jh_pico_board_has_cyw43 "${PICO_CYW43_SUPPORTED}")
if(_jh_pico_board_has_cyw43)
    set(PICO_CYW43_SUPPORTED 0)
endif()

project(JaszczurHAL_RP_Native C CXX ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JH_PICOTOOL_EXECUTABLE "" CACHE FILEPATH
    "Verified picotool executable used by Pico SDK post-processing")
if(JH_PICOTOOL_EXECUTABLE AND NOT TARGET picotool)
    if(NOT EXISTS "${JH_PICOTOOL_EXECUTABLE}")
        message(FATAL_ERROR
            "JH_PICOTOOL_EXECUTABLE does not exist: "
            "${JH_PICOTOOL_EXECUTABLE}")
    endif()
    add_executable(picotool IMPORTED GLOBAL)
    set_target_properties(picotool PROPERTIES
        IMPORTED_LOCATION "${JH_PICOTOOL_EXECUTABLE}")
endif()

include("${JH_ROOT}/cmake/jh_rp_native_sdk.cmake")
target_sources(JaszczurHAL PRIVATE
    "${JH_BOARD_GENERATED_DIR}/jh_link_contract_definition.c")
target_include_directories(JaszczurHAL PUBLIC "${JH_BOARD_GENERATED_DIR}")
file(REMOVE "${CMAKE_BINARY_DIR}/include/generated/jh_board_registry.h")
add_custom_command(TARGET JaszczurHAL POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_BINARY_DIR}/include/generated"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${JH_BOARD_GENERATED_DIR}/jh_board_config.h"
        "${JH_BOARD_GENERATED_DIR}/jh_link_contract.h"
        "${CMAKE_BINARY_DIR}/include/generated"
    VERBATIM)

option(JH_RP_NATIVE_BUILD_ARTIFACT_PROBE
    "Build a minimal link probe and produce ELF/BIN/UF2 artifacts" ON)
if(JH_RP_NATIVE_BUILD_ARTIFACT_PROBE)
    add_executable(jh_rp_native_artifact_probe artifact_probe.cpp)
    jh_add_rp_native_firmware(jh_rp_native_artifact_probe CUSTOM_ENTRY)
endif()

option(JH_RP_NATIVE_BUILD_CORE1_PROBE
    "Build the HAL-owned native entry/task1 path" ON)
if(JH_RP_NATIVE_BUILD_CORE1_PROBE)
    add_executable(jh_rp_native_core1_probe core1_probe.c)
    target_compile_definitions(jh_rp_native_core1_probe PRIVATE
        HAL_ENABLE_APP_TASK1=1)
    jh_add_rp_native_firmware(jh_rp_native_core1_probe)
endif()

if(JH_RP_NATIVE_APP_DIR)
    set(_jh_native_app_sources "")
    if(JH_RP_NATIVE_APP_SOURCES)
        foreach(_jh_native_app_source IN LISTS JH_RP_NATIVE_APP_SOURCES)
            get_filename_component(_jh_native_app_source_abs
                "${JH_RP_NATIVE_APP_DIR}/${_jh_native_app_source}" ABSOLUTE)
            if(NOT EXISTS "${_jh_native_app_source_abs}")
                message(FATAL_ERROR
                    "Native app source not found: ${_jh_native_app_source_abs}")
            endif()
            list(APPEND _jh_native_app_sources
                "${_jh_native_app_source_abs}")
        endforeach()
    else()
        file(GLOB _jh_native_app_sources CONFIGURE_DEPENDS
            "${JH_RP_NATIVE_APP_DIR}/*.c"
            "${JH_RP_NATIVE_APP_DIR}/*.cc"
            "${JH_RP_NATIVE_APP_DIR}/*.cpp"
        )
    endif()
    if(NOT _jh_native_app_sources)
        message(FATAL_ERROR
            "No C/C++ application sources found in "
            "${JH_RP_NATIVE_APP_DIR}")
    endif()
    add_executable(jh_rp_native_firmware ${_jh_native_app_sources})
    target_include_directories(jh_rp_native_firmware PRIVATE
        "${JH_RP_NATIVE_APP_DIR}")
    jh_add_rp_native_firmware(jh_rp_native_firmware)
endif()

install(TARGETS JaszczurHAL ARCHIVE DESTINATION lib)
install(DIRECTORY "${SRC}/" DESTINATION include
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "impl/.mock" EXCLUDE)
install(CODE [[
    file(REMOVE
        "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/include/generated/jh_board_registry.h")
]])
install(FILES
        "${JH_BOARD_GENERATED_DIR}/jh_board_config.h"
        "${JH_BOARD_GENERATED_DIR}/jh_link_contract.h"
        DESTINATION include/generated)
install(FILES
        "${JH_BOARD_GENERATED_DIR}/jh_link_contract_reference.c"
        "${JH_BOARD_GENERATED_DIR}/jh_board_resolved.json"
        DESTINATION share/JaszczurHAL/generated)
