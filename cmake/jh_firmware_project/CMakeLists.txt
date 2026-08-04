cmake_minimum_required(VERSION 3.20)

# ─────────────────────────────────────────────────────────────────────────────
# JaszczurHAL multi-target firmware dispatcher.
#
# A client project's .vscode manifest points cmake.sourceDir HERE and selects a
# target via cmake.cache:
#     JH_TARGET       = rp2040 | rp2350-arm | rp2350-riscv |
#                       stm32g474 (default: rp2040)
#     JH_PROJECT_DIR  = <the project's source dir> (required)
#     JH_MODULE_NAME  = firmware/sketch name      (optional; default: dir name)
# plus target-specific cache (PICO_PLATFORM/PICO_SDK_PATH for RP or
# CMAKE_TOOLCHAIN_FILE for stm32g474) and
# any project DEFINES/options.
#
# Whatever the target, this produces the stable jh-vscode target contract:
#     firmware / firmware_debug / firmware_upload / firmware_compile_db
#
# The dispatch shape (project(NONE) then per-target enable_language) mirrors the
# proven examples/CMakeLists.txt and keeps each carrier in one target recipe.
# ─────────────────────────────────────────────────────────────────────────────

set(JH_TARGET "rp2040" CACHE STRING
    "Target backend: rp2040 | rp2350-arm | rp2350-riscv | stm32g474")
set(JH_BOARD "" CACHE STRING
    "Physical board profile; defaults to the selected target's defaultBoard")
set(JH_EXTRA_DEFINES "" CACHE STRING
    "Extra compile definitions for the selected firmware")
set(JH_PROJECT_DIR "" CACHE PATH "Client project source directory")

if(NOT JH_PROJECT_DIR)
    message(FATAL_ERROR "JH_PROJECT_DIR is required (the client project source dir)")
endif()
get_filename_component(JH_PROJECT_DIR "${JH_PROJECT_DIR}" ABSOLUTE)
if(NOT IS_DIRECTORY "${JH_PROJECT_DIR}")
    message(FATAL_ERROR "JH_PROJECT_DIR not found: ${JH_PROJECT_DIR}")
endif()

# Repo root: this file is <JH_ROOT>/cmake/jh_firmware_project/CMakeLists.txt
get_filename_component(JH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${JH_ROOT}/src/JaszczurHAL.h")
    message(FATAL_ERROR "JaszczurHAL root not found at '${JH_ROOT}'")
endif()

include("${JH_ROOT}/cmake/jh_project_features.cmake")
jh_collect_project_feature_defines(
    _jh_project_feature_defines "${JH_PROJECT_DIR}")
set(_jh_project_config_file "${JH_PROJECT_DIR}/hal_project_config.h")
if(EXISTS "${_jh_project_config_file}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_jh_project_config_file}")
endif()

set(_jh_requested_pico_platform "")
set(_jh_requested_pico_board "")
if(DEFINED PICO_PLATFORM)
    set(_jh_requested_pico_platform "${PICO_PLATFORM}")
endif()
if(DEFINED PICO_BOARD)
    set(_jh_requested_pico_board "${PICO_BOARD}")
endif()

include("${JH_ROOT}/cmake/jh_board_profiles.cmake")
jh_generate_board_config(
    ROOT "${JH_ROOT}"
    TARGET "${JH_TARGET}"
    BOARD "${JH_BOARD}"
    OUTPUT_DIR
        "${CMAKE_BINARY_DIR}/generated/boards/${JH_TARGET}/${JH_BOARD}"
    OUTPUT_ROOT "${CMAKE_BINARY_DIR}"
    DEFINES ${JH_EXTRA_DEFINES} ${_jh_project_feature_defines})
set(JH_BOARD "${JH_RESOLVED_BOARD}" CACHE STRING
    "Resolved physical board profile" FORCE)

if(_jh_requested_pico_platform AND
   NOT "${_jh_requested_pico_platform}" STREQUAL "${PICO_PLATFORM}")
    message(FATAL_ERROR
        "PICO_PLATFORM=${_jh_requested_pico_platform} conflicts with "
        "resolved target ${JH_TARGET} (${PICO_PLATFORM})")
endif()
if(_jh_requested_pico_board AND
   NOT "${_jh_requested_pico_board}" STREQUAL "${PICO_BOARD}")
    message(FATAL_ERROR
        "PICO_BOARD=${_jh_requested_pico_board} conflicts with "
        "resolved board ${JH_BOARD} (${PICO_BOARD})")
endif()

include("${JH_ROOT}/cmake/jh_board_components.cmake")
jh_apply_board_components(PROVIDER "${JH_BOARD_PROVIDER}")

set(_jh_native_rp FALSE)
if(JH_TARGET STREQUAL "rp2040")
    set(_jh_native_rp TRUE)
    set(_jh_expected_pico_platform rp2040)
    set(JH_RP_TARGET_DEFINE HAL_TARGET_RP2040)
elseif(JH_TARGET STREQUAL "rp2350-arm")
    set(_jh_native_rp TRUE)
    set(_jh_expected_pico_platform rp2350-arm-s)
    set(JH_RP_TARGET_DEFINE HAL_TARGET_RP2350_ARM)
elseif(JH_TARGET STREQUAL "rp2350-riscv")
    set(_jh_native_rp TRUE)
    set(_jh_expected_pico_platform rp2350-riscv)
    set(JH_RP_TARGET_DEFINE HAL_TARGET_RP2350_RISCV)
endif()

if(_jh_native_rp)
    if(NOT "${PICO_PLATFORM}" STREQUAL "${_jh_expected_pico_platform}")
        message(FATAL_ERROR
            "JH_TARGET=${JH_TARGET} requires "
            "PICO_PLATFORM=${_jh_expected_pico_platform}, got ${PICO_PLATFORM}")
    endif()
    set(PICO_PLATFORM "${PICO_PLATFORM}" CACHE STRING
        "Pico SDK platform selected by the resolved target" FORCE)
    set(PICO_BOARD "${PICO_BOARD}" CACHE STRING
        "Pico SDK board selected by the resolved board profile" FORCE)
    if(DEFINED PICO_SDK_PATH)
        set(_jh_pico_sdk_import
            "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    elseif(DEFINED ENV{PICO_SDK_PATH})
        set(_jh_pico_sdk_import
            "$ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    else()
        message(FATAL_ERROR
            "PICO_SDK_PATH is required for native target ${JH_TARGET}")
    endif()
    if(NOT EXISTS "${_jh_pico_sdk_import}")
        message(FATAL_ERROR
            "Pico SDK import file not found: ${_jh_pico_sdk_import}")
    endif()
    include("${_jh_pico_sdk_import}")
    if(DEFINED PICO_FLASH_SIZE_BYTES)
        math(EXPR _jh_pico_flash_bytes "${PICO_FLASH_SIZE_BYTES}")
        if(NOT "${_jh_pico_flash_bytes}" STREQUAL
           "${JH_BOARD_EXPECTED_FLASH_BYTES}")
            message(FATAL_ERROR
                "Board ${JH_BOARD} requires "
                "${JH_BOARD_EXPECTED_FLASH_BYTES} bytes of flash, but Pico "
                "SDK reports ${_jh_pico_flash_bytes}")
        endif()
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH
        "Pico SDK toolchain selected for ${JH_TARGET}" FORCE)

    set(_jh_pico_board_has_cyw43 "${PICO_CYW43_SUPPORTED}")
    if(_jh_pico_board_has_cyw43)
        set(PICO_CYW43_SUPPORTED 0)
    endif()
    project(jh_firmware C CXX ASM)
else()
    project(jh_firmware NONE)
endif()

set(JH_MODULE_NAME "" CACHE STRING "Client firmware module/sketch name")
set(JH_PROJECT_SOURCES "" CACHE STRING "Optional ';'-separated project sources; relative paths resolve from JH_PROJECT_DIR")
set(JH_EXTRA_SOURCES "" CACHE STRING "Optional additional ';'-separated sources; relative paths resolve from JH_PROJECT_DIR")
set(JH_PROJECT_RECIPE "" CACHE FILEPATH "Optional project-specific recipe included after target selection")
set(JH_ARTIFACT_DIR "" CACHE PATH
    "Directory for final firmware artifacts")

if(NOT JH_MODULE_NAME)
    get_filename_component(JH_MODULE_NAME "${JH_PROJECT_DIR}" NAME)
endif()
if(NOT JH_ARTIFACT_DIR)
    set(JH_ARTIFACT_DIR "${JH_PROJECT_DIR}/.build")
endif()
get_filename_component(JH_ARTIFACT_DIR "${JH_ARTIFACT_DIR}" ABSOLUTE)
file(MAKE_DIRECTORY "${JH_ARTIFACT_DIR}")

message(STATUS
    "jh-firmware: target=${JH_TARGET} board=${JH_BOARD} module=${JH_MODULE_NAME} "
    "artifacts=${JH_ARTIFACT_DIR}")

function(jh_resolve_project_sources OUT_VAR)
    set(_sources "")
    if(JH_PROJECT_SOURCES)
        foreach(_src IN LISTS JH_PROJECT_SOURCES)
            if(IS_ABSOLUTE "${_src}")
                set(_src_abs "${_src}")
            else()
                get_filename_component(_src_abs "${JH_PROJECT_DIR}/${_src}" ABSOLUTE)
            endif()
            if(NOT EXISTS "${_src_abs}")
                message(FATAL_ERROR "JH_PROJECT_SOURCES entry not found: ${_src_abs}")
            endif()
            list(APPEND _sources "${_src_abs}")
        endforeach()
    else()
        file(GLOB _sources CONFIGURE_DEPENDS
            "${JH_PROJECT_DIR}/*.c"
            "${JH_PROJECT_DIR}/*.cpp"
            "${JH_PROJECT_DIR}/*.h"
            "${JH_PROJECT_DIR}/*.hpp"
        )
    endif()
    foreach(_src IN LISTS JH_EXTRA_SOURCES)
        if(IS_ABSOLUTE "${_src}")
            set(_src_abs "${_src}")
        else()
            get_filename_component(_src_abs "${JH_PROJECT_DIR}/${_src}" ABSOLUTE)
        endif()
        if(NOT EXISTS "${_src_abs}")
            message(FATAL_ERROR "JH_EXTRA_SOURCES entry not found: ${_src_abs}")
        endif()
        list(APPEND _sources "${_src_abs}")
    endforeach()
    list(REMOVE_DUPLICATES _sources)
    set(${OUT_VAR} ${_sources} PARENT_SCOPE)
endfunction()

if(JH_PROJECT_RECIPE)
    get_filename_component(JH_PROJECT_RECIPE "${JH_PROJECT_RECIPE}" ABSOLUTE BASE_DIR "${JH_PROJECT_DIR}")
    if(NOT EXISTS "${JH_PROJECT_RECIPE}")
        message(FATAL_ERROR "JH_PROJECT_RECIPE not found: ${JH_PROJECT_RECIPE}")
    endif()
    message(STATUS "jh-firmware: project recipe=${JH_PROJECT_RECIPE}")
    include("${JH_PROJECT_RECIPE}")
elseif(JH_TARGET STREQUAL "stm32g474")
    if(NOT CMAKE_TOOLCHAIN_FILE)
        message(FATAL_ERROR
            "CMAKE_TOOLCHAIN_FILE is required for target stm32g474")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH
        "STM32 toolchain selected for ${JH_TARGET}" FORCE)
    enable_language(C CXX ASM)
    include("${JH_ROOT}/cmake/targets/stm32g474.cmake")
elseif(_jh_native_rp)
    include("${JH_ROOT}/cmake/targets/rp-native.cmake")
else()
    message(FATAL_ERROR
        "Unsupported JH_TARGET='${JH_TARGET}' (expected rp2040, rp2350-arm, "
        "rp2350-riscv, or stm32g474)")
endif()
