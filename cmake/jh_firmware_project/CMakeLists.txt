cmake_minimum_required(VERSION 3.20)

# ─────────────────────────────────────────────────────────────────────────────
# JaszczurHAL multi-target firmware dispatcher.
#
# A client project's .vscode manifest points cmake.sourceDir HERE and selects a
# target via cmake.cache:
#     JH_TARGET       = rp2040 | stm32g474        (default: rp2040)
#     JH_PROJECT_DIR  = <the project's source dir> (required)
#     JH_MODULE_NAME  = firmware/sketch name      (optional; default: dir name)
# plus target-specific cache (ARDUINO_FQBN for rp2040; CMAKE_TOOLCHAIN_FILE for
# stm32g474) and any project DEFINES/options.
#
# Whatever the target, this produces the canonical jh-vscode target contract:
#     firmware / firmware_debug / firmware_upload / firmware_compile_db
#
# The dispatch shape (project(NONE) then per-target enable_language) mirrors the
# proven examples/CMakeLists.txt, so the RP2040 path stays byte-for-byte the
# arduino-cli flow it always was.
# ─────────────────────────────────────────────────────────────────────────────

project(jh_firmware NONE)

set(JH_TARGET "rp2040" CACHE STRING "Target backend: rp2040 | stm32g474")
set(JH_PROJECT_DIR "" CACHE PATH "Client project source directory")
set(JH_MODULE_NAME "" CACHE STRING "Client firmware module/sketch name")
set(JH_PROJECT_SOURCES "" CACHE STRING "Optional ';'-separated project sources; relative paths resolve from JH_PROJECT_DIR")
set(JH_PROJECT_RECIPE "" CACHE FILEPATH "Optional project-specific recipe included after target selection")

# Repo root: this file is <JH_ROOT>/cmake/jh_firmware_project/CMakeLists.txt
get_filename_component(JH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${JH_ROOT}/library.properties")
    message(FATAL_ERROR "JaszczurHAL root not found at '${JH_ROOT}'")
endif()

if(NOT JH_PROJECT_DIR)
    message(FATAL_ERROR "JH_PROJECT_DIR is required (the client project source dir)")
endif()
get_filename_component(JH_PROJECT_DIR "${JH_PROJECT_DIR}" ABSOLUTE)
if(NOT IS_DIRECTORY "${JH_PROJECT_DIR}")
    message(FATAL_ERROR "JH_PROJECT_DIR not found: ${JH_PROJECT_DIR}")
endif()
if(NOT JH_MODULE_NAME)
    get_filename_component(JH_MODULE_NAME "${JH_PROJECT_DIR}" NAME)
endif()

message(STATUS "jh-firmware: target=${JH_TARGET} module=${JH_MODULE_NAME}")

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
    enable_language(C CXX ASM)
    include("${JH_ROOT}/cmake/targets/stm32g474.cmake")
elseif(JH_TARGET STREQUAL "rp2040")
    include("${JH_ROOT}/cmake/targets/rp2040.cmake")
else()
    message(FATAL_ERROR "Unsupported JH_TARGET='${JH_TARGET}' (expected rp2040 | stm32g474)")
endif()
