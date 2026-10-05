# A build must reconfigure, and reject the registry again, whenever a file that
# the board generator reads changes or a new descriptor appears.
if(NOT DEFINED JH_ROOT OR NOT DEFINED JH_TEST_BINARY_DIR)
    message(FATAL_ERROR "JH_ROOT and JH_TEST_BINARY_DIR are required")
endif()

set(_fixture "${JH_TEST_BINARY_DIR}/board_config_dependencies")
set(_source "${_fixture}/source")
set(_build "${_source}/.build/probe")
file(REMOVE_RECURSE "${_fixture}")
file(MAKE_DIRECTORY "${_source}")
file(COPY "${JH_ROOT}/boards" "${JH_ROOT}/config" DESTINATION "${_source}")
file(COPY "${JH_ROOT}/scripts/" DESTINATION "${_source}/scripts"
    FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
file(WRITE "${_source}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.16)\n"
    "project(board_config_dependencies NONE)\n"
    "include(\"${JH_ROOT}/cmake/jh_board_profiles.cmake\")\n"
    "jh_generate_board_config(ROOT \"\${CMAKE_CURRENT_SOURCE_DIR}\"\n"
    "    TARGET rp2040 BOARD pico\n"
    "    OUTPUT_DIR \"\${CMAKE_BINARY_DIR}/generated\")\n"
    "add_custom_target(probe ALL COMMAND \"\${CMAKE_COMMAND}\" -E true)\n")

function(run_step NAME EXPECT_SUCCESS EXPECT_TEXT)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    # CMake wraps long diagnostics, so compare with whitespace collapsed.
    string(REGEX REPLACE "[ \t\r\n]+" " " _output "${_stdout} ${_stderr}")
    if(EXPECT_SUCCESS AND NOT _result EQUAL 0)
        message(FATAL_ERROR "${NAME} failed:\n${_output}")
    endif()
    if(NOT EXPECT_SUCCESS)
        if(_result EQUAL 0)
            message(FATAL_ERROR "${NAME} built with an invalid board input")
        endif()
        string(FIND "${_output}" "${EXPECT_TEXT}" _match)
        if(_match EQUAL -1)
            message(FATAL_ERROR "${NAME} did not report '${EXPECT_TEXT}':\n${_output}")
        endif()
    endif()
endfunction()

run_step(configure TRUE "" "${CMAKE_COMMAND}" -S "${_source}" -B "${_build}")
run_step(build TRUE "" "${CMAKE_COMMAND}" --build "${_build}")

# Each case breaks one generator input, expects the next incremental build to
# fail with the generator's diagnostic, then restores it and builds again.
function(check_input NAME FILE OLD NEW EXPECT_TEXT)
    set(_path "${_source}/${FILE}")
    if(EXISTS "${_path}")
        file(READ "${_path}" _original)
        string(REPLACE "${OLD}" "${NEW}" _broken "${_original}")
        if(_broken STREQUAL _original)
            message(FATAL_ERROR "${NAME}: '${OLD}' not found in ${FILE}")
        endif()
    else()
        set(_original "")
        set(_broken "${NEW}")
    endif()
    file(WRITE "${_path}" "${_broken}")
    run_step("${NAME}" FALSE "${EXPECT_TEXT}" "${CMAKE_COMMAND}" --build "${_build}")
    if(_original STREQUAL "")
        file(REMOVE "${_path}")
    else()
        file(WRITE "${_path}" "${_original}")
    endif()
    run_step("${NAME}-restored" TRUE "" "${CMAKE_COMMAND}" --build "${_build}")
endfunction()

check_input(profile-registry boards/profiles/pico.json
    "\"profileId\": 1," "\"profileId\": 2,"
    "a unique value; first used by")
check_input(component-registry config/tooling/board_components.json
    "\"id\": \"sx126x-radio\"" "\"id\": \"lora-radio\""
    "expected a known component ID")
check_input(generator-module scripts/codegen_support.py
    "def validation_error(" "raise SystemExit('generator module changed')\ndef validation_error("
    "generator module changed")
check_input(new-descriptor boards/profiles/zz-new.json
    unused "{\"id\": \"zz-new\"}"
    "zz-new")
