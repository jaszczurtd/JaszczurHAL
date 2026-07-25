if(NOT JH_CXX OR NOT JH_ROOT)
    message(FATAL_ERROR "JH_CXX and JH_ROOT are required")
endif()

set(_probe "${JH_ROOT}/tests/fixtures/board_selection_probe.cpp")
set(_include "${JH_ROOT}/src")
include("${JH_ROOT}/tests/jh_test_artifacts.cmake")
jh_test_artifact_dir(_artifact_dir board-selection)

function(check_board NAME EXPECT_SUCCESS EXPECT_TEXT)
    set(_object "${_artifact_dir}/board_selection_${NAME}.o")
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

set(_pico
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP_PICO -DJH_EXPECT_USB=1
    -DJH_EXPECT_LED=25 -DJH_EXPECT_NAME_PICO=1)
set(_pico_w
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP_PICO_W -DJH_EXPECT_USB=1
    -DJH_EXPECT_CYW43=1 -DJH_EXPECT_LED=64 -DJH_EXPECT_NAME_PICO_W=1
    -DJH_EXPECT_LEGACY_PICOW=1)
set(_pico_2
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP_PICO_2 -DJH_EXPECT_USB=1
    -DJH_EXPECT_LED=25 -DJH_EXPECT_NAME_PICO_2=1)
set(_pico_2_w
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP_PICO_2_W -DJH_EXPECT_USB=1
    -DJH_EXPECT_CYW43=1 -DJH_EXPECT_LED=64 -DJH_EXPECT_NAME_PICO_2_W=1
    -DJH_EXPECT_LEGACY_PICOW=1)
set(_pim730
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP_PICO_PIM730 -DJH_EXPECT_USB=1
    -DJH_EXPECT_CYW43=1 -DJH_EXPECT_EXTERNAL_RADIO=1 -DJH_EXPECT_LED=25
    -DJH_EXPECT_NAME_PICO_PIM730=1 -DJH_EXPECT_LEGACY_PIM730=1)

check_board(explicit_pico TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO=1 ${_pico})
check_board(explicit_pico_w TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO_W=1 ${_pico_w})
check_board(explicit_pico_2_arm TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 -DHAL_BOARD_PROFILE_RP_PICO_2=1 ${_pico_2})
check_board(explicit_pico_2_w_riscv TRUE ""
    -DHAL_TARGET_RP2350_RISCV=1 -DHAL_BOARD_PROFILE_RP_PICO_2_W=1
    ${_pico_2_w})
check_board(explicit_pim730 TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO_PIM730=1 ${_pim730})

check_board(default_rp2040 FALSE "unknown RP board" -DHAL_TARGET_RP2040=1)
check_board(default_rp2350 FALSE "unknown RP board" -DHAL_TARGET_RP2350_ARM=1)
check_board(picosdk_pico TRUE ""
    -DHAL_TARGET_RP2040=1 -DRASPBERRYPI_PICO=1 ${_pico})
check_board(picosdk_pico_w TRUE ""
    -DHAL_TARGET_RP2040=1 -DRASPBERRYPI_PICO_W=1 ${_pico_w})
check_board(picosdk_pico_2_arm TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 -DRASPBERRYPI_PICO2=1 ${_pico_2})
check_board(picosdk_pico_2_w_riscv TRUE ""
    -DHAL_TARGET_RP2350_RISCV=1 -DRASPBERRYPI_PICO2_W=1 ${_pico_2_w})
check_board(legacy_picow_rp2040 TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_CYW43_PROFILE_PICOW=1 ${_pico_w})
check_board(legacy_picow_rp2350 TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 -DHAL_CYW43_PROFILE_PICOW=1 ${_pico_2_w})
check_board(legacy_pim730 TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_CYW43_PROFILE_PIM730=1 ${_pim730})
check_board(stm32_generic TRUE ""
    -DHAL_TARGET_STM32G474=1
    -DJH_EXPECT_PROFILE=HAL_BOARD_STM32G474_GENERIC
    -DJH_EXPECT_LED=5 -DJH_EXPECT_NAME_STM32=1)
check_board(host_mock TRUE ""
    -DHAL_TARGET_MOCK=1 -DJH_EXPECT_PROFILE=HAL_BOARD_HOST_MOCK
    -DJH_EXPECT_NAME_MOCK=1)

check_board(multiple_profiles FALSE "exactly one HAL_BOARD_PROFILE"
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO=1
    -DHAL_BOARD_PROFILE_RP_PICO_W=1)
check_board(pico_on_rp2350 FALSE "profiles require HAL_TARGET_RP2040"
    -DHAL_TARGET_RP2350_ARM=1 -DHAL_BOARD_PROFILE_RP_PICO=1)
check_board(pico2_on_rp2040 FALSE "profiles require an RP2350 target"
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO_2=1)
check_board(rp_profile_on_mock FALSE "profiles require HAL_TARGET_RP2040"
    -DHAL_TARGET_MOCK=1 -DHAL_BOARD_PROFILE_RP_PICO_W=1)
check_board(conflicting_legacy_profile FALSE
    "conflicts with the selected board profile"
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO=1
    -DHAL_CYW43_PROFILE_PICOW=1)
