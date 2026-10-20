if(NOT JH_CXX OR NOT JH_ROOT OR NOT JH_ASSEMBLY_ARGUMENTS)
    message(FATAL_ERROR "JH_CXX, JH_ROOT and JH_ASSEMBLY_ARGUMENTS are required")
endif()
# Compiler arguments of the generated PIM730 assemblies (tests/CMakeLists.txt):
# JH_PICO_PIM730_ARGUMENTS and JH_NUCLEO_PIM730_ARGUMENTS.
include("${JH_ASSEMBLY_ARGUMENTS}")

set(_probe "${JH_ROOT}/tests/fixtures/board_selection_probe.cpp")
include("${JH_ROOT}/cmake/jh_jhbase.cmake")
jh_jhbase_include_dir(_jhbase_include)
set(_include "${JH_ROOT}/src")
include("${JH_ROOT}/tests/jh_test_artifacts.cmake")
jh_test_artifact_dir(_artifact_dir board-selection)

function(check_board NAME EXPECT_SUCCESS EXPECT_TEXT)
    set(_object "${_artifact_dir}/board_selection_${NAME}.o")
    execute_process(
        COMMAND "${JH_CXX}" -std=c++17 -Wall -Wextra -Werror
                "-I${_include}" "-I${_jhbase_include}" ${ARGN} -c "${_probe}" -o "${_object}"
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
    -DJH_EXPECT_LED=25 -DJH_EXPECT_NAME="pico")
set(_pico_w
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP_PICO_W -DJH_EXPECT_USB=1
    -DJH_EXPECT_CYW43=1 -DJH_EXPECT_LED=64
    -DJH_EXPECT_BLUETOOTH=1 -DJH_EXPECT_NAME="picow"
    -DJH_EXPECT_LEGACY_PICOW=1)
set(_pico_2
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP_PICO_2 -DJH_EXPECT_USB=1
    -DJH_EXPECT_LED=25 -DJH_EXPECT_NAME="pico2")
set(_pico_2_w
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP_PICO_2_W -DJH_EXPECT_USB=1
    -DJH_EXPECT_CYW43=1 -DJH_EXPECT_LED=64 -DJH_EXPECT_BLUETOOTH=1
    -DJH_EXPECT_NAME="pico2w"
    -DJH_EXPECT_LEGACY_PICOW=1)
set(_rp2040_zero
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP2040_ZERO -DJH_EXPECT_USB=1
    -DJH_EXPECT_NAME="rp2040-zero" -DJH_EXPECT_NO_LED_BUILTIN=1)
set(_rp2040_plus
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP2040_PLUS_4MB -DJH_EXPECT_USB=1
    -DJH_EXPECT_LED=25 -DJH_EXPECT_NAME="rp2040-plus-4mb")
set(_rp2040_lora
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP2040_LORA_LF -DJH_EXPECT_USB=1
    -DJH_EXPECT_SX1262=1 -DJH_EXPECT_LED=25
    -DJH_EXPECT_NAME="rp2040-lora-lf")
set(_nucleo
    -DJH_EXPECT_PROFILE=HAL_BOARD_STM32G474_NUCLEO_G474RE
    -DJH_EXPECT_LED=5 -DJH_EXPECT_NAME="nucleo-g474re")
set(_host_mock
    -DJH_EXPECT_PROFILE=HAL_BOARD_HOST_MOCK -DJH_EXPECT_BLUETOOTH=1
    -DJH_EXPECT_NAME="host-mock")

check_board(explicit_pico TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO=1 ${_pico})
check_board(explicit_pico_w TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO_W=1 ${_pico_w})
check_board(explicit_pico_2_arm TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 -DHAL_BOARD_PROFILE_RP_PICO_2=1 ${_pico_2})
check_board(explicit_pico_2_w_arm TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 -DHAL_BOARD_PROFILE_RP_PICO_2_W=1
    ${_pico_2_w})
check_board(explicit_rp2040_zero TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP2040_ZERO=1 ${_rp2040_zero})
check_board(explicit_rp2040_plus TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP2040_PLUS_4MB=1
    ${_rp2040_plus})
check_board(explicit_rp2040_lora TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP2040_LORA_LF=1
    ${_rp2040_lora})
check_board(explicit_nucleo TRUE ""
    -DHAL_TARGET_STM32G474=1
    -DHAL_BOARD_PROFILE_STM32G474_NUCLEO_G474RE=1 ${_nucleo})

check_board(default_rp2040 FALSE "unknown board" -DHAL_TARGET_RP2040=1)
check_board(default_rp2350 FALSE "unknown board" -DHAL_TARGET_RP2350_ARM=1)
check_board(picosdk_pico TRUE ""
    -DHAL_TARGET_RP2040=1 -DRASPBERRYPI_PICO=1 ${_pico})
check_board(picosdk_pico_w TRUE ""
    -DHAL_TARGET_RP2040=1 -DRASPBERRYPI_PICO_W=1 ${_pico_w})
check_board(picosdk_pico_2_arm TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 -DRASPBERRYPI_PICO2=1 ${_pico_2})
check_board(picosdk_pico_2_w_arm TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 -DRASPBERRYPI_PICO2_W=1 ${_pico_2_w})
check_board(legacy_picow_rp2040 TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_CYW43_PROFILE_PICOW=1 ${_pico_w})
check_board(legacy_picow_rp2350_arm TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 -DHAL_CYW43_PROFILE_PICOW=1 ${_pico_2_w})
check_board(stm32_generic TRUE ""
    -DHAL_TARGET_STM32G474=1
    -DHAL_BOARD_PROFILE_STM32G474_GENERIC=1 ${_nucleo})
check_board(host_mock TRUE ""
    -DHAL_TARGET_MOCK=1 ${_host_mock})
check_board(default_stm32 TRUE ""
    -DHAL_TARGET_STM32G474=1 ${_nucleo})

check_board(multiple_profiles FALSE "exactly one HAL_BOARD_PROFILE"
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO=1
    -DHAL_BOARD_PROFILE_RP_PICO_W=1)
check_board(pico_on_rp2350 FALSE "board pico is incompatible"
    -DHAL_TARGET_RP2350_ARM=1 -DHAL_BOARD_PROFILE_RP_PICO=1)
check_board(pico2_on_rp2040 FALSE "board pico2 is incompatible"
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO_2=1)
check_board(pico2w_on_riscv FALSE "board pico2w is incompatible"
    -DHAL_TARGET_RP2350_RISCV=1 -DHAL_BOARD_PROFILE_RP_PICO_2_W=1)
check_board(rp_profile_on_mock FALSE "board picow is incompatible"
    -DHAL_TARGET_MOCK=1 -DHAL_BOARD_PROFILE_RP_PICO_W=1)
check_board(conflicting_legacy_profile FALSE
    "exactly one HAL_BOARD_PROFILE"
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO=1
    -DHAL_CYW43_PROFILE_PICOW=1)
check_board(multiple_provider_selectors FALSE
    "multiple provider board selectors"
    -DHAL_TARGET_RP2040=1 -DRASPBERRYPI_PICO=1
    -DRASPBERRYPI_PICO_W=1)

# A module assembly keeps the identity of its base board and adds the radio of
# the module: CYW43, the external frontend and Bluetooth.
check_board(assembly_pico_pim730 TRUE ""
    -DHAL_TARGET_RP2040=1 ${JH_PICO_PIM730_ARGUMENTS}
    -DJH_EXPECT_PROFILE=HAL_BOARD_RP_PICO -DJH_EXPECT_USB=1
    -DJH_EXPECT_CYW43=1 -DJH_EXPECT_EXTERNAL_RADIO=1 -DJH_EXPECT_LED=25
    -DJH_EXPECT_BLUETOOTH=1 -DJH_EXPECT_NAME="pico")
check_board(assembly_nucleo_pim730 TRUE ""
    -DHAL_TARGET_STM32G474=1 ${JH_NUCLEO_PIM730_ARGUMENTS}
    -DJH_EXPECT_PROFILE=HAL_BOARD_STM32G474_NUCLEO_G474RE
    -DJH_EXPECT_CYW43=1 -DJH_EXPECT_EXTERNAL_RADIO=1
    -DJH_EXPECT_BLUETOOTH=1 -DJH_EXPECT_LED=5
    -DJH_EXPECT_NAME="nucleo-g474re")

# Removed composite profiles fail with their migration. Without the check each
# case builds the base board it expects, without the module.
check_board(removed_nucleo_canhat FALSE
    "the nucleo-g474re-canhat profile was removed"
    -DHAL_TARGET_STM32G474=1 -DHAL_BOARD_PROFILE_STM32G474_NUCLEO_CANHAT=1
    ${_nucleo})
check_board(removed_nucleo_pim730 FALSE
    "the nucleo-g474re-pim730 profile was removed"
    -DHAL_TARGET_STM32G474=1 -DHAL_BOARD_PROFILE_STM32G474_NUCLEO_PIM730=1
    ${_nucleo})
check_board(removed_nucleo_core1262 FALSE
    "the nucleo-g474re-core1262-hf profile was removed"
    -DHAL_TARGET_STM32G474=1
    -DHAL_BOARD_PROFILE_STM32G474_NUCLEO_CORE1262_HF=1 ${_nucleo})
check_board(removed_pico_pim730 FALSE "the pico-rm2 profile was removed"
    -DHAL_TARGET_RP2040=1 -DRASPBERRYPI_PICO=1
    -DHAL_BOARD_PROFILE_RP_PICO_PIM730=1 ${_pico})
check_board(removed_legacy_pim730 FALSE "the pico-rm2 profile was removed"
    -DHAL_TARGET_RP2040=1 -DRASPBERRYPI_PICO=1 -DHAL_CYW43_PROFILE_PIM730=1
    ${_pico})
check_board(removed_pico_core1262 FALSE
    "the pico-core1262-hf profile was removed"
    -DHAL_TARGET_RP2040=1 -DRASPBERRYPI_PICO=1
    -DHAL_BOARD_PROFILE_RP_PICO_CORE1262_HF=1 ${_pico})

# A generated jh_board_config.h replaces the fallback, so the check must not
# live only there.
set(_generated "${_artifact_dir}/removed-selector-generated")
file(MAKE_DIRECTORY "${_generated}")
file(WRITE "${_generated}/jh_board_config.h"
    "#pragma once\n#define HAL_BOARD_PROFILE_STM32G474_NUCLEO_G474RE 1\n")
check_board(removed_selector_with_generated_config FALSE
    "the nucleo-g474re-canhat profile was removed"
    -DHAL_TARGET_STM32G474=1 "-I${_generated}"
    -DHAL_BOARD_PROFILE_STM32G474_NUCLEO_CANHAT=1)
