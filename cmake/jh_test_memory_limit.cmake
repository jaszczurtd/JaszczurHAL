# Memory ceiling of the native test programs of the host builds: the root
# project and the jh host CLI (tools/jh). On Linux a test that allocates
# without an end otherwise grows until the kernel kills processes of its own
# choice. tests/support/jh_test_memory_limit.c, linked into a test executable,
# ends such a run inside the test.
#
# Windows refuses the allocation at the commit limit of the system and has no
# such killer, so the ceiling is applied on Linux only.
include_guard(GLOBAL)

set(JH_TEST_MEMORY_LIMIT_MB 2048 CACHE STRING
    "Memory ceiling of one native test process on Linux in MiB (1..1048576); 0 removes it")
if(NOT JH_TEST_MEMORY_LIMIT_MB MATCHES "^(0|[1-9][0-9]*)$" OR
        JH_TEST_MEMORY_LIMIT_MB GREATER 1048576)
    message(FATAL_ERROR
        "JH_TEST_MEMORY_LIMIT_MB must be 0 or a number of MiB from 1 to 1048576: "
        "'${JH_TEST_MEMORY_LIMIT_MB}'")
endif()

set(_JH_TEST_MEMORY_LIMIT_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/../tests/support/jh_test_memory_limit.c")

# jh_test_memory_limit_active(<out>): whether this build limits its tests.
function(jh_test_memory_limit_active OUT_VAR)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT JH_TEST_MEMORY_LIMIT_MB EQUAL 0)
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    else()
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    endif()
endfunction()

# jh_limit_test_memory(<executable>...): gives the listed test executables the
# memory ceiling.
function(jh_limit_test_memory)
    jh_test_memory_limit_active(_active)
    if(NOT _active)
        return()
    endif()
    if(NOT TARGET jh_test_memory_limit)
        # One object for every test of the project, built with the options of
        # the directory (sanitizers included), not with those of a test.
        add_library(jh_test_memory_limit OBJECT "${_JH_TEST_MEMORY_LIMIT_SOURCE}")
        set_target_properties(jh_test_memory_limit PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
        target_compile_definitions(jh_test_memory_limit PRIVATE
            "JH_TEST_MEMORY_LIMIT_MB=${JH_TEST_MEMORY_LIMIT_MB}")
        target_compile_options(jh_test_memory_limit PRIVATE
            -Wall -Wextra -Werror -Wpedantic -Wconversion -Wstrict-prototypes)
    endif()
    foreach(_target IN LISTS ARGN)
        target_link_libraries(${_target} PRIVATE jh_test_memory_limit)
    endforeach()
endfunction()

# jh_limit_test_memory_in_directory(<directory>): the same for every executable
# defined so far in the directory and below it. Call it after the last target.
function(jh_limit_test_memory_in_directory DIRECTORY)
    get_property(_targets DIRECTORY "${DIRECTORY}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_type ${_target} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            jh_limit_test_memory(${_target})
        endif()
    endforeach()
    get_property(_subdirectories DIRECTORY "${DIRECTORY}" PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        jh_limit_test_memory_in_directory("${_subdirectory}")
    endforeach()
endfunction()
