include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/jh_project_features.cmake")

function(_jh_validate_rp_feature_inputs DEFINE_NAME)
    if(DEFINED ${DEFINE_NAME})
        string(REGEX MATCH "^HAL_(ENABLE|DISABLE)_" _jh_is_hal_feature
            "${DEFINE_NAME}")
        if(_jh_is_hal_feature)
            jh_validate_cmake_feature_variable("${DEFINE_NAME}")
            jh_normalize_feature_defines(
                _jh_direct_extra_defines ${EXTRA_HAL_DEFINES})
            jh_normalize_feature_defines(
                _jh_direct_board_defines ${JH_RP_BOARD_DEFINES})
            set(_jh_direct_defines
                ${_jh_direct_extra_defines} ${_jh_direct_board_defines})
            list(FIND _jh_direct_defines
                "${DEFINE_NAME}=1" _jh_direct_explicit_index)
            list(FIND _jh_direct_defines
                "${DEFINE_NAME}" _jh_direct_bare_index)
            if(_jh_direct_explicit_index EQUAL -1 AND
               _jh_direct_bare_index EQUAL -1)
                message(FATAL_ERROR
                    "[JH-CFG-VALUE] direct CMake variable ${DEFINE_NAME} "
                    "must be normalized into EXTRA_HAL_DEFINES before source "
                    "selection")
            endif()
        else()
            jh_validate_feature_defines("${DEFINE_NAME}=${${DEFINE_NAME}}")
        endif()
    endif()
    if(DEFINED EXTRA_HAL_DEFINES)
        jh_validate_feature_defines(${EXTRA_HAL_DEFINES})
    endif()
    if(DEFINED JH_RP_BOARD_DEFINES)
        jh_validate_feature_defines(${JH_RP_BOARD_DEFINES})
    endif()
    if(DEFINED HAL_PROJECT_CONFIG_DIR)
        foreach(_config_dir IN LISTS HAL_PROJECT_CONFIG_DIR)
            jh_collect_project_feature_defines(_jh_unused_features
                "${_config_dir}")
        endforeach()
    endif()
endfunction()

# Report whether a HAL feature is present in the registry-resolved closure.
# Non-feature macros keep the legacy direct-definition lookup used for provider
# selectors and tunables.
function(jh_hal_define_enabled OUT_VAR DEFINE_NAME)
    _jh_validate_rp_feature_inputs("${DEFINE_NAME}")
    jh_normalize_feature_defines(_jh_extra_defines ${EXTRA_HAL_DEFINES})
    jh_normalize_feature_defines(_jh_board_defines ${JH_RP_BOARD_DEFINES})
    set(_enabled FALSE)
    if(DEFINE_NAME MATCHES "^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+$")
        if(DEFINED JH_RESOLVED_FEATURES)
            set(_jh_resolved_features ${JH_RESOLVED_FEATURES})
        else()
            set(_jh_project_features "")
            if(DEFINED HAL_PROJECT_CONFIG_DIR)
                foreach(_config_dir IN LISTS HAL_PROJECT_CONFIG_DIR)
                    jh_collect_project_feature_defines(
                        _jh_config_features "${_config_dir}")
                    list(APPEND _jh_project_features
                        ${_jh_config_features})
                endforeach()
            endif()
            jh_resolve_feature_defines(
                _jh_requested_features
                _jh_resolved_features
                ${_jh_extra_defines}
                ${_jh_board_defines}
                ${_jh_project_features})
        endif()
        list(FIND _jh_resolved_features "${DEFINE_NAME}"
            _jh_resolved_index)
        if(NOT _jh_resolved_index EQUAL -1)
            set(_enabled TRUE)
        endif()
        set(${OUT_VAR} ${_enabled} PARENT_SCOPE)
        return()
    endif()
    if(DEFINED ${DEFINE_NAME})
        set(_enabled TRUE)
    endif()
    if(DEFINED EXTRA_HAL_DEFINES)
        foreach(_def IN LISTS _jh_extra_defines)
            string(COMPARE EQUAL "${_def}" "${DEFINE_NAME}"
                _jh_is_bare_define)
            string(REGEX MATCH "^${DEFINE_NAME}=" _jh_is_valued_define
                "${_def}")
            if(_jh_is_bare_define OR _jh_is_valued_define)
                set(_enabled TRUE)
            endif()
        endforeach()
    endif()
    if(DEFINED JH_RP_BOARD_DEFINES)
        foreach(_def IN LISTS _jh_board_defines)
            string(COMPARE EQUAL "${_def}" "${DEFINE_NAME}"
                _jh_is_bare_define)
            string(REGEX MATCH "^${DEFINE_NAME}=" _jh_is_valued_define
                "${_def}")
            if(_jh_is_bare_define OR _jh_is_valued_define)
                set(_enabled TRUE)
            endif()
        endforeach()
    endif()
    if(DEFINED HAL_PROJECT_CONFIG_DIR)
        foreach(_config_dir IN LISTS HAL_PROJECT_CONFIG_DIR)
            string(REGEX MATCH "^HAL_(ENABLE|DISABLE)_" _jh_is_hal_feature
                "${DEFINE_NAME}")
            if(_jh_is_hal_feature)
                jh_collect_project_feature_defines(
                    _jh_config_features "${_config_dir}")
                list(FIND _jh_config_features
                    "${DEFINE_NAME}" _jh_config_feature_index)
                if(NOT _jh_config_feature_index EQUAL -1)
                    set(_enabled TRUE)
                endif()
            else()
                set(_config_file "${_config_dir}/hal_project_config.h")
                if(EXISTS "${_config_file}")
                    file(STRINGS "${_config_file}" _config_hits
                        REGEX
                        "^[ \t]*#[ \t]*define[ \t]+${DEFINE_NAME}([ \t(/]|$)"
                    )
                    if(_config_hits)
                        set(_enabled TRUE)
                    endif()
                endif()
            endif()
        endforeach()
    endif()
    set(${OUT_VAR} ${_enabled} PARENT_SCOPE)
endfunction()

# Resolve a single-token define value from CMake, EXTRA_HAL_DEFINES or the
# active project configuration.
function(jh_hal_define_value OUT_VAR DEFINE_NAME)
    _jh_validate_rp_feature_inputs("${DEFINE_NAME}")
    jh_normalize_feature_defines(_jh_extra_defines ${EXTRA_HAL_DEFINES})
    jh_normalize_feature_defines(_jh_board_defines ${JH_RP_BOARD_DEFINES})
    set(_value "")
    if(DEFINED ${DEFINE_NAME})
        set(_value "${${DEFINE_NAME}}")
    endif()
    if("${_value}" STREQUAL "" AND DEFINED EXTRA_HAL_DEFINES)
        foreach(_def IN LISTS _jh_extra_defines)
            string(REGEX MATCH "^${DEFINE_NAME}=(.+)$"
                _jh_value_match "${_def}")
            if(_jh_value_match)
                set(_value "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    if("${_value}" STREQUAL "" AND DEFINED JH_RP_BOARD_DEFINES)
        foreach(_def IN LISTS _jh_board_defines)
            string(REGEX MATCH "^${DEFINE_NAME}=(.+)$"
                _jh_value_match "${_def}")
            if(_jh_value_match)
                set(_value "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    if("${_value}" STREQUAL "" AND DEFINED HAL_PROJECT_CONFIG_DIR)
        foreach(_config_dir IN LISTS HAL_PROJECT_CONFIG_DIR)
            set(_config_file "${_config_dir}/hal_project_config.h")
            if(EXISTS "${_config_file}")
                file(STRINGS "${_config_file}" _config_values
                    REGEX
                    "^[ \t]*#[ \t]*define[ \t]+${DEFINE_NAME}[ \t]+[^ \t/]+"
                )
                if(_config_values)
                    list(GET _config_values -1 _config_value)
                    string(REGEX REPLACE
                        "^[ \t]*#[ \t]*define[ \t]+${DEFINE_NAME}[ \t]+([^ \t/]+).*$"
                        "\\1" _value "${_config_value}")
                endif()
            endif()
        endforeach()
    endif()
    set(${OUT_VAR} "${_value}" PARENT_SCOPE)
endfunction()

# Keep all RP targets on one source inventory.
function(jh_collect_rp_hal_sources OUT_VAR SRC_DIR)
    cmake_parse_arguments(JH_RP_SOURCES "EXCLUDE_APP_ENTRY" "" "" ${ARGN})
    file(GLOB _rp_impl_sources CONFIGURE_DEPENDS
        "${SRC_DIR}/hal/impl/rp2040/*.cpp"
    )
    file(GLOB_RECURSE _common_sources CONFIGURE_DEPENDS
        "${SRC_DIR}/hal/*.cpp"
        "${SRC_DIR}/hal/*.c"
    )
    list(FILTER _common_sources EXCLUDE REGEX "/hal/impl/")
    list(FILTER _common_sources EXCLUDE REGEX "/network/mqtt/PubSubClient/")
    list(FILTER _common_sources EXCLUDE REGEX "/bluetooth/")
    list(APPEND _common_sources
        "${SRC_DIR}/hal/bluetooth/hal_ble.cpp"
        "${SRC_DIR}/hal/bluetooth/hal_ble_stream.cpp"
    )

    file(GLOB_RECURSE _driver_sources CONFIGURE_DEPENDS
        "${SRC_DIR}/hal/impl/rp2040/drivers/*.cpp"
        "${SRC_DIR}/hal/impl/rp2040/drivers/*.c"
    )

    set(_framework_sources)
    jh_hal_define_enabled(_enable_mqtt HAL_ENABLE_MQTT)
    if(_enable_mqtt)
        file(GLOB_RECURSE _mqtt_sources CONFIGURE_DEPENDS
            "${SRC_DIR}/hal/network/mqtt/PubSubClient/*.cpp"
            "${SRC_DIR}/hal/network/mqtt/PubSubClient/*.c"
        )
        list(APPEND _framework_sources ${_mqtt_sources})
    endif()

    if(NOT JH_RP_SOURCES_EXCLUDE_APP_ENTRY)
        list(APPEND _common_sources "${SRC_DIR}/hal_app_entry.cpp")
    endif()

    set(_utility_sources
        "${SRC_DIR}/utils/tools.cpp"
        "${SRC_DIR}/utils/multicoreWatchdog.cpp"
        "${SRC_DIR}/utils/draw7Segment.cpp"
        "${SRC_DIR}/utils/pidController.cpp"
    )
    jh_hal_define_enabled(_enable_unity HAL_ENABLE_UNITY)
    if(_enable_unity)
        list(APPEND _utility_sources "${SRC_DIR}/utils/unity.c")
    endif()

    set(${OUT_VAR}
        ${_rp_impl_sources}
        ${_driver_sources}
        ${_framework_sources}
        ${_common_sources}
        ${_utility_sources}
        PARENT_SCOPE
    )
endfunction()
