include_guard(GLOBAL)

# Reader for the tracked third_party/*_version.conf pin files. It follows
# scripts/component_manager.py parse_config(): KEY=value lines, optional
# single or double quotes around the value, and the last assignment wins.

# jh_read_version_pin(CONF KEY OUT_VAR [DEFAULT <value>])
# Without DEFAULT a missing file, key or empty value is a configure error.
function(jh_read_version_pin CONF KEY OUT_VAR)
    cmake_parse_arguments(PARSE_ARGV 3 _jh "" "DEFAULT" "")
    set(_jh_value "")
    if(EXISTS "${CONF}")
        file(STRINGS "${CONF}" _jh_lines REGEX "^[ \t]*${KEY}[ \t]*=")
        foreach(_jh_line IN LISTS _jh_lines)
            string(REGEX REPLACE "^[ \t]*${KEY}[ \t]*=(.*)$" "\\1"
                _jh_value "${_jh_line}")
            string(STRIP "${_jh_value}" _jh_value)
            string(REGEX REPLACE "^\"(.*)\"$" "\\1" _jh_value "${_jh_value}")
            string(REGEX REPLACE "^'(.*)'$" "\\1" _jh_value "${_jh_value}")
        endforeach()
    endif()
    if("${_jh_value}" STREQUAL "")
        if(DEFINED _jh_DEFAULT)
            set(_jh_value "${_jh_DEFAULT}")
        else()
            message(FATAL_ERROR "${CONF} does not assign ${KEY}")
        endif()
    endif()
    set(${OUT_VAR} "${_jh_value}" PARENT_SCOPE)
endfunction()
