include_guard(GLOBAL)

# jhbase holds the status codes and COUNTOF behind hal/core/hal_status.h and
# hal/core/hal_array.h, so every HAL compile needs its headers. The managed
# checkout third_party/jhbase follows third_party/jhbase_version.conf;
# JH_JHBASE_DIR selects another checkout, as for scripts/ensure_jhbase.sh.
set(_jh_jhbase_hal_root "${CMAKE_CURRENT_LIST_DIR}/..")

# jh_dependency_root(<out>): the checkout holding the managed third_party
# directories. JH_DEPENDENCY_ROOT in the environment names another
# JaszczurHAL checkout, so an exported tree (the pre-commit index snapshot,
# a gate copy) builds with verified checkouts it does not contain; the pins
# always come from this checkout.
function(jh_dependency_root OUT_DIR)
    if(DEFINED ENV{JH_DEPENDENCY_ROOT} AND NOT "$ENV{JH_DEPENDENCY_ROOT}" STREQUAL "")
        get_filename_component(_jh_dep_root "$ENV{JH_DEPENDENCY_ROOT}" ABSOLUTE)
    else()
        get_filename_component(_jh_dep_root "${_jh_jhbase_hal_root}" ABSOLUTE)
    endif()
    set(${OUT_DIR} "${_jh_dep_root}" PARENT_SCOPE)
endfunction()

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
                jh_dependency_root(_jh_jb_base)
                get_filename_component(_jh_jb_dir "${CMAKE_MATCH_1}" ABSOLUTE
                    BASE_DIR "${_jh_jb_base}")
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
        # Keep the managed jhbase checkout clean when Unicode is enabled.
        # Callers may still select an explicit checkout with the jhbase option.
        if(JHBASE_UNICODE AND "${JHBASE_UTF8PROC_DIR}" STREQUAL "")
            file(READ "${_jh_jhbase_hal_root}/config/tooling/managed_components.json"
                _jh_jb_components)
            string(JSON _jh_jb_utf8proc_dir GET "${_jh_jb_components}"
                jhbasePins utf8proc directory)
            jh_dependency_root(_jh_jb_base)
            get_filename_component(JHBASE_UTF8PROC_DIR
                "${_jh_jb_base}/${_jh_jb_utf8proc_dir}" ABSOLUTE)
            unset(_jh_jb_base)
            unset(_jh_jb_components)
            unset(_jh_jb_utf8proc_dir)
        endif()
        add_subdirectory("${_jh_jb_source}" "${CMAKE_BINARY_DIR}/jhbase")
        unset(_jh_jb_source)
        unset(JHBASE_REVISION)
    endif()
endmacro()
