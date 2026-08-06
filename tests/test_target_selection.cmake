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

function(write_project_config NAME TARGET BOARD)
    set(_config_dir "${_artifact_dir}/project_config_${NAME}")
    file(MAKE_DIRECTORY "${_config_dir}")
    file(WRITE "${_config_dir}/hal_project_config.h"
        "#pragma once\n#define ${TARGET} 1\n#define ${BOARD} 1\n"
        "#define HAL_AT24C256_PAGE_SIZE 128u\n")
    set("${NAME}_CONFIG_DIR" "${_config_dir}" PARENT_SCOPE)
endfunction()

function(check_project_target NAME CONFIG_DIR EXPECT_SUCCESS EXPECT_TEXT)
    set(_object "${_artifact_dir}/target_selection_project_${NAME}.o")
    execute_process(
        COMMAND "${JH_CXX}" -std=c++17 -Wall -Wextra -Werror
                "-I${_include}" "-I${CONFIG_DIR}" ${ARGN}
                -c "${_probe}" -o "${_object}"
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

# Project target and board declarations are loaded before target auto-detection.
write_project_config(rp2040 HAL_TARGET_RP2040 HAL_BOARD_PROFILE_RP_PICO)
write_project_config(rp2350_arm HAL_TARGET_RP2350_ARM
    HAL_BOARD_PROFILE_RP_PICO_2)
write_project_config(rp2350_riscv HAL_TARGET_RP2350_RISCV
    HAL_BOARD_PROFILE_RP_PICO_2)
write_project_config(stm32 HAL_TARGET_STM32G474
    HAL_BOARD_PROFILE_STM32G474_GENERIC)
write_project_config(mock HAL_TARGET_MOCK HAL_BOARD_PROFILE_HOST_MOCK)

check_project_target(project_rp2040 "${rp2040_CONFIG_DIR}" TRUE ""
    -D__arm__=1 -DJH_EXPECT_BOARD_ID=HAL_BOARD_RP_PICO ${_rp2040})
check_project_target(project_rp2350_arm "${rp2350_arm_CONFIG_DIR}" TRUE ""
    -D__arm__=1 -DJH_EXPECT_BOARD_ID=HAL_BOARD_RP_PICO_2 ${_rp2350_arm})
check_project_target(project_rp2350_riscv "${rp2350_riscv_CONFIG_DIR}" TRUE ""
    -D__riscv=1 -DJH_EXPECT_BOARD_ID=HAL_BOARD_RP_PICO_2
    ${_rp2350_riscv})
check_project_target(project_stm32 "${stm32_CONFIG_DIR}" TRUE ""
    -D__arm__=1 -DJH_EXPECT_BOARD_ID=HAL_BOARD_STM32G474_GENERIC
    -DJH_EXPECT_STM32G474=1 -DJH_EXPECT_NAME_STM32G474=1)
check_project_target(project_mock "${mock_CONFIG_DIR}" TRUE ""
    -DJH_EXPECT_BOARD_ID=HAL_BOARD_HOST_MOCK
    -DJH_EXPECT_MOCK=1 -DJH_EXPECT_NAME_MOCK=1)

# The hook reaches target-dependent defaults through hal_config.h for every target.
check_project_target(hal_config_rp2040 "${rp2040_CONFIG_DIR}" TRUE ""
    -D__arm__=1 -DJH_PROBE_HAL_CONFIG=1
    -DJH_EXPECT_BOARD_ID=HAL_BOARD_RP_PICO
    -DJH_EXPECT_EEPROM_TYPE=EEPROM_TYPE_AT24C256 ${_rp2040})
check_project_target(hal_config_rp2350_arm "${rp2350_arm_CONFIG_DIR}" TRUE ""
    -D__arm__=1 -DJH_PROBE_HAL_CONFIG=1
    -DJH_EXPECT_BOARD_ID=HAL_BOARD_RP_PICO_2
    -DJH_EXPECT_EEPROM_TYPE=EEPROM_TYPE_AT24C256 ${_rp2350_arm})
check_project_target(hal_config_rp2350_riscv
    "${rp2350_riscv_CONFIG_DIR}" TRUE ""
    -D__riscv=1 -DJH_PROBE_HAL_CONFIG=1
    -DJH_EXPECT_BOARD_ID=HAL_BOARD_RP_PICO_2
    -DJH_EXPECT_EEPROM_TYPE=EEPROM_TYPE_AT24C256 ${_rp2350_riscv})
check_project_target(hal_config_stm32 "${stm32_CONFIG_DIR}" TRUE ""
    -D__arm__=1 -DJH_PROBE_HAL_CONFIG=1
    -DJH_EXPECT_BOARD_ID=HAL_BOARD_STM32G474_GENERIC
    -DJH_EXPECT_EEPROM_TYPE=EEPROM_TYPE_STM32_FLASH
    -DJH_EXPECT_STM32G474=1 -DJH_EXPECT_NAME_STM32G474=1)
check_project_target(hal_config_mock "${mock_CONFIG_DIR}" TRUE ""
    -DJH_PROBE_HAL_CONFIG=1 -DJH_EXPECT_BOARD_ID=HAL_BOARD_HOST_MOCK
    -DJH_EXPECT_EEPROM_TYPE=EEPROM_TYPE_AT24C256
    -DJH_EXPECT_MOCK=1 -DJH_EXPECT_NAME_MOCK=1)

# The public umbrella header must reach the same early hook.
check_project_target(umbrella "${mock_CONFIG_DIR}" TRUE ""
    -DJH_PROBE_UMBRELLA=1 -DJH_EXPECT_BOARD_ID=HAL_BOARD_HOST_MOCK
    -DJH_EXPECT_MOCK=1 -DJH_EXPECT_NAME_MOCK=1)

# STM32 firmware force-includes hal_target.h before compiling each source.
check_project_target(forced_include_stm32 "${stm32_CONFIG_DIR}" TRUE ""
    -D__arm__=1 -include "${_include}/hal/hal_target.h"
    -DJH_PROBE_FORCED_TARGET=1
    -DJH_EXPECT_BOARD_ID=HAL_BOARD_STM32G474_GENERIC
    -DJH_EXPECT_STM32G474=1 -DJH_EXPECT_NAME_STM32G474=1)

check_project_target(project_cli_conflict "${rp2040_CONFIG_DIR}" FALSE
    "exactly one HAL_TARGET_*" -DHAL_TARGET_MOCK=1)
