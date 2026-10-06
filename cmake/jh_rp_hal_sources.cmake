include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/jh_project_features.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/jh_project_config.cmake")

# Report whether a macro is enabled for the RP build: a HAL feature when the
# registry-resolved closure contains it, any other macro when the project
# configuration or the board's compile definitions define it.
function(jh_hal_define_enabled OUT_VAR DEFINE_NAME)
    if(DEFINE_NAME MATCHES "^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+$")
        if(NOT JH_PROJECT_CONFIG_LOADED)
            message(FATAL_ERROR
                "jh_hal_define_enabled(${DEFINE_NAME}) needs the project "
                "configuration and its resolved features")
        endif()
        list(FIND JH_RESOLVED_FEATURES "${DEFINE_NAME}" _jh_resolved_index)
        if(_jh_resolved_index EQUAL -1)
            set(${OUT_VAR} FALSE PARENT_SCOPE)
        else()
            set(${OUT_VAR} TRUE PARENT_SCOPE)
        endif()
        return()
    endif()
    jh_build_defined(_enabled "${DEFINE_NAME}" ${JH_RP_BOARD_DEFINES})
    set(${OUT_VAR} ${_enabled} PARENT_SCOPE)
endfunction()

# Value of a macro for the RP build; see jh_build_define_value().
function(jh_hal_define_value OUT_VAR DEFINE_NAME)
    jh_build_define_value(_value "${DEFINE_NAME}" ${JH_RP_BOARD_DEFINES})
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
        "${SRC_DIR}/hal/bluetooth/hal_ble_commands.cpp"
        "${SRC_DIR}/hal/bluetooth/hal_ble_stream.cpp"
    )

    file(GLOB_RECURSE _driver_sources CONFIGURE_DEPENDS
        "${SRC_DIR}/hal/impl/rp2040/drivers/*.cpp"
        "${SRC_DIR}/hal/impl/rp2040/drivers/*.c"
        "${SRC_DIR}/hal/impl/rp2040/drivers/*.S"
    )

    if(NOT JH_RP_SOURCES_EXCLUDE_APP_ENTRY)
        list(APPEND _common_sources "${SRC_DIR}/hal_app_entry.cpp")
    endif()

    set(_utility_sources
        "${SRC_DIR}/utils/multicoreWatchdog.cpp"
        "${SRC_DIR}/utils/draw7Segment.cpp"
        "${SRC_DIR}/utils/pidController.cpp"
    )
    set(${OUT_VAR}
        ${_rp_impl_sources}
        ${_driver_sources}
        ${_common_sources}
        ${_utility_sources}
        PARENT_SCOPE
    )
endfunction()
