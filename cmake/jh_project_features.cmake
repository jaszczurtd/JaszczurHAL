include_guard(GLOBAL)

# Collect direct HAL feature declarations from a project's public build
# configuration. The firmware dispatcher uses the result both for board
# capability validation and for selecting feature-owned source inventories.
function(jh_collect_project_feature_defines OUT_VAR PROJECT_DIR)
    set(_jh_features "")
    set(_jh_config_file "${PROJECT_DIR}/hal_project_config.h")
    if(EXISTS "${_jh_config_file}")
        file(STRINGS "${_jh_config_file}" _jh_feature_lines
            REGEX
            "^[ \t]*#[ \t]*define[ \t]+HAL_ENABLE_[A-Z0-9_]+([ \t]|$)"
        )
        foreach(_jh_line IN LISTS _jh_feature_lines)
            string(REGEX MATCH "HAL_ENABLE_[A-Z0-9_]+"
                _jh_feature "${_jh_line}")
            if(_jh_feature)
                list(APPEND _jh_features "${_jh_feature}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _jh_features)
    endif()
    set(${OUT_VAR} ${_jh_features} PARENT_SCOPE)
endfunction()
