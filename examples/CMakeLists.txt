cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_Examples_Dispatcher NONE)

# Compatibility wrapper for the checked-in examples.
#
# The example projects themselves now live as dispatcher-backed VS Code firmware
# projects under examples/*/.vscode/jaszczurhal.project.json. This CMake entry
# remains only so older muscle-memory commands such as:
#
#   cmake -S examples -B .build/examples-cmake/rp2040
#       -DJH_EXAMPLE_TARGET=rp2040
#   cmake --build .build/examples-cmake/rp2040
#
# still exercise the same dispatcher path.

set(JH_EXAMPLE_TARGET "rp2040" CACHE STRING
    "Example backend: rp2040, rp2350-arm, rp2350-riscv or stm32g474")
set_property(CACHE JH_EXAMPLE_TARGET PROPERTY STRINGS
    rp2040 rp2350-arm rp2350-riscv stm32g474)
set(JH_EXAMPLE_JOBS "1" CACHE STRING "Parallel example project builds used by the dispatcher runner")

if(DEFINED JH_STM32_FREERTOS OR DEFINED EXTRA_HAL_DEFINES)
    message(WARNING
        "The legacy examples CMake switches JH_STM32_FREERTOS and "
        "EXTRA_HAL_DEFINES are retired. "
        "Per-example flags now live in examples/*/.vscode/jaszczurhal.project.json.")
endif()

get_filename_component(JH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(_jh_examples_runner "${JH_ROOT}/scripts/examples_dispatcher.py")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(examples ALL
    COMMAND "${Python3_EXECUTABLE}" "${_jh_examples_runner}"
            build --target "${JH_EXAMPLE_TARGET}" --jobs "${JH_EXAMPLE_JOBS}"
    WORKING_DIRECTORY "${JH_ROOT}"
    USES_TERMINAL
    VERBATIM
)

file(GLOB _jh_example_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/[0-9][0-9]_*")
foreach(_jh_example_dir IN LISTS _jh_example_dirs)
    if(NOT IS_DIRECTORY "${_jh_example_dir}")
        continue()
    endif()
    get_filename_component(_jh_example_name "${_jh_example_dir}" NAME)
    add_custom_target("${_jh_example_name}_${JH_EXAMPLE_TARGET}"
        COMMAND "${Python3_EXECUTABLE}" "${_jh_examples_runner}"
                build --target "${JH_EXAMPLE_TARGET}" --example "${_jh_example_name}" --jobs 1
        WORKING_DIRECTORY "${JH_ROOT}"
        USES_TERMINAL
        VERBATIM
    )
endforeach()
