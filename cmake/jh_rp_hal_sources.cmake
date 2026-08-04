include_guard(GLOBAL)

# Report whether a HAL feature define is enabled directly, through the
# EXTRA_HAL_DEFINES cache list, or through a project configuration header.
function(jh_hal_define_enabled OUT_VAR DEFINE_NAME)
    set(_enabled FALSE)
    if(DEFINED ${DEFINE_NAME})
        set(_enabled TRUE)
    endif()
    if(DEFINED EXTRA_HAL_DEFINES)
        foreach(_def IN LISTS EXTRA_HAL_DEFINES)
            if("${_def}" STREQUAL "${DEFINE_NAME}" OR
               "${_def}" MATCHES "^${DEFINE_NAME}=")
                set(_enabled TRUE)
            endif()
        endforeach()
    endif()
    if(DEFINED JH_RP_BOARD_DEFINES)
        foreach(_def IN LISTS JH_RP_BOARD_DEFINES)
            if("${_def}" STREQUAL "${DEFINE_NAME}" OR
               "${_def}" MATCHES "^${DEFINE_NAME}=")
                set(_enabled TRUE)
            endif()
        endforeach()
    endif()
    if(DEFINED HAL_PROJECT_CONFIG_DIR)
        foreach(_config_dir IN LISTS HAL_PROJECT_CONFIG_DIR)
            set(_config_file "${_config_dir}/hal_project_config.h")
            if(EXISTS "${_config_file}")
                file(STRINGS "${_config_file}" _config_hits
                    REGEX
                    "^[ \t]*#[ \t]*define[ \t]+${DEFINE_NAME}([ \t(]|$)"
                )
                if(_config_hits)
                    set(_enabled TRUE)
                endif()
            endif()
        endforeach()
    endif()
    set(${OUT_VAR} ${_enabled} PARENT_SCOPE)
endfunction()

# Resolve a single-token define value from CMake, EXTRA_HAL_DEFINES or the
# active project configuration.
function(jh_hal_define_value OUT_VAR DEFINE_NAME)
    set(_value "")
    if(DEFINED ${DEFINE_NAME})
        set(_value "${${DEFINE_NAME}}")
    endif()
    if("${_value}" STREQUAL "" AND DEFINED EXTRA_HAL_DEFINES)
        foreach(_def IN LISTS EXTRA_HAL_DEFINES)
            if("${_def}" MATCHES "^${DEFINE_NAME}=(.+)$")
                set(_value "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    if("${_value}" STREQUAL "" AND DEFINED JH_RP_BOARD_DEFINES)
        foreach(_def IN LISTS JH_RP_BOARD_DEFINES)
            if("${_def}" MATCHES "^${DEFINE_NAME}=(.+)$")
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
    file(GLOB_RECURSE _rp_shared_sources CONFIGURE_DEPENDS
        "${SRC_DIR}/hal/impl/shared/*.cpp"
        "${SRC_DIR}/hal/impl/shared/*.c"
    )
    list(FILTER _rp_shared_sources EXCLUDE REGEX "/frameworks/PubSubClient/")
    list(FILTER _rp_shared_sources EXCLUDE REGEX "/bluetooth/")

    file(GLOB_RECURSE _driver_sources CONFIGURE_DEPENDS
        "${SRC_DIR}/hal/impl/rp2040/drivers/*.cpp"
        "${SRC_DIR}/hal/impl/rp2040/drivers/*.c"
    )

    set(_framework_sources)
    jh_hal_define_enabled(_enable_mqtt HAL_ENABLE_MQTT)
    if(_enable_mqtt)
        file(GLOB_RECURSE _mqtt_sources CONFIGURE_DEPENDS
            "${SRC_DIR}/hal/impl/shared/frameworks/PubSubClient/*.cpp"
            "${SRC_DIR}/hal/impl/shared/frameworks/PubSubClient/*.c"
        )
        list(APPEND _framework_sources ${_mqtt_sources})
    endif()

    file(GLOB _common_sources CONFIGURE_DEPENDS
        "${SRC_DIR}/hal/*.cpp"
    )
    if(NOT JH_RP_SOURCES_EXCLUDE_APP_ENTRY)
        list(APPEND _common_sources "${SRC_DIR}/hal_app_entry.cpp")
    endif()

    set(_utility_sources
        "${SRC_DIR}/utils/tools.cpp"
        "${SRC_DIR}/utils/multicoreWatchdog.cpp"
        "${SRC_DIR}/utils/draw7Segment.cpp"
        "${SRC_DIR}/utils/pidController.cpp"
    )
    if(HAL_ENABLE_UNITY)
        list(APPEND _utility_sources "${SRC_DIR}/utils/unity.c")
    endif()

    set(${OUT_VAR}
        ${_rp_impl_sources}
        ${_rp_shared_sources}
        ${_driver_sources}
        ${_framework_sources}
        ${_common_sources}
        ${_utility_sources}
        PARENT_SCOPE
    )
endfunction()
