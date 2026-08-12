if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_generator "${JH_ROOT}/scripts/generate_board_config.py")
file(READ "${_generator}" _generator_text)

foreach(_fact IN ITEMS
        HAL_TARGET_DESCRIPTOR_ID
        HAL_TARGET_BACKEND_NAME
        HAL_TARGET_MCU_NAME
        HAL_TARGET_MCU_SUBTYPE_NAME
        HAL_TARGET_CPU_ARCH_NAME
        HAL_TARGET_CPU_CORES
        HAL_TARGET_HAS_FPU
        HAL_TARGET_RAM_TOTAL_BYTES
        HAL_TARGET_RAM_USABLE_BYTES)
    string(FIND "${_generator_text}" "${_fact}" _fact_pos)
    if(_fact_pos EQUAL -1)
        message(FATAL_ERROR "Board generator does not emit ${_fact}")
    endif()
endforeach()

foreach(_backend IN ITEMS rp2040 stm32g474 .mock)
    set(_system "${JH_ROOT}/src/hal/impl/${_backend}/hal_system.cpp")
    file(READ "${_system}" _system_text)
    foreach(_fact IN ITEMS
            HAL_TARGET_DESCRIPTOR_ID
            HAL_TARGET_BACKEND_NAME
            HAL_TARGET_MCU_NAME
            HAL_TARGET_MCU_SUBTYPE_NAME
            HAL_TARGET_CPU_ARCH_NAME
            HAL_TARGET_CPU_CORES
            HAL_TARGET_HAS_FPU
            HAL_TARGET_RAM_TOTAL_BYTES
            HAL_TARGET_RAM_USABLE_BYTES
            HAL_BOARD_EXPECTED_FLASH_BYTES)
        string(FIND "${_system_text}" "${_fact}" _fact_pos)
        if(_fact_pos EQUAL -1)
            message(FATAL_ERROR
                "${_backend} architecture snapshot bypasses generated ${_fact}")
        endif()
    endforeach()
endforeach()

foreach(_driver IN ITEMS
        "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_system.cpp"
        "${JH_ROOT}/src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_system.cpp")
    file(READ "${_driver}" _driver_text)
    foreach(_forbidden IN ITEMS
            rp2040_system_get_arch_info
            stm32g474_system_get_arch_info
            "ARM Cortex-M0+"
            "ARM Cortex-M33"
            "Hazard3 RISC-V"
            "ARM Cortex-M4F")
        string(FIND "${_driver_text}" "${_forbidden}" _forbidden_pos)
        if(NOT _forbidden_pos EQUAL -1)
            message(FATAL_ERROR
                "Target driver retains static architecture fact '${_forbidden}'")
        endif()
    endforeach()
endforeach()
