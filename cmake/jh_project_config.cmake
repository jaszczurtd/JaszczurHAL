include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/jh_python.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/jh_project_features.cmake")

# The project's hal_project_config.h evaluated by scripts/project_config.py for
# one target and variant, exactly as the compiler sees it before the HAL
# headers. Every build reads project configuration through these helpers.

# Hand JH_PROJECT_CONFIG_VARIABLES, the variables it names,
# JH_PROJECT_CONFIG_LOADED and the extra names in ARGN from a reading function
# to its caller; a name the function left undefined is unset there too.
macro(_jh_project_config_export)
    foreach(_jh_pc_variable IN LISTS JH_PROJECT_CONFIG_VARIABLES ITEMS
            JH_PROJECT_CONFIG_VARIABLES JH_PROJECT_CONFIG_LOADED ${ARGN})
        if(DEFINED ${_jh_pc_variable})
            set(${_jh_pc_variable} "${${_jh_pc_variable}}" PARENT_SCOPE)
        else()
            unset(${_jh_pc_variable} PARENT_SCOPE)
        endif()
    endforeach()
endmacro()

# jh_read_project_config(ROOT <JaszczurHAL root> CONFIG_DIR <dir>
#     TARGET <registry id> OUTPUT_DIR <dir> [VARIANT <id>]
#     [DEFINES <NAME or NAME=VALUE>...])
#
# DEFINES are the -D values a static library build passes; a project build
# selects a VARIANT instead. Sets in the caller's scope JH_PROJECT_FEATURES,
# JH_PROJECT_DEFINITIONS (to compile with), JH_PROJECT_TARGETS,
# JH_PROJECT_VARIANTS, JH_PROJECT_VARIANT and the macro values read through
# jh_project_defined() and jh_project_value(). A function, so arguments such
# as Windows paths reach it unchanged. The build reconfigures when a file the
# reader read changes or the header appears.
function(jh_read_project_config)
    cmake_parse_arguments(PARSE_ARGV 0 _JH_PC ""
        "ROOT;CONFIG_DIR;TARGET;VARIANT;OUTPUT_DIR" "DEFINES")
    jh_find_python(_jh_pc_python)
    set(_jh_pc_command
        "${_jh_pc_python}" "${_JH_PC_ROOT}/scripts/project_config.py" cmake
        --target "${_JH_PC_TARGET}"
        --output "${_JH_PC_OUTPUT_DIR}/jh_project_config.cmake")
    if(NOT "${_JH_PC_CONFIG_DIR}" STREQUAL "")
        list(APPEND _jh_pc_command --config-dir "${_JH_PC_CONFIG_DIR}")
    endif()
    # A variant id is any C identifier, including CMake false constants.
    if(NOT "${_JH_PC_VARIANT}" STREQUAL "")
        list(APPEND _jh_pc_command --variant "${_JH_PC_VARIANT}")
    endif()
    foreach(_jh_pc_define IN LISTS _JH_PC_DEFINES)
        list(APPEND _jh_pc_command "--define=${_jh_pc_define}")
    endforeach()
    execute_process(
        COMMAND ${_jh_pc_command}
        RESULT_VARIABLE _jh_pc_result
        ERROR_VARIABLE _jh_pc_error)
    if(NOT _jh_pc_result EQUAL 0)
        string(STRIP "${_jh_pc_error}" _jh_pc_error)
        message(FATAL_ERROR "Project configuration failed: ${_jh_pc_error}")
    endif()
    include("${_JH_PC_OUTPUT_DIR}/jh_project_config.cmake")
    if(NOT CMAKE_SCRIPT_MODE_FILE AND NOT "${_JH_PC_CONFIG_DIR}" STREQUAL "")
        file(GLOB _jh_pc_header CONFIGURE_DEPENDS
            "${_JH_PC_CONFIG_DIR}/hal_project_config.h")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            ${JH_PROJECT_CONFIG_FILES})
    endif()
    set(JH_PROJECT_CONFIG_LOADED TRUE)
    _jh_project_config_export()
endfunction()

# jh_read_library_config(ROOT <root> TARGET <id> OUTPUT_DIR <dir>
#     DEFINES <library -D values>...)
#
# A static library build: its -D inputs and HAL_ENABLE_* CMake variables,
# normalized into EXTRA_HAL_DEFINES, plus the optional project header from
# HAL_PROJECT_CONFIG_DIR (one directory). Sets JH_PROJECT_* like
# jh_read_project_config(), EXTRA_HAL_DEFINES and JH_REQUESTED_FEATURES /
# JH_RESOLVED_FEATURES in the caller's scope.
function(jh_read_library_config)
    cmake_parse_arguments(PARSE_ARGV 0 _JH_LC ""
        "ROOT;TARGET;OUTPUT_DIR" "DEFINES")
    list(LENGTH HAL_PROJECT_CONFIG_DIR _jh_lc_config_dirs)
    if(_jh_lc_config_dirs GREATER 1)
        message(FATAL_ERROR
            "HAL_PROJECT_CONFIG_DIR names one directory, got "
            "'${HAL_PROJECT_CONFIG_DIR}'")
    endif()
    jh_collect_cmake_feature_defines(_jh_lc_cmake_features)
    jh_normalize_feature_defines(EXTRA_HAL_DEFINES
        ${_JH_LC_DEFINES} ${_jh_lc_cmake_features})
    # The build merges several lists (target defaults, --freertos, all
    # features, HAL_ENABLE_* variables), so a feature can arrive twice and in
    # both spellings. The first one stays as written; the reader then reports
    # repeats only in the project's own configuration.
    set(_jh_lc_defines "")
    set(_jh_lc_seen "")
    foreach(_jh_lc_define IN LISTS EXTRA_HAL_DEFINES)
        string(REGEX REPLACE "^(HAL_(ENABLE|DISABLE)_[A-Z0-9_]+)=1$" "\\1"
            _jh_lc_key "${_jh_lc_define}")
        list(FIND _jh_lc_seen "${_jh_lc_key}" _jh_lc_index)
        if(_jh_lc_index EQUAL -1)
            list(APPEND _jh_lc_seen "${_jh_lc_key}")
            list(APPEND _jh_lc_defines "${_jh_lc_define}")
        endif()
    endforeach()
    set(EXTRA_HAL_DEFINES ${_jh_lc_defines})
    jh_read_project_config(
        ROOT "${_JH_LC_ROOT}"
        CONFIG_DIR "${HAL_PROJECT_CONFIG_DIR}"
        TARGET "${_JH_LC_TARGET}"
        OUTPUT_DIR "${_JH_LC_OUTPUT_DIR}"
        DEFINES ${EXTRA_HAL_DEFINES})
    jh_resolve_feature_defines(
        JH_REQUESTED_FEATURES JH_RESOLVED_FEATURES ${JH_PROJECT_FEATURES})
    _jh_project_config_export(
        EXTRA_HAL_DEFINES JH_REQUESTED_FEATURES JH_RESOLVED_FEATURES)
endfunction()

function(_jh_project_require_certain NAME)
    if(NOT JH_PROJECT_CONFIG_LOADED)
        message(FATAL_ERROR
            "jh_read_project_config() must run before reading ${NAME}")
    endif()
    if(DEFINED JH_PROJECT_UNCERTAIN_${NAME})
        message(FATAL_ERROR
            "[JH-CFG-SCOPE] ${JH_PROJECT_CONFIG_HEADER}: ${NAME} depends on "
            "${JH_PROJECT_UNCERTAIN_${NAME}}, which the build does not pass "
            "to the compiler before this header")
    endif()
endfunction()

# TRUE when the evaluated configuration defines NAME.
function(jh_project_defined OUT_VAR NAME)
    _jh_project_require_certain("${NAME}")
    list(FIND JH_PROJECT_MACROS "${NAME}" _jh_index)
    if(_jh_index EQUAL -1)
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

# Value of NAME: the integer when it evaluates to one, otherwise its text;
# empty when NAME is not defined. A value built from names the configuration
# does not define (SDK macros, C symbols) exists only for the compiler.
function(jh_project_value OUT_VAR NAME)
    _jh_project_require_certain("${NAME}")
    if(DEFINED JH_PROJECT_UNRESOLVED_${NAME})
        message(FATAL_ERROR
            "[JH-CFG-SCOPE] ${JH_PROJECT_CONFIG_HEADER}: the value of ${NAME} "
            "uses ${JH_PROJECT_UNRESOLVED_${NAME}}, which the project "
            "configuration does not define, so the build cannot read it; "
            "give ${NAME} a value the header computes itself")
    endif()
    if(DEFINED JH_PROJECT_INT_${NAME})
        set(${OUT_VAR} "${JH_PROJECT_INT_${NAME}}" PARENT_SCOPE)
    elseif(DEFINED JH_PROJECT_DEFINE_${NAME})
        set(${OUT_VAR} "${JH_PROJECT_DEFINE_${NAME}}" PARENT_SCOPE)
    else()
        set(${OUT_VAR} "" PARENT_SCOPE)
    endif()
endfunction()

# TRUE when the build defines NAME: the project configuration (header and its
# -D inputs) or the board's compile definitions passed as ARGN.
function(jh_build_defined OUT_VAR NAME)
    jh_project_defined(_jh_defined "${NAME}")
    if(NOT _jh_defined)
        foreach(_jh_definition IN LISTS ARGN)
            if("${_jh_definition}" STREQUAL "${NAME}" OR
               "${_jh_definition}" MATCHES "^${NAME}=")
                set(_jh_defined TRUE)
            endif()
        endforeach()
    endif()
    set(${OUT_VAR} ${_jh_defined} PARENT_SCOPE)
endfunction()

# Value of NAME for the build, from the project configuration or else from the
# board's compile definitions passed as ARGN. A board value keeps no C integer
# suffix (u, U, l, L), so the linker can take it. Empty when undefined.
function(jh_build_define_value OUT_VAR NAME)
    jh_project_defined(_jh_defined "${NAME}")
    if(_jh_defined)
        jh_project_value(_jh_value "${NAME}")
        set(${OUT_VAR} "${_jh_value}" PARENT_SCOPE)
        return()
    endif()
    set(_jh_value "")
    foreach(_jh_definition IN LISTS ARGN)
        if("${_jh_definition}" MATCHES "^${NAME}=(.+)$")
            set(_jh_value "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    string(REGEX REPLACE "[uUlL]+$" "" _jh_value "${_jh_value}")
    set(${OUT_VAR} "${_jh_value}" PARENT_SCOPE)
endfunction()

# Split a definition list into its plain definitions and its HAL feature
# definitions. The compiler gets both; feature selection and its checks take
# features only from the project configuration, which already applied the
# header to the feature definitions (a header may #undef one).
function(jh_split_project_definitions PLAIN_OUT FEATURE_OUT)
    set(_jh_plain ${ARGN})
    set(_jh_features ${ARGN})
    set(_jh_feature_regex "^(-D)?HAL_(ENABLE|DISABLE)_[A-Z0-9_]+(=.*)?$")
    list(FILTER _jh_plain EXCLUDE REGEX "${_jh_feature_regex}")
    list(FILTER _jh_features INCLUDE REGEX "${_jh_feature_regex}")
    set(${PLAIN_OUT} ${_jh_plain} PARENT_SCOPE)
    set(${FEATURE_OUT} ${_jh_features} PARENT_SCOPE)
endfunction()
