include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/generated/jh_hal_features.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/jh_littlefs.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/jh_sx126x.cmake")

function(jh_feature_build_dependency_enabled OUT_VAR DEPENDENCY)
    cmake_parse_arguments(JH_EFFECT "" "" "FEATURES" ${ARGN})
    jh_hal_resolve_build_effects(
        _jh_unused_feature_sources
        _jh_unused_portable_sources
        _jh_dependencies
        ${JH_EFFECT_FEATURES})
    list(FIND _jh_dependencies "${DEPENDENCY}" _jh_dependency_index)
    if(_jh_dependency_index EQUAL -1)
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

# Resolve feature-owned sources and managed dependency manifests. Portable
# sources remain separate so selective inventories can append them directly;
# broad inventories can merge them and remove duplicate paths.
function(jh_collect_feature_build_effects PREFIX)
    cmake_parse_arguments(JH_EFFECT "" "ROOT" "FEATURES" ${ARGN})
    if(NOT JH_EFFECT_ROOT)
        get_filename_component(JH_EFFECT_ROOT
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    endif()

    jh_hal_resolve_build_effects(
        _jh_feature_sources
        _jh_portable_sources
        _jh_dependencies
        ${JH_EFFECT_FEATURES})

    set(_jh_resolved_feature_sources)
    foreach(_jh_source IN LISTS _jh_feature_sources)
        set(_jh_absolute_source "${JH_EFFECT_ROOT}/${_jh_source}")
        if(NOT EXISTS "${_jh_absolute_source}")
            message(FATAL_ERROR
                "Feature-owned source is missing: ${_jh_absolute_source}")
        endif()
        list(APPEND _jh_resolved_feature_sources "${_jh_absolute_source}")
    endforeach()

    set(_jh_resolved_portable_sources)
    foreach(_jh_source IN LISTS _jh_portable_sources)
        set(_jh_absolute_source "${JH_EFFECT_ROOT}/${_jh_source}")
        if(NOT EXISTS "${_jh_absolute_source}")
            message(FATAL_ERROR
                "Portable feature source is missing: ${_jh_absolute_source}")
        endif()
        list(APPEND _jh_resolved_portable_sources "${_jh_absolute_source}")
    endforeach()

    set(_jh_dependency_sources)
    set(_jh_dependency_include_dirs)
    set(_jh_littlefs_sources)
    set(_jh_sx126x_sources)
    foreach(_jh_dependency IN LISTS _jh_dependencies)
        if(_jh_dependency STREQUAL "bearssl")
            continue()
        elseif(_jh_dependency STREQUAL "littlefs")
            jh_littlefs_source_manifest(
                _jh_littlefs_sources _jh_littlefs_include_dirs)
            list(APPEND _jh_dependency_sources ${_jh_littlefs_sources})
            list(APPEND _jh_dependency_include_dirs
                ${_jh_littlefs_include_dirs})
        elseif(_jh_dependency STREQUAL "sx126x")
            jh_sx126x_source_manifest(
                _jh_sx126x_sources _jh_sx126x_include_dirs)
            list(APPEND _jh_dependency_sources ${_jh_sx126x_sources})
            list(APPEND _jh_dependency_include_dirs ${_jh_sx126x_include_dirs})
        else()
            message(FATAL_ERROR
                "Unsupported managed feature dependency: ${_jh_dependency}")
        endif()
    endforeach()

    set(${PREFIX}_FEATURE_SOURCES
        ${_jh_resolved_feature_sources} PARENT_SCOPE)
    set(${PREFIX}_PORTABLE_SOURCES
        ${_jh_resolved_portable_sources} PARENT_SCOPE)
    set(${PREFIX}_DEPENDENCY_SOURCES ${_jh_dependency_sources} PARENT_SCOPE)
    set(${PREFIX}_INCLUDE_DIRS ${_jh_dependency_include_dirs} PARENT_SCOPE)
    set(${PREFIX}_DEPENDENCIES ${_jh_dependencies} PARENT_SCOPE)
    set(${PREFIX}_LITTLEFS_SOURCES ${_jh_littlefs_sources} PARENT_SCOPE)
    set(${PREFIX}_SX126X_SOURCES ${_jh_sx126x_sources} PARENT_SCOPE)
endfunction()
