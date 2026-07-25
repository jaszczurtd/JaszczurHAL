include_guard(GLOBAL)

function(jh_generate_board_config)
    set(options)
    set(one_value_args ROOT TARGET BOARD OUTPUT_DIR OUTPUT_ROOT)
    set(multi_value_args DEFINES)
    cmake_parse_arguments(JH_BOARD
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    foreach(_required ROOT TARGET OUTPUT_DIR)
        if(NOT JH_BOARD_${_required})
            message(FATAL_ERROR
                "jh_generate_board_config requires ${_required}")
        endif()
    endforeach()

    find_program(_jh_board_python NAMES python3 python REQUIRED)
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
        if(_define MATCHES "^HAL_ENABLE_[A-Z0-9_]+(=(0|1))?$")
            list(APPEND _command --feature "${_define}")
        endif()
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
    file(GLOB _jh_board_descriptors CONFIGURE_DEPENDS
        "${JH_BOARD_ROOT}/boards/targets/*.json"
        "${JH_BOARD_ROOT}/boards/profiles/*.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${JH_BOARD_ROOT}/scripts/generate_board_config.py"
        "${JH_BOARD_ROOT}/boards/capabilities.json"
        ${_jh_board_descriptors})

    foreach(_variable
        JH_RESOLVED_TARGET
        JH_RESOLVED_BOARD
        JH_BOARD_PROVIDER
        JH_BOARD_RECIPE
        JH_BOARD_COMPONENTS
        JH_BOARD_COMPILE_DEFINITIONS
        JH_BOARD_EXPECTED_FLASH_BYTES
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
