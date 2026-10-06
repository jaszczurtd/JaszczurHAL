include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/generated/jh_hal_features.cmake")

# Return the broadest valid feature profile for a production architecture.
# The generated registry remains the source of truth, so newly registered
# features automatically enter this CI-oriented profile unless the target
# cannot support them or the registry declares an exclusive provider choice.
function(jh_all_features_for_target OUT_VAR TARGET_NAME)
    if(NOT TARGET_NAME MATCHES
       "^(rp2040|rp2350-arm|rp2350-riscv|stm32g474)$")
        message(FATAL_ERROR
            "JH_ENABLE_ALL_FEATURES does not support target '${TARGET_NAME}'")
    endif()

    set(_jh_all_features "")
    foreach(_jh_symbol IN LISTS JH_HAL_FEATURE_SYMBOLS)
        if(NOT _jh_symbol MATCHES "^HAL_ENABLE_")
            continue()
        endif()
        list(FIND JH_HAL_FEATURE_DERIVED_SYMBOLS
            "${_jh_symbol}" _jh_derived_index)
        if(_jh_derived_index EQUAL -1)
            list(APPEND _jh_all_features "${_jh_symbol}")
        endif()
    endforeach()

    # SX126X and SX127X are intentionally exclusive. Keep SX127X as the
    # all-features provider so the provider currently developed in-tree stays
    # covered without adding another architecture build.
    list(REMOVE_ITEM _jh_all_features HAL_ENABLE_SX126X)

    if(NOT TARGET_NAME STREQUAL "stm32g474")
        list(REMOVE_ITEM _jh_all_features HAL_ENABLE_STM32G474_FDCAN)
    else()
        list(REMOVE_ITEM _jh_all_features
            HAL_ENABLE_BLUETOOTH_A2DP_SINK
            HAL_ENABLE_BLUETOOTH_AVRCP_TARGET)
    endif()
    if(TARGET_NAME STREQUAL "rp2350-riscv")
        # BTstack and the CYW43 BLE backend are not enabled for Hazard3.
        list(REMOVE_ITEM _jh_all_features
            HAL_ENABLE_BLE
            HAL_ENABLE_BLE_COMMANDS
            HAL_ENABLE_BLE_STREAM
            HAL_ENABLE_BLUETOOTH_CLASSIC
            HAL_ENABLE_BLUETOOTH_A2DP_SINK
            HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
            HAL_ENABLE_BLUETOOTH_HID_HOST
            HAL_ENABLE_BLUETOOTH_GAMEPAD)
    endif()
    if(TARGET_NAME STREQUAL "rp2350-riscv")
        # Native OTA is not implemented for the Hazard3 RISC-V port.
        list(REMOVE_ITEM _jh_all_features HAL_ENABLE_OTA)
    endif()

    # A full network feature set needs an explicit backend. The RP RISC-V
    # build intentionally uses the plain pico2 profile and therefore exercises
    # the same no-physical-radio compile path as a consumer-provided CYW43
    # frontend. Boards with an owned CYW43 component produce the same defines.
    if(TARGET_NAME STREQUAL "stm32g474")
        list(APPEND _jh_all_features
            HAL_NETWORK_BACKEND_CYW43
            HAL_CYW43_BUS_STM32_GSPI
            HAL_CYW43_STACK_LWIP
            HAL_CYW43_PIN_WL_ON=30u
            HAL_CYW43_PIN_CHIP_SELECT=28u
            HAL_CYW43_PIN_DATA=31u
            HAL_CYW43_PIN_CLOCK=29u
            HAL_CYW43_MAX_TRANSACTION_BYTES=2048u)
    else()
        list(APPEND _jh_all_features
            HAL_NETWORK_BACKEND_CYW43
            HAL_CYW43_BUS_PICO_PIO
            HAL_CYW43_STACK_LWIP
            HAL_CYW43_MAX_TRANSACTION_BYTES=2048u)
    endif()

    # HAL_ENABLE_TFT needs one concrete facade selection even though all TFT
    # driver implementations are enabled by the feature registry.
    list(APPEND _jh_all_features HAL_DISPLAY_ILI9341)
    list(REMOVE_DUPLICATES _jh_all_features)
    list(SORT _jh_all_features)
    set(${OUT_VAR} "${_jh_all_features}" PARENT_SCOPE)
endfunction()

function(jh_normalize_feature_defines OUT_VAR)
    set(_jh_result "")
    foreach(_jh_definition IN LISTS ARGN)
        string(STRIP "${_jh_definition}" _jh_definition)
        if(_jh_definition MATCHES "\\$<")
            message(FATAL_ERROR
                "[JH-CFG-VALUE] compile definition '${_jh_definition}': "
                "generator expressions are not supported in HAL definition "
                "inputs")
        endif()
        string(REGEX REPLACE "^-D" "" _jh_normalized "${_jh_definition}")
        if(NOT _jh_normalized MATCHES "^HAL_(ENABLE|DISABLE)_")
            if(_jh_normalized MATCHES "HAL_(ENABLE|DISABLE)_")
                message(FATAL_ERROR
                    "[JH-CFG-VALUE] compile definition "
                    "'${_jh_definition}': HAL feature flags must be standalone "
                    "bare macro or have an explicit value of 1")
            endif()
            list(APPEND _jh_result "${_jh_definition}")
            continue()
        endif()
        if(_jh_normalized MATCHES "^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+$" OR
           _jh_normalized MATCHES "^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+=1$")
            list(APPEND _jh_result "${_jh_normalized}")
            continue()
        endif()
        message(FATAL_ERROR
            "[JH-CFG-VALUE] compile definition '${_jh_definition}': "
            "HAL feature flags accept only a bare macro or an explicit value of 1")
    endforeach()
    set(${OUT_VAR} ${_jh_result} PARENT_SCOPE)
endfunction()

function(jh_validate_feature_defines)
    jh_normalize_feature_defines(_jh_unused ${ARGN})
endfunction()

function(jh_validate_cmake_feature_variable FEATURE_NAME)
    string(REGEX MATCH "^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+$"
        _jh_valid_feature_name "${FEATURE_NAME}")
    if(NOT _jh_valid_feature_name)
        message(FATAL_ERROR
            "[JH-CFG-VALUE] invalid direct CMake feature variable "
            "'${FEATURE_NAME}'")
    endif()
    if(NOT DEFINED ${FEATURE_NAME})
        return()
    endif()
    set(_jh_value "${${FEATURE_NAME}}")
    string(COMPARE EQUAL "${_jh_value}" "1" _jh_value_is_one)
    if(NOT _jh_value_is_one)
        message(FATAL_ERROR
            "[JH-CFG-VALUE] direct CMake feature "
            "'${FEATURE_NAME}=${_jh_value}': HAL feature cache variables "
            "accept only the explicit value 1")
    endif()
endfunction()

function(jh_collect_cmake_feature_defines OUT_VAR)
    get_cmake_property(_jh_variable_names VARIABLES)
    set(_jh_features "")
    foreach(_jh_name IN LISTS _jh_variable_names)
        if(NOT _jh_name MATCHES "^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+$")
            continue()
        endif()
        jh_validate_cmake_feature_variable("${_jh_name}")
        list(APPEND _jh_features "${_jh_name}=1")
    endforeach()
    list(REMOVE_DUPLICATES _jh_features)
    list(SORT _jh_features)
    set(${OUT_VAR} ${_jh_features} PARENT_SCOPE)
endfunction()

# Split supported HAL feature flags from arbitrary compile definitions. Feature
# outputs are bare registry symbols; non-feature outputs retain their normalized
# compile-definition spelling.
function(jh_split_feature_defines FEATURE_OUT NON_FEATURE_OUT)
    jh_normalize_feature_defines(_jh_definitions ${ARGN})
    set(_jh_features "")
    set(_jh_non_features "")
    foreach(_jh_definition IN LISTS _jh_definitions)
        string(REGEX REPLACE "^-D" "" _jh_normalized "${_jh_definition}")
        if(_jh_normalized MATCHES
           "^(HAL_(ENABLE|DISABLE)_[A-Z0-9_]+)(=1)?$")
            list(APPEND _jh_features "${CMAKE_MATCH_1}")
        else()
            list(APPEND _jh_non_features "${_jh_definition}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _jh_features)
    list(SORT _jh_features)
    set(${FEATURE_OUT} "${_jh_features}" PARENT_SCOPE)
    set(${NON_FEATURE_OUT} "${_jh_non_features}" PARENT_SCOPE)
endfunction()

# Resolve the registry graph once at configure time. The requested set remains
# the only compiler input; resolved features are for source/link selection and
# the board contract.
function(jh_resolve_feature_defines REQUESTED_OUT RESOLVED_OUT)
    jh_split_feature_defines(_jh_features _jh_unused_non_features ${ARGN})
    jh_hal_resolve_features(_jh_requested _jh_resolved ${_jh_features})
    set(${REQUESTED_OUT} "${_jh_requested}" PARENT_SCOPE)
    set(${RESOLVED_OUT} "${_jh_resolved}" PARENT_SCOPE)
endfunction()
