cmake_minimum_required(VERSION 3.22)
# The tracked clock tree registry: jh clock check passes on this checkout (the
# trees follow from their parameters, every board runs the default tree of
# each of its targets, the STM32G474 startup headers, the pinned Pico SDK and
# the HAL build files agree), the STM32G474 startup code builds exactly two
# trees, and the STM32G474 oscillator pins follow RM0440. The checks
# themselves are tested by the jh_clocks test of tools/jh.
if(NOT DEFINED JH_ROOT OR NOT DEFINED JH_TEST_BINARY_DIR)
    message(FATAL_ERROR "JH_ROOT and JH_TEST_BINARY_DIR are required")
endif()

set(_scratch "${JH_TEST_BINARY_DIR}/clock_registry")
file(REMOVE_RECURSE "${_scratch}")
file(MAKE_DIRECTORY "${_scratch}")
include("${JH_ROOT}/cmake/jh_project_config.cmake")
jh_project_config_tool(_jh "${JH_ROOT}" "${_scratch}")

function(run_jh variable)
    execute_process(COMMAND "${_jh}" ${ARGN}
        RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "jh ${ARGN} failed (${_code}):\n${_out}\n${_err}")
    endif()
    set(${variable} "${_out}" PARENT_SCOPE)
endfunction()

run_jh(_check clock check --root "${JH_ROOT}")
if(NOT _check MATCHES "^\\[OK\\] clock registry: [0-9]+ targets, [0-9]+ trees\n$")
    message(FATAL_ERROR "Unexpected clock check result: ${_check}")
endif()

run_jh(_list clock list --root "${JH_ROOT}" --json)
string(JSON _count LENGTH "${_list}" targets)
math(EXPR _last "${_count} - 1")
set(_stm32_trees "")
foreach(_index RANGE ${_last})
    string(JSON _id GET "${_list}" targets ${_index} id)
    if(_id STREQUAL "stm32g474")
        string(JSON _trees GET "${_list}" targets ${_index} registry trees)
        string(JSON _tree_count LENGTH "${_trees}")
        math(EXPR _tree_last "${_tree_count} - 1")
        foreach(_tree RANGE ${_tree_last})
            string(JSON _name MEMBER "${_trees}" ${_tree})
            list(APPEND _stm32_trees "${_name}")
        endforeach()
    endif()
endforeach()
list(SORT _stm32_trees)
if(NOT _stm32_trees STREQUAL "default;hse-160mhz")
    message(FATAL_ERROR "STM32G474 trees changed: ${_stm32_trees}")
endif()

# RM0440 7.2.1/7.2.4: a crystal takes OSC_IN and OSC_OUT, bypass only OSC_IN.
file(READ "${JH_ROOT}/boards/targets/stm32g474.json" _target)
string(JSON _inputs GET "${_target}" clockInputs)
set(_expected [=[{"hse": {"crystal": ["PF0", "PF1"], "external-clock": ["PF0"]}, "lse": {"crystal": ["PC14", "PC15"], "external-clock": ["PC14"]}}]=])
string(JSON _same EQUAL "${_inputs}" "${_expected}")
if(NOT _same)
    message(FATAL_ERROR "STM32G474 clock inputs differ from RM0440: ${_inputs}")
endif()
file(REMOVE_RECURSE "${_scratch}")
