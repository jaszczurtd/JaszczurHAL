if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

include("${JH_ROOT}/cmake/jh_build_dir_guard.cmake")

function(expect_managed binary expected)
    jh_build_dir_is_managed("${JH_ROOT}" "${binary}" _managed)
    if(NOT _managed STREQUAL expected)
        message(FATAL_ERROR
            "build-dir guard: ${binary} gave ${_managed}, expected ${expected}")
    endif()
endfunction()

expect_managed("${JH_ROOT}/.build/host" TRUE)
expect_managed("${JH_ROOT}/.build" TRUE)
expect_managed("${JH_ROOT}/tests/fixtures/esp32s3_phase3/.build/esp32s3" TRUE)
expect_managed("${JH_ROOT}/../consumer/.build/fw" TRUE)
expect_managed("${JH_ROOT}/../consumer/build" TRUE)
expect_managed("${JH_ROOT}" FALSE)
expect_managed("${JH_ROOT}/build" FALSE)
expect_managed("${JH_ROOT}/build_test" FALSE)
expect_managed("${JH_ROOT}/cmake-build-debug" FALSE)
expect_managed("${JH_ROOT}/examples/01_core_runtime/build" FALSE)
expect_managed("${JH_ROOT}/.buildx/host" FALSE)

# A real configure into a stray directory must stop with the guard message.
set(_probe "${JH_ROOT}/jh-build-guard-probe")
file(REMOVE_RECURSE "${_probe}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${JH_ROOT}/examples" -B "${_probe}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
file(REMOVE_RECURSE "${_probe}")
if(_result EQUAL 0 OR NOT _error MATCHES "\\[JH-BUILD-DIR\\]")
    message(FATAL_ERROR
        "configure into a stray directory was not refused:\n${_error}")
endif()
