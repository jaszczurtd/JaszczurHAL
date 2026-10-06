include_guard(GLOBAL)

# Python used by the configure-time generators: Python3_EXECUTABLE when the
# caller set it, otherwise python3 or python from PATH.
function(jh_find_python OUT_VAR)
    if(Python3_EXECUTABLE)
        if(NOT EXISTS "${Python3_EXECUTABLE}")
            message(FATAL_ERROR
                "Python3_EXECUTABLE does not exist: ${Python3_EXECUTABLE}")
        endif()
        set(${OUT_VAR} "${Python3_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()
    find_program(_jh_python NAMES python3 python REQUIRED)
    set(${OUT_VAR} "${_jh_python}" PARENT_SCOPE)
endfunction()
