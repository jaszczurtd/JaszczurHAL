if(NOT JH_CXX OR NOT JH_ROOT)
    message(FATAL_ERROR "JH_CXX and JH_ROOT are required")
endif()

set(_probe "${JH_ROOT}/tests/fixtures/target_selection_probe.cpp")
set(_include "${JH_ROOT}/src")
include("${JH_ROOT}/tests/jh_test_artifacts.cmake")
jh_test_artifact_dir(_artifact_dir target-selection)

function(check_target NAME EXPECT_SUCCESS EXPECT_TEXT)
    set(_object "${_artifact_dir}/target_selection_${NAME}.o")
    execute_process(
        COMMAND "${JH_CXX}" -std=c++17 -Wall -Wextra -Werror
                "-I${_include}" ${ARGN} -c "${_probe}" -o "${_object}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )
    if(EXPECT_SUCCESS)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "${NAME} unexpectedly failed:\n${_stderr}")
        endif()
    else()
        if(_result EQUAL 0)
            message(FATAL_ERROR "${NAME} unexpectedly succeeded")
        endif()
        string(FIND "${_stderr}" "${EXPECT_TEXT}" _match)
        if(_match EQUAL -1)
            message(FATAL_ERROR
                "${NAME} did not emit '${EXPECT_TEXT}':\n${_stderr}")
        endif()
    endif()
endfunction()

set(_rp2040
    -DJH_EXPECT_RP2040=1 -DJH_EXPECT_RP=1 -DJH_EXPECT_ARM=1
    -DJH_EXPECT_NAME_RP2040=1)
set(_rp2350_arm
    -DJH_EXPECT_RP2350_ARM=1 -DJH_EXPECT_RP=1 -DJH_EXPECT_ARM=1
    -DJH_EXPECT_NAME_RP2350_ARM=1)
set(_rp2350_riscv
    -DJH_EXPECT_RP2350_RISCV=1 -DJH_EXPECT_RP=1 -DJH_EXPECT_RISCV=1
    -DJH_EXPECT_NAME_RP2350_RISCV=1)

check_target(explicit_rp2040 TRUE ""
    -DHAL_TARGET_RP2040=1 ${_rp2040})
check_target(explicit_rp2350_arm TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 ${_rp2350_arm})
check_target(explicit_rp2350_riscv TRUE ""
    -DHAL_TARGET_RP2350_RISCV=1 ${_rp2350_riscv})
check_target(explicit_stm32 TRUE ""
    -DHAL_TARGET_STM32G474=1 -DJH_EXPECT_STM32G474=1
    -DJH_EXPECT_NAME_STM32G474=1)
check_target(explicit_mock TRUE ""
    -DHAL_TARGET_MOCK=1 -DJH_EXPECT_MOCK=1 -DJH_EXPECT_NAME_MOCK=1)

check_target(autodetect_rp2040 TRUE ""
    -DPICO_RP2040=1 ${_rp2040})
check_target(autodetect_rp2350_arm TRUE ""
    -DPICO_RP2350=1 -D__arm__=1 ${_rp2350_arm})
check_target(autodetect_rp2350_riscv TRUE ""
    -DPICO_RP2350=1 -D__riscv=1 ${_rp2350_riscv})
check_target(autodetect_host_mock TRUE ""
    -DJH_EXPECT_MOCK=1 -DJH_EXPECT_NAME_MOCK=1)

check_target(ambiguous_rp2350_isa FALSE "target ISA is ambiguous"
    -DPICO_RP2350=1)
check_target(multiple_targets FALSE "exactly one HAL_TARGET_*"
    -DHAL_TARGET_RP2040=1 -DHAL_TARGET_RP2350_ARM=1)
check_target(no_bare_metal_target FALSE "no target selected"
    -D__arm__=1)
