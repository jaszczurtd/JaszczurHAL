include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/jh_project_features.cmake")

function(jh_generate_board_config)
    set(options)
    set(one_value_args ROOT TARGET BOARD OUTPUT_DIR OUTPUT_ROOT)
    set(multi_value_args DEFINES REQUESTED_FEATURES RESOLVED_FEATURES)
    cmake_parse_arguments(JH_BOARD
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    foreach(_required ROOT TARGET OUTPUT_DIR)
        if(NOT JH_BOARD_${_required})
            message(FATAL_ERROR
                "jh_generate_board_config requires ${_required}")
        endif()
    endforeach()

    jh_normalize_feature_defines(JH_BOARD_DEFINES ${JH_BOARD_DEFINES})
    jh_split_feature_defines(
        _jh_board_define_features _jh_board_non_feature_defines
        ${JH_BOARD_DEFINES})
    if(JH_BOARD_RESOLVED_FEATURES AND NOT JH_BOARD_REQUESTED_FEATURES)
        message(FATAL_ERROR
            "jh_generate_board_config requires REQUESTED_FEATURES when "
            "RESOLVED_FEATURES is provided")
    endif()
    if(JH_BOARD_REQUESTED_FEATURES)
        jh_resolve_feature_defines(
            _jh_board_requested
            _jh_board_expected_resolved
            ${JH_BOARD_REQUESTED_FEATURES})
        if(JH_BOARD_RESOLVED_FEATURES)
            jh_split_feature_defines(
                _jh_board_resolved _jh_unused_non_features
                ${JH_BOARD_RESOLVED_FEATURES})
            if(NOT "${_jh_board_resolved}" STREQUAL
               "${_jh_board_expected_resolved}")
                message(FATAL_ERROR
                    "Board resolved feature set differs from the registry: "
                    "expected '${_jh_board_expected_resolved}', got "
                    "'${_jh_board_resolved}'")
            endif()
        else()
            set(_jh_board_resolved ${_jh_board_expected_resolved})
        endif()
    else()
        jh_resolve_feature_defines(
            _jh_board_requested _jh_board_resolved ${JH_BOARD_DEFINES})
    endif()
    foreach(_jh_board_define_feature IN LISTS _jh_board_define_features)
        list(FIND _jh_board_requested "${_jh_board_define_feature}"
            _jh_board_requested_index)
        if(_jh_board_requested_index EQUAL -1)
            message(FATAL_ERROR
                "[JH-CFG-PARITY] board compile definition "
                "'${_jh_board_define_feature}' is absent from "
                "REQUESTED_FEATURES")
        endif()
    endforeach()

    if(Python3_EXECUTABLE)
        if(NOT EXISTS "${Python3_EXECUTABLE}")
            message(FATAL_ERROR
                "Python3_EXECUTABLE does not exist: ${Python3_EXECUTABLE}")
        endif()
        set(_jh_board_python "${Python3_EXECUTABLE}")
    else()
        find_program(_jh_board_python NAMES python3 python REQUIRED)
    endif()
    if(NOT JH_BOARD_BOARD)
        execute_process(
            COMMAND
                "${_jh_board_python}"
                "${JH_BOARD_ROOT}/scripts/generate_board_config.py"
                --boards-root "${JH_BOARD_ROOT}/boards"
                --target "${JH_BOARD_TARGET}"
                --default-board
            RESULT_VARIABLE _jh_default_board_result
            OUTPUT_VARIABLE _jh_default_board
            ERROR_VARIABLE _jh_default_board_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT _jh_default_board_result EQUAL 0)
            string(STRIP "${_jh_default_board_error}" _jh_default_board_error)
            message(FATAL_ERROR
                "Default board resolution failed: ${_jh_default_board_error}")
        endif()
        set(JH_BOARD_BOARD "${_jh_default_board}")
        set(JH_BOARD_OUTPUT_DIR
            "${JH_BOARD_OUTPUT_DIR}/${JH_BOARD_BOARD}")
    endif()
    set(_command
        "${_jh_board_python}"
        "${JH_BOARD_ROOT}/scripts/generate_board_config.py"
        --boards-root "${JH_BOARD_ROOT}/boards"
        --target "${JH_BOARD_TARGET}"
        --output-dir "${JH_BOARD_OUTPUT_DIR}")
    if(JH_BOARD_OUTPUT_ROOT)
        list(APPEND _command --output-root "${JH_BOARD_OUTPUT_ROOT}")
    endif()
    list(APPEND _command --board "${JH_BOARD_BOARD}")
    foreach(_define IN LISTS JH_BOARD_DEFINES)
        list(APPEND _command --define "${_define}")
    endforeach()
    foreach(_feature IN LISTS _jh_board_requested)
        list(APPEND _command --requested-feature "${_feature}")
    endforeach()

    execute_process(
        COMMAND ${_command}
        RESULT_VARIABLE _jh_board_result
        OUTPUT_VARIABLE _jh_board_stdout
        ERROR_VARIABLE _jh_board_stderr
    )
    if(NOT _jh_board_result EQUAL 0)
        string(STRIP "${_jh_board_stderr}" _jh_board_error)
        message(FATAL_ERROR "Board profile generation failed: ${_jh_board_error}")
    endif()

    include("${JH_BOARD_OUTPUT_DIR}/jh_board_config.cmake")
    if(NOT "${JH_BOARD_REQUESTED_FEATURES}" STREQUAL
       "${_jh_board_requested}" OR
       NOT "${JH_BOARD_RESOLVED_FEATURES}" STREQUAL
       "${_jh_board_resolved}")
        message(FATAL_ERROR
            "Board generator feature resolution differs from CMake: "
            "requested '${JH_BOARD_REQUESTED_FEATURES}' vs "
            "'${_jh_board_requested}', resolved "
            "'${JH_BOARD_RESOLVED_FEATURES}' vs '${_jh_board_resolved}'")
    endif()
    file(GLOB _jh_board_descriptors CONFIGURE_DEPENDS
        "${JH_BOARD_ROOT}/boards/targets/*.json"
        "${JH_BOARD_ROOT}/boards/profiles/*.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${JH_BOARD_ROOT}/scripts/generate_board_config.py"
        "${JH_BOARD_ROOT}/scripts/generate_hal_features.py"
        "${JH_BOARD_ROOT}/config/features.schema.json"
        "${JH_BOARD_ROOT}/boards/capabilities.json"
        ${_jh_board_descriptors})
    file(GLOB _jh_feature_descriptors CONFIGURE_DEPENDS
        "${JH_BOARD_ROOT}/config/features/*.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${_jh_feature_descriptors})

    foreach(_variable
        JH_RESOLVED_TARGET
        JH_RESOLVED_BOARD
        JH_BOARD_PROVIDER
        JH_BOARD_RECIPE
        JH_BOARD_COMPONENTS
        JH_BOARD_COMPILE_DEFINITIONS
        JH_BOARD_EXPECTED_FLASH_BYTES
        JH_BOARD_REQUESTED_FEATURES
        JH_BOARD_RESOLVED_FEATURES
        JH_BOARD_RESOLVED_FEATURES_DIGEST
        JH_BOARD_FEATURE_HASH
        JH_BOARD_CONTRACT_SYMBOL
        PICO_PLATFORM
        PICO_BOARD)
        if(DEFINED ${_variable})
            set(${_variable} "${${_variable}}" PARENT_SCOPE)
        endif()
    endforeach()
    set(JH_BOARD_GENERATED_DIR "${JH_BOARD_OUTPUT_DIR}" PARENT_SCOPE)
endfunction()
