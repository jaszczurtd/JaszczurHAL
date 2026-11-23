# HAL_RAM_CODE_SOURCES: parsing, path resolution, object patterns, ESP-IDF
# fragment names, the STM32 linker script copy and the linked-map check.
# Cases that must stop run in a child cmake -P with JH_RAM_CODE_CASE set.
cmake_minimum_required(VERSION 3.22)
if(NOT JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()
include("${JH_ROOT}/cmake/jh_ram_code.cmake")
include("${JH_ROOT}/tests/jh_test_artifacts.cmake")

jh_test_artifact_dir(_work ram-code)
set(_proj "${_work}/proj")
set(CMAKE_C_OUTPUT_EXTENSION ".o")
set(CMAKE_CXX_OUTPUT_EXTENSION ".obj")

function(_fail TEXT)
    message(FATAL_ERROR "test_ram_code: ${TEXT}")
endfunction()

function(_check_map MAP OUT_RESULT OUT_OUTPUT)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DJH_RAM_CODE_MAP=${MAP}"
            "-DJH_RAM_CODE_OBJECTS=/proj/a.c.o"
            "-DJH_RAM_CODE_FLASH=0x10000000|0x20000000"
            -P "${JH_ROOT}/cmake/jh_ram_code_check.cmake"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    set(${OUT_RESULT} "${_result}" PARENT_SCOPE)
    set(${OUT_OUTPUT} "${_out}${_err}" PARENT_SCOPE)
endfunction()

if(DEFINED JH_RAM_CODE_CASE)
    set(_sources "${_proj}/a.c" "${_proj}/b.c")
    if(JH_RAM_CODE_CASE STREQUAL "unquoted")
        jh_ram_code_parse(_x "\"a.c\" b.c")
    elseif(JH_RAM_CODE_CASE STREQUAL "empty")
        jh_ram_code_parse(_x "  ")
    elseif(JH_RAM_CODE_CASE STREQUAL "missing")
        jh_ram_code_resolve(_x BASE_DIR "${_proj}" ENTRIES "none.c"
            SOURCES ${_sources})
    elseif(JH_RAM_CODE_CASE STREQUAL "foreign")
        jh_ram_code_resolve(_x BASE_DIR "${_proj}" ENTRIES "c.c"
            SOURCES ${_sources})
    elseif(JH_RAM_CODE_CASE STREQUAL "twice")
        jh_ram_code_resolve(_x BASE_DIR "${_proj}" ENTRIES "a.c" "./a.c"
            SOURCES ${_sources})
    elseif(JH_RAM_CODE_CASE STREQUAL "esp_stem")
        jh_ram_code_esp_idf_entries(_x RAM_SOURCES "${_proj}/a.c"
            SOURCES "${_proj}/a.c" "${_proj}/a.cpp")
    elseif(JH_RAM_CODE_CASE STREQUAL "stm32_markers")
        file(WRITE "${_work}/plain.ld" "SECTIONS { .text : { *(.text*) } }\n")
        jh_ram_code_stm32_script(_x "${_work}/plain.ld" "${_work}/out.ld" "*/a.c.o")
    endif()
    message(STATUS "UNEXPECTED SUCCESS")
    return()
endif()

file(REMOVE_RECURSE "${_work}")
file(WRITE "${_proj}/a.c" "")
file(WRITE "${_proj}/b.c" "")
file(WRITE "${_proj}/c.c" "")
file(WRITE "${_work}/common/x/b.c" "")
file(WRITE "${_work}/other/x/b.c" "")
file(WRITE "${_work}/lib/fast.cpp" "")

# Literals in order; whitespace between them as joined continuation lines.
jh_ram_code_parse(_entries "\"a.c\"   \"../common/x/b.c\"  \"../lib/fast.cpp\"")
if(NOT "${_entries}" STREQUAL "a.c;../common/x/b.c;../lib/fast.cpp")
    _fail("parse order: ${_entries}")
endif()

set(_sources "${_proj}/a.c" "${_proj}/b.c" "${_work}/common/x/b.c"
    "${_work}/other/x/b.c" "${_work}/lib/fast.cpp")
jh_ram_code_resolve(_ram BASE_DIR "${_proj}" ENTRIES ${_entries}
    SOURCES ${_sources})
if(NOT "${_ram}" STREQUAL
        "${_proj}/a.c;${_work}/common/x/b.c;${_work}/lib/fast.cpp")
    _fail("resolve: ${_ram}")
endif()

# The shortest unique path ending: b.c and x/b.c clash with other sources.
jh_ram_code_object_patterns(_patterns RAM_SOURCES ${_ram} SOURCES ${_sources})
if(NOT "${_patterns}" STREQUAL "*/a.c.o;*/common/x/b.c.o;*/fast.cpp.obj")
    _fail("patterns: ${_patterns}")
endif()

jh_ram_code_esp_idf_entries(_esp RAM_SOURCES "${_proj}/a.c" "${_work}/lib/fast.cpp"
    SOURCES "${_proj}/a.c" "${_proj}/b.c" "${_work}/lib/fast.cpp")
if(NOT "${_esp}" STREQUAL "a;fast")
    _fail("ESP-IDF entries: ${_esp}")
endif()

# The STM32 script copy excludes the objects from .text and lists them in CCM.
jh_ram_code_stm32_script(_script
    "${JH_ROOT}/link_libraries/stm32_lib/STM32G474RETx_FLASH.ld"
    "${_work}/stm32.ld" "*/a.c.o" "*/my dir/b.c.o")
file(READ "${_script}" _ld)
string(FIND "${_ld}" "*(EXCLUDE_FILE(\"*/a.c.o\" \"*/my dir/b.c.o\") .text)\n" _at1)
string(FIND "${_ld}" "*(EXCLUDE_FILE(\"*/a.c.o\" \"*/my dir/b.c.o\") .text*)\n" _at2)
string(FIND "${_ld}" "\"*/my dir/b.c.o\"(.text .text*)\n" _at3)
string(FIND "${_ld}" "        *(.text*)\n" _plain)
if(_at1 EQUAL -1 OR _at2 EQUAL -1 OR _at3 EQUAL -1 OR NOT _plain EQUAL -1)
    _fail("STM32 script copy")
endif()

# Map check: sections in RAM pass, a discarded one and a wrapped name count.
set(_map_ok [=[
Discarded input sections

 .text.unused   0x00000000       0x10 CMakeFiles/fw.dir/proj/a.c.o

Linker script and memory map

.data           0x20000000      0x200
 .text.fast     0x20000010       0x40 CMakeFiles/fw.dir/proj/a.c.o
 .text.a_function_name_long_enough_to_wrap
                0x20000050       0x20 CMakeFiles/fw.dir/proj/a.c.o
 .text.empty    0x20000070        0x0 CMakeFiles/fw.dir/proj/a.c.o
.text           0x10000000     0x1000
 .text.other    0x10000100       0x30 CMakeFiles/fw.dir/proj/za.c.o
]=])
file(WRITE "${_work}/ok.map" "${_map_ok}")
_check_map("${_work}/ok.map" _result _output)
if(NOT _result EQUAL 0 OR NOT _output MATCHES "\\] 2 code sections")
    _fail("map check passes and counts both RAM sections: ${_output}")
endif()

string(REPLACE " .text.other    0x10000100       0x30 CMakeFiles/fw.dir/proj/za.c.o"
    " .text.slow     0x10000100       0x30 CMakeFiles/fw.dir/proj/a.c.o"
    _map_flash "${_map_ok}")
file(WRITE "${_work}/flash.map" "${_map_flash}")
_check_map("${_work}/flash.map" _result _output)
if(_result EQUAL 0 OR NOT _output MATCHES "text\\.slow at 0x10000100 is in flash")
    _fail("map check rejects code in flash: ${_output}")
endif()

string(REGEX REPLACE " \\.text\\.fast[^\n]*\n" ""
    _map_empty "${_map_ok}")
string(REGEX REPLACE " \\.text\\.a_function[^\n]*\n[^\n]*\n" ""
    _map_empty "${_map_empty}")
file(WRITE "${_work}/empty.map" "${_map_empty}")
_check_map("${_work}/empty.map" _result _output)
if(_result EQUAL 0 OR NOT _output MATCHES "no linked code in RAM")
    _fail("map check rejects an object without linked code: ${_output}")
endif()

string(REPLACE "proj/a.c.o" "proj/q.c.o" _map_missing "${_map_ok}")
file(WRITE "${_work}/missing.map" "${_map_missing}")
_check_map("${_work}/missing.map" _result _output)
if(_result EQUAL 0 OR NOT _output MATCHES "not in the map")
    _fail("map check rejects a missing object: ${_output}")
endif()

foreach(_case IN ITEMS unquoted empty missing foreign twice esp_stem stm32_markers)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DJH_ROOT=${JH_ROOT}"
            "-DJH_RAM_CODE_CASE=${_case}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(_result EQUAL 0 OR NOT "${_err}" MATCHES "JH-RAM-CODE")
        _fail("case ${_case} must stop: ${_out}${_err}")
    endif()
endforeach()

file(REMOVE_RECURSE "${_work}")
message(STATUS "test_ram_code: all checks passed")
