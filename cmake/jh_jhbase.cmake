include_guard(GLOBAL)

# jhbase holds the status codes and COUNTOF behind hal/core/hal_status.h and
# hal/core/hal_array.h, so every HAL compile needs its headers. The managed
# checkout third_party/jhbase follows third_party/jhbase_version.conf;
# JH_JHBASE_DIR selects another checkout, as for scripts/ensure_jhbase.sh.
set(_jh_jhbase_hal_root "${CMAKE_CURRENT_LIST_DIR}/..")

# jh_jhbase_dir(<out dir> [<out revision>]): the checkout and the revision its
# package records (the pin, or empty for an external checkout, which then
# reports its own git HEAD). Stops the configure when the checkout is missing.
function(jh_jhbase_dir OUT_DIR)
    if(DEFINED ENV{JH_JHBASE_DIR} AND NOT "$ENV{JH_JHBASE_DIR}" STREQUAL "")
        get_filename_component(_jh_jb_dir "$ENV{JH_JHBASE_DIR}" ABSOLUTE)
        set(_jh_jb_revision "")
    else()
        file(STRINGS "${_jh_jhbase_hal_root}/third_party/jhbase_version.conf"
            _jh_jb_pin REGEX "^JHBASE_(REF|DIR)=")
        foreach(_jh_jb_line IN LISTS _jh_jb_pin)
            if(_jh_jb_line MATCHES "^JHBASE_REF=(.+)$")
                set(_jh_jb_revision "${CMAKE_MATCH_1}")
            elseif(_jh_jb_line MATCHES "^JHBASE_DIR=(.+)$")
                get_filename_component(_jh_jb_dir "${CMAKE_MATCH_1}" ABSOLUTE
                    BASE_DIR "${_jh_jhbase_hal_root}")
            endif()
        endforeach()
    endif()
    if(NOT EXISTS "${_jh_jb_dir}/include/jb/status.h")
        message(FATAL_ERROR
            "jhbase is missing: ${_jh_jb_dir}/include/jb/status.h. "
            "Run third_party/update_components.sh (or scripts/ensure_jhbase.sh)")
    endif()
    set(${OUT_DIR} "${_jh_jb_dir}" PARENT_SCOPE)
    if(ARGC GREATER 1)
        set(${ARGV1} "${_jh_jb_revision}" PARENT_SCOPE)
    endif()
endfunction()

# jh_jhbase_include_dir(<out>): the include directory, for compiles that do
# not link jb::status (ESP-IDF components, direct compiler calls).
function(jh_jhbase_include_dir OUT_INCLUDE)
    jh_jhbase_dir(_jh_jb_dir)
    set(${OUT_INCLUDE} "${_jh_jb_dir}/include" PARENT_SCOPE)
endfunction()

# jh_add_jhbase(): adds the jhbase project once per build, giving jb::status
# and jb::base. The library builds only when linked; installing the build
# writes the status headers and the jhbase package with the recorded revision
# into the install prefix.
macro(jh_add_jhbase)
    if(NOT TARGET jb::status)
        jh_jhbase_dir(_jh_jb_source JHBASE_REVISION)
        add_subdirectory("${_jh_jb_source}" "${CMAKE_BINARY_DIR}/jhbase")
        unset(_jh_jb_source)
        unset(JHBASE_REVISION)
    endif()
endmacro()
