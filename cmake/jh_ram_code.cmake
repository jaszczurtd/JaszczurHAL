include_guard(GLOBAL)

# HAL_RAM_CODE_SOURCES in hal_project_config.h names project source files
# whose code runs from RAM: SRAM on RP, CCM SRAM on STM32G474 and IRAM on
# ESP32. One C string literal names one file, relative to the project
# directory or absolute:
#
#   #define HAL_RAM_CODE_SOURCES "control.c" "../common/filter.c"
#
# Only the code (.text) of these files moves; their constants stay in flash.
# The target recipes call the functions below and check the linked map.

# jh_ram_code_parse(<out> <body>): the file names of a HAL_RAM_CODE_SOURCES
# body, in order. Stops the configure on anything but string literals.
function(jh_ram_code_parse OUT_VAR BODY)
    string(STRIP "${BODY}" _jh_rest)
    set(_jh_items "")
    while(NOT _jh_rest STREQUAL "")
        if(NOT _jh_rest MATCHES "^\"([^\"\\\\]*)\"(.*)$")
            message(FATAL_ERROR
                "[JH-RAM-CODE] HAL_RAM_CODE_SOURCES must list string "
                "literals, one file each, without escapes: ${BODY}")
        endif()
        set(_jh_item "${CMAKE_MATCH_1}")
        string(STRIP "${CMAKE_MATCH_2}" _jh_rest)
        if(_jh_item STREQUAL "" OR _jh_item MATCHES "[;*?]|\\[|\\]")
            message(FATAL_ERROR
                "[JH-RAM-CODE] HAL_RAM_CODE_SOURCES entry \"${_jh_item}\" is "
                "empty or contains ';', '*', '?', '[' or ']'")
        endif()
        list(APPEND _jh_items "${_jh_item}")
    endwhile()
    if(NOT _jh_items)
        message(FATAL_ERROR
            "[JH-RAM-CODE] HAL_RAM_CODE_SOURCES is defined but lists no file")
    endif()
    set(${OUT_VAR} "${_jh_items}" PARENT_SCOPE)
endfunction()

# jh_ram_code_resolve(<out> BASE_DIR <dir> ENTRIES <name>... SOURCES <src>...):
# absolute paths of the entries. Each one must exist, appear once and be one
# of the firmware SOURCES.
function(jh_ram_code_resolve OUT_VAR)
    cmake_parse_arguments(PARSE_ARGV 1 _JH_RC "" "BASE_DIR" "ENTRIES;SOURCES")
    set(_jh_known "")
    foreach(_jh_source IN LISTS _JH_RC_SOURCES)
        get_filename_component(_jh_abs "${_jh_source}" ABSOLUTE
            BASE_DIR "${_JH_RC_BASE_DIR}")
        list(APPEND _jh_known "${_jh_abs}")
    endforeach()
    set(_jh_result "")
    foreach(_jh_entry IN LISTS _JH_RC_ENTRIES)
        get_filename_component(_jh_abs "${_jh_entry}" ABSOLUTE
            BASE_DIR "${_JH_RC_BASE_DIR}")
        if(NOT EXISTS "${_jh_abs}")
            message(FATAL_ERROR
                "[JH-RAM-CODE] HAL_RAM_CODE_SOURCES entry \"${_jh_entry}\" "
                "not found: ${_jh_abs}")
        endif()
        if(NOT _jh_abs IN_LIST _jh_known)
            message(FATAL_ERROR
                "[JH-RAM-CODE] HAL_RAM_CODE_SOURCES entry \"${_jh_entry}\" is "
                "not a source of this firmware: ${_jh_abs}")
        endif()
        if(_jh_abs IN_LIST _jh_result)
            message(FATAL_ERROR
                "[JH-RAM-CODE] HAL_RAM_CODE_SOURCES lists \"${_jh_entry}\" "
                "twice")
        endif()
        list(APPEND _jh_result "${_jh_abs}")
    endforeach()
    set(${OUT_VAR} "${_jh_result}" PARENT_SCOPE)
endfunction()

# jh_ram_code_from_project(<out> BASE_DIR <dir> SOURCES <src>...): the
# resolved HAL_RAM_CODE_SOURCES of the project configuration that
# jh_read_project_config loaded, or an empty list.
function(jh_ram_code_from_project OUT_VAR)
    cmake_parse_arguments(PARSE_ARGV 1 _JH_RC "" "BASE_DIR" "SOURCES")
    set(_jh_result "")
    jh_project_defined(_jh_defined HAL_RAM_CODE_SOURCES)
    if(_jh_defined)
        jh_project_value(_jh_body HAL_RAM_CODE_SOURCES)
        jh_ram_code_parse(_jh_entries "${_jh_body}")
        jh_ram_code_resolve(_jh_result BASE_DIR "${_JH_RC_BASE_DIR}"
            ENTRIES ${_jh_entries} SOURCES ${_JH_RC_SOURCES})
    endif()
    set(${OUT_VAR} "${_jh_result}" PARENT_SCOPE)
endfunction()

# jh_ram_code_object_name(<out> <source>): the object file CMake makes from
# the source in this build.
function(jh_ram_code_object_name OUT_VAR SOURCE)
    get_filename_component(_jh_name "${SOURCE}" NAME)
    get_filename_component(_jh_ext "${SOURCE}" LAST_EXT)
    if(_jh_ext STREQUAL ".c")
        set(_jh_lang C)
    elseif(_jh_ext MATCHES "^\\.(cpp|cc|cxx|C)$")
        set(_jh_lang CXX)
    elseif(_jh_ext MATCHES "^\\.(S|s|asm)$")
        set(_jh_lang ASM)
    else()
        message(FATAL_ERROR
            "[JH-RAM-CODE] HAL_RAM_CODE_SOURCES names ${SOURCE}, which is not "
            "a C, C++ or assembly source")
    endif()
    set(_jh_object_ext "${CMAKE_${_jh_lang}_OUTPUT_EXTENSION}")
    if(_jh_object_ext STREQUAL "")
        message(FATAL_ERROR
            "[JH-RAM-CODE] ${SOURCE}: language ${_jh_lang} is not enabled")
    endif()
    if(CMAKE_${_jh_lang}_OUTPUT_EXTENSION_REPLACE)
        get_filename_component(_jh_name "${SOURCE}" NAME_WLE)
    endif()
    set(${OUT_VAR} "${_jh_name}${_jh_object_ext}" PARENT_SCOPE)
endfunction()

function(_jh_ram_code_ends_with OUT_VAR TEXT SUFFIX)
    string(LENGTH "${TEXT}" _jh_text_length)
    string(LENGTH "${SUFFIX}" _jh_suffix_length)
    set(_jh_result FALSE)
    if(NOT _jh_suffix_length GREATER _jh_text_length)
        math(EXPR _jh_start "${_jh_text_length} - ${_jh_suffix_length}")
        string(SUBSTRING "${TEXT}" ${_jh_start} -1 _jh_tail)
        if(_jh_tail STREQUAL SUFFIX)
            set(_jh_result TRUE)
        endif()
    endif()
    set(${OUT_VAR} ${_jh_result} PARENT_SCOPE)
endfunction()

# jh_ram_code_object_patterns(<out> RAM_SOURCES <src>... SOURCES <src>...):
# one linker file pattern per RAM source, "*/" plus the shortest trailing part
# of its path that no other firmware source ends with and the object name.
# A bare object name keeps matching when CMake shortens long object paths.
function(jh_ram_code_object_patterns OUT_VAR)
    cmake_parse_arguments(PARSE_ARGV 1 _JH_RC "" "" "RAM_SOURCES;SOURCES")
    set(_jh_others "")
    foreach(_jh_source IN LISTS _JH_RC_SOURCES)
        get_filename_component(_jh_source "${_jh_source}" ABSOLUTE)
        list(APPEND _jh_others "${_jh_source}")
    endforeach()
    set(_jh_patterns "")
    foreach(_jh_source IN LISTS _JH_RC_RAM_SOURCES)
        get_filename_component(_jh_source "${_jh_source}" ABSOLUTE)
        jh_ram_code_object_name(_jh_object "${_jh_source}")
        get_filename_component(_jh_dir "${_jh_source}" DIRECTORY)
        string(REPLACE "/" ";" _jh_parts "${_jh_dir}")
        list(REVERSE _jh_parts)
        list(LENGTH _jh_parts _jh_part_count)
        get_filename_component(_jh_name "${_jh_source}" NAME)
        set(_jh_suffix "${_jh_name}")
        set(_jh_object_suffix "${_jh_object}")
        set(_jh_index 0)
        while(TRUE)
            set(_jh_clash FALSE)
            foreach(_jh_other IN LISTS _jh_others)
                if(NOT _jh_other STREQUAL _jh_source)
                    _jh_ram_code_ends_with(_jh_match "${_jh_other}" "/${_jh_suffix}")
                    if(_jh_match)
                        set(_jh_clash TRUE)
                    endif()
                endif()
            endforeach()
            if(NOT _jh_clash)
                break()
            endif()
            if(NOT _jh_index LESS _jh_part_count)
                message(FATAL_ERROR
                    "[JH-RAM-CODE] no unique object pattern for ${_jh_source}")
            endif()
            list(GET _jh_parts ${_jh_index} _jh_part)
            math(EXPR _jh_index "${_jh_index} + 1")
            set(_jh_suffix "${_jh_part}/${_jh_suffix}")
            set(_jh_object_suffix "${_jh_part}/${_jh_object_suffix}")
        endwhile()
        list(APPEND _jh_patterns "*/${_jh_object_suffix}")
    endforeach()
    set(${OUT_VAR} "${_jh_patterns}" PARENT_SCOPE)
endfunction()

# jh_ram_code_esp_idf_entries(<out> RAM_SOURCES <src>... SOURCES <src>...):
# the ESP-IDF linker fragment object names of the RAM sources. ldgen matches
# "<name>.*.obj", so the name before the first dot must be unique among the
# component sources and a valid fragment identifier.
function(jh_ram_code_esp_idf_entries OUT_VAR)
    cmake_parse_arguments(PARSE_ARGV 1 _JH_RC "" "" "RAM_SOURCES;SOURCES")
    set(_jh_result "")
    foreach(_jh_source IN LISTS _JH_RC_RAM_SOURCES)
        get_filename_component(_jh_stem "${_jh_source}" NAME_WE)
        if(NOT _jh_stem MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
            message(FATAL_ERROR
                "[JH-RAM-CODE] ESP-IDF linker fragments cannot name "
                "${_jh_source}: '${_jh_stem}' is not an identifier")
        endif()
        foreach(_jh_other IN LISTS _JH_RC_SOURCES)
            get_filename_component(_jh_other_stem "${_jh_other}" NAME_WE)
            get_filename_component(_jh_other "${_jh_other}" ABSOLUTE)
            get_filename_component(_jh_abs "${_jh_source}" ABSOLUTE)
            if(_jh_other_stem STREQUAL _jh_stem AND NOT _jh_other STREQUAL _jh_abs)
                message(FATAL_ERROR
                    "[JH-RAM-CODE] ${_jh_source} and ${_jh_other} share the "
                    "object name '${_jh_stem}' an ESP-IDF linker fragment "
                    "selects; rename one of them")
            endif()
        endforeach()
        list(APPEND _jh_result "${_jh_stem}")
    endforeach()
    set(${OUT_VAR} "${_jh_result}" PARENT_SCOPE)
endfunction()

# jh_ram_code_add_map_check(TARGET <t> MAP <file> OBJECTS <text>...
#     FLASH <begin> <end> [<begin> <end>]...): after the link, every .text
# input section of each named object must lie outside the flash ranges.
# OBJECTS are the endings of the object names in the map, such as
# "/control.c.o" or "libmain.a(control.c.obj)".
function(jh_ram_code_add_map_check)
    cmake_parse_arguments(PARSE_ARGV 0 _JH_RC "" "TARGET;MAP" "OBJECTS;FLASH")
    # Resolved here: include_guard keeps a variable of the first includer's
    # scope out of later callers.
    set(_jh_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/jh_ram_code_check.cmake")
    if(NOT EXISTS "${_jh_script}" OR NOT _JH_RC_OBJECTS OR NOT _JH_RC_FLASH)
        message(FATAL_ERROR "[JH-RAM-CODE] map check misconfigured for ${_JH_RC_TARGET}")
    endif()
    list(JOIN _JH_RC_OBJECTS "|" _jh_objects)
    list(JOIN _JH_RC_FLASH "|" _jh_flash)
    add_custom_command(TARGET "${_JH_RC_TARGET}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DJH_RAM_CODE_MAP=${_JH_RC_MAP}"
            "-DJH_RAM_CODE_OBJECTS=${_jh_objects}"
            "-DJH_RAM_CODE_FLASH=${_jh_flash}"
            -P "${_jh_script}"
        VERBATIM)
endfunction()

# Linker script file list for EXCLUDE_FILE or an input section description.
function(jh_ram_code_quoted_patterns OUT_VAR)
    set(_jh_result "")
    foreach(_jh_pattern IN LISTS ARGN)
        string(APPEND _jh_result " \"${_jh_pattern}\"")
    endforeach()
    string(STRIP "${_jh_result}" _jh_result)
    set(${OUT_VAR} "${_jh_result}" PARENT_SCOPE)
endfunction()

# jh_ram_code_stm32_script(<out> <script> <output> <pattern>...): a copy of
# the HAL STM32G474 linker script that moves the .text of the objects matching
# the patterns from flash to its .ccmram_code section in CCM SRAM.
function(jh_ram_code_stm32_script OUT_VAR SCRIPT OUTPUT)
    file(READ "${SCRIPT}" _jh_script)
    jh_ram_code_quoted_patterns(_jh_quoted ${ARGN})
    set(_jh_text_old "        *(.text)\n        *(.text*)\n")
    set(_jh_ccm_marker
        "        /* JH_RAM_CODE_CCM: the .text of HAL_RAM_CODE_SOURCES objects. */\n")
    string(FIND "${_jh_script}" "${_jh_text_old}" _jh_text_at)
    string(FIND "${_jh_script}" "${_jh_text_old}" _jh_text_last REVERSE)
    string(FIND "${_jh_script}" "${_jh_ccm_marker}" _jh_ccm_at)
    if(_jh_text_at EQUAL -1 OR NOT _jh_text_at EQUAL _jh_text_last OR
       _jh_ccm_at EQUAL -1)
        message(FATAL_ERROR
            "[JH-RAM-CODE] ${SCRIPT} lacks the HAL_RAM_CODE_SOURCES markers")
    endif()
    set(_jh_text_new
        "        *(EXCLUDE_FILE(${_jh_quoted}) .text)\n        *(EXCLUDE_FILE(${_jh_quoted}) .text*)\n")
    set(_jh_ccm_new "${_jh_ccm_marker}")
    foreach(_jh_pattern IN LISTS ARGN)
        string(APPEND _jh_ccm_new "        \"${_jh_pattern}\"(.text .text*)\n")
    endforeach()
    string(REPLACE "${_jh_text_old}" "${_jh_text_new}" _jh_script "${_jh_script}")
    string(REPLACE "${_jh_ccm_marker}" "${_jh_ccm_new}" _jh_script "${_jh_script}")
    file(CONFIGURE OUTPUT "${OUTPUT}" CONTENT "${_jh_script}" @ONLY)
    set(${OUT_VAR} "${OUTPUT}" PARENT_SCOPE)
endfunction()
