# Check a GNU ld map after the link: every .text input section of the
# HAL_RAM_CODE_SOURCES objects lies outside flash. Run with cmake -P:
#   -DJH_RAM_CODE_MAP=<map file>
#   -DJH_RAM_CODE_OBJECTS=<object name ending>;...   ("/a.c.o", "libmain.a(a.c.obj)")
#   -DJH_RAM_CODE_FLASH=<begin>;<end>;...            (hexadecimal, end exclusive)
cmake_minimum_required(VERSION 3.22)

foreach(_jh_var IN ITEMS JH_RAM_CODE_MAP JH_RAM_CODE_OBJECTS JH_RAM_CODE_FLASH)
    if(NOT DEFINED ${_jh_var} OR "${${_jh_var}}" STREQUAL "")
        message(FATAL_ERROR "[JH-RAM-CODE] ${_jh_var} is required")
    endif()
endforeach()
if(NOT EXISTS "${JH_RAM_CODE_MAP}")
    message(FATAL_ERROR "[JH-RAM-CODE] linker map not found: ${JH_RAM_CODE_MAP}")
endif()

file(READ "${JH_RAM_CODE_MAP}" _jh_map)
string(REPLACE "\r" "" _jh_map "${_jh_map}")
set(_jh_objects "${JH_RAM_CODE_OBJECTS}")
set(_jh_flash "${JH_RAM_CODE_FLASH}")
list(LENGTH _jh_flash _jh_flash_count)
math(EXPR _jh_odd "${_jh_flash_count} % 2")
if(_jh_flash_count EQUAL 0 OR _jh_odd EQUAL 1)
    message(FATAL_ERROR "[JH-RAM-CODE] JH_RAM_CODE_FLASH needs begin/end pairs")
endif()

set(_jh_problems "")
set(_jh_moved 0)
foreach(_jh_object IN LISTS _jh_objects)
    string(REGEX REPLACE "([][+.*()^$?|\\\\{}])" "\\\\\\1" _jh_rx "${_jh_object}")
    # Input sections: " .name 0xADDR 0xSIZE file", the name may stand alone
    # on the previous line. The trailing newline stays for the next match.
    string(REGEX MATCHALL
        "[ \n]0x[0-9a-fA-F]+[ ]+0x[0-9a-fA-F]+[ ]+[^\n]*${_jh_rx}"
        _jh_any "${_jh_map}")
    if(NOT _jh_any)
        list(APPEND _jh_problems
            "${_jh_object}: not in the map; the file adds no linked code or "
            "its object name did not match")
        continue()
    endif()
    set(_jh_object_code 0)
    string(REGEX MATCHALL
        "\n \\.text[^ \n]*[ \n]+0x[0-9a-fA-F]+[ ]+0x[0-9a-fA-F]+[ ]+[^\n]*${_jh_rx}"
        _jh_lines "${_jh_map}")
    foreach(_jh_line IN LISTS _jh_lines)
        if(NOT _jh_line MATCHES
                "^\n (\\.text[^ \n]*)[ \n]+(0x[0-9a-fA-F]+)[ ]+(0x[0-9a-fA-F]+)")
            continue()
        endif()
        set(_jh_section "${CMAKE_MATCH_1}")
        set(_jh_address "${CMAKE_MATCH_2}")
        math(EXPR _jh_size "${CMAKE_MATCH_3}")
        math(EXPR _jh_value "${_jh_address}")
        if(_jh_size EQUAL 0 OR _jh_value EQUAL 0)
            # Empty or discarded by --gc-sections.
            continue()
        endif()
        set(_jh_in_flash FALSE)
        set(_jh_index 0)
        while(_jh_index LESS _jh_flash_count)
            list(GET _jh_flash ${_jh_index} _jh_begin)
            math(EXPR _jh_index "${_jh_index} + 1")
            list(GET _jh_flash ${_jh_index} _jh_end)
            math(EXPR _jh_index "${_jh_index} + 1")
            math(EXPR _jh_begin "${_jh_begin}")
            math(EXPR _jh_end "${_jh_end}")
            if(NOT _jh_value LESS _jh_begin AND _jh_value LESS _jh_end)
                set(_jh_in_flash TRUE)
            endif()
        endwhile()
        if(_jh_in_flash)
            list(APPEND _jh_problems
                "${_jh_object}: ${_jh_section} at ${_jh_address} is in flash")
        else()
            math(EXPR _jh_moved "${_jh_moved} + 1")
            math(EXPR _jh_object_code "${_jh_object_code} + 1")
        endif()
    endforeach()
    if(_jh_object_code EQUAL 0)
        list(APPEND _jh_problems
            "${_jh_object}: no linked code in RAM; list only files that add "
            "code to this build")
    endif()
endforeach()

if(_jh_problems)
    list(JOIN _jh_problems "\n  " _jh_report)
    message(FATAL_ERROR
        "[JH-RAM-CODE] HAL_RAM_CODE_SOURCES code was not placed in RAM:\n  "
        "${_jh_report}")
endif()
message(STATUS "[JH-RAM-CODE] ${_jh_moved} code sections of "
    "HAL_RAM_CODE_SOURCES run from RAM")
