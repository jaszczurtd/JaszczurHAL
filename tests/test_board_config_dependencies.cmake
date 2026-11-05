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
file(COPY "${JH_ROOT}/scripts/jh_tool_inputs.cmake" DESTINATION "${_source}/scripts")
# The generator reads the feature registry through jh. The copy has its
# scripts but no tools/, so it uses a copy of the jh of JH_ROOT, which a case
# below replaces.
include("${JH_ROOT}/cmake/jh_project_config.cmake")
jh_project_config_tool(_jh_tool "${JH_ROOT}" "${_fixture}")
# On POSIX a backslash is a file name character: the copy sits in such a
# directory, which the build depends on under that very name.
# file(MAKE_DIRECTORY) would take the backslash for a separator.
get_filename_component(_jh_tool_name "${_jh_tool}" NAME)
if(CMAKE_HOST_WIN32)
    set(_jh_tool_dir "${_fixture}/tool")
    file(MAKE_DIRECTORY "${_jh_tool_dir}")
else()
    set(_jh_tool_dir "${_fixture}/tool\\dir")
    execute_process(COMMAND mkdir -p "${_jh_tool_dir}" COMMAND_ERROR_IS_FATAL ANY)
endif()
set(_jh_tool_copy "${_jh_tool_dir}/${_jh_tool_name}")
file(COPY_FILE "${_jh_tool}" "${_jh_tool_copy}")
set(ENV{JH_EXECUTABLE} "${_jh_tool_copy}")
set(ENV{JH_EXECUTABLE_ROOT} "${_source}")
file(WRITE "${_source}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.22)\n"
    "project(board_config_dependencies NONE)\n"
    # CMake records the file it reads under the normalized path, the jh inputs
    # reach it through the root below.
    "include(\"\${CMAKE_CURRENT_SOURCE_DIR}/scripts/jh_tool_inputs.cmake\")\n"
    "include(\"${JH_ROOT}/cmake/jh_board_profiles.cmake\")\n"
    # A root spelt with .., as the static libraries pass theirs.
    "jh_generate_board_config(ROOT \"\${CMAKE_CURRENT_SOURCE_DIR}/scripts/..\"\n"
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

# Ninja, where available, rejects a file the build depends on under two
# spellings.
find_program(_ninja ninja)
set(_generator "")
if(_ninja)
    set(_generator -G Ninja)
endif()
run_step(configure TRUE "" "${CMAKE_COMMAND}" -S "${_source}" -B "${_build}" ${_generator})
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

# The generator runs with jh: another executable, or one rebuilt in place,
# makes the next build generate again. A tool without a JSON result fails it,
# the real one builds again.
file(COPY_FILE "${CMAKE_COMMAND}" "${_jh_tool_copy}")
file(TOUCH "${_jh_tool_copy}")
run_step(tool-replaced FALSE "gave no result" "${CMAKE_COMMAND}" --build "${_build}")
file(COPY_FILE "${_jh_tool}" "${_jh_tool_copy}")
file(TOUCH "${_jh_tool_copy}")
run_step(tool-restored TRUE "" "${CMAKE_COMMAND}" --build "${_build}")

# A new file among the jh sources needs another jh, which configure verifies:
# the next build configures again.
file(WRITE "${_source}/tools/jh/new-input.c" "\n")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_build}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
if(NOT _result EQUAL 0 OR NOT "${_stdout}${_stderr}" MATCHES "Configuring done")
    message(FATAL_ERROR "a new jh source did not reconfigure the build:\n${_stdout}${_stderr}")
endif()
