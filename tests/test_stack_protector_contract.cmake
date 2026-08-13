if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_contract_files
    "config/features/core.json"
    "cmake/jh_stack_protector.cmake"
    "cmake/jh_rp_native_sdk.cmake"
    "stm32_lib/CMakeLists.txt"
    "stm32_lib/jh_stm32g474_firmware.cmake"
    "src/hal/system/jh_stack_protector.c"
    "src/hal/system/jh_stack_protector.h")

foreach(_relative IN LISTS _contract_files)
    if(NOT EXISTS "${JH_ROOT}/${_relative}")
        message(FATAL_ERROR
            "Stack-protector contract file is missing: ${_relative}")
    endif()
endforeach()

file(READ "${JH_ROOT}/config/features/core.json" _registry)
file(READ "${JH_ROOT}/cmake/jh_stack_protector.cmake" _helper)
file(READ "${JH_ROOT}/cmake/jh_rp_native_sdk.cmake" _rp)
file(READ "${JH_ROOT}/stm32_lib/CMakeLists.txt" _stm_static)
file(READ "${JH_ROOT}/stm32_lib/jh_stm32g474_firmware.cmake" _stm_firmware)
file(READ "${JH_ROOT}/src/hal/system/jh_stack_protector.c" _runtime)
file(READ
    "${JH_ROOT}/src/hal/impl/stm32g474/port/exception_info.c"
    _stm_exception)
file(READ
    "${JH_ROOT}/src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.cpp"
    _stm_fault)

function(_jh_require_text VARIABLE NEEDLE)
    string(FIND "${${VARIABLE}}" "${NEEDLE}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR
            "Stack-protector contract is missing: ${NEEDLE}")
    endif()
endfunction()

_jh_require_text(_registry "HAL_ENABLE_STACK_PROTECTOR")
_jh_require_text(_helper "-fstack-protector-strong")
_jh_require_text(_helper "JH_STACK_PROTECTOR_STRONG_COMPILE_CONTRACT=1")
_jh_require_text(_helper "-fno-stack-protector")
_jh_require_text(_rp "jh_target_enable_stack_protector(JaszczurHAL PUBLIC)")
_jh_require_text(_rp "\${SRC}/hal/system/jh_stack_protector.c")
_jh_require_text(_rp
    "\${SRC}/hal/impl/rp2040/drivers/rp2040/rp2040_fault.cpp")
_jh_require_text(_rp "\${SRC}/hal/impl/rp2040/freertos/freertos_hooks.c")
_jh_require_text(_stm_static
    "jh_target_enable_stack_protector(JaszczurHAL PUBLIC)")
_jh_require_text(_stm_static "\${SRC}/hal/system/jh_stack_protector.c")
_jh_require_text(_stm_static
    "\${SRC}/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.cpp")
_jh_require_text(_stm_static
    "\${SRC}/hal/impl/stm32g474/freertos/freertos_hooks.c")
_jh_require_text(_stm_firmware
    "jh_target_enable_stack_protector(\${TARGET} PRIVATE)")
foreach(_source IN ITEMS
        "hal/system/jh_stack_protector.c"
        "port/startup_stm32g474.c"
        "port/system_stm32g474.c"
        "port/exception_info.c"
        "drivers/stm32g474/stm32g474_fault.cpp"
        "freertos/freertos_hooks.c")
    _jh_require_text(_stm_firmware "${_source}")
endforeach()
_jh_require_text(_runtime "uintptr_t __stack_chk_guard")
_jh_require_text(_runtime "void __stack_chk_fail(void)")
_jh_require_text(_runtime
    "#if HAL_RP_ARCH_ARM || defined(JH_STM32G474_HW)")
_jh_require_text(_runtime "#elif HAL_RP_ARCH_RISCV")
_jh_require_text(_runtime "__attribute__((naked, used))")
_jh_require_text(_runtime "cpsid i")
_jh_require_text(_runtime
    "ldr r2, =jh_stack_overflow_reset_with_context")
_jh_require_text(_runtime "bx r2")
_jh_require_text(_runtime "csrci mstatus, 8")
_jh_require_text(_runtime "tail jh_stack_overflow_reset_with_context")
_jh_require_text(_runtime "jh_stack_overflow_reset_with_context(caller, 0u);")
_jh_require_text(_stm_exception "__asm volatile(\"cpsid i")
_jh_require_text(_stm_fault "exception_info_discard_last();")
_jh_require_text(_stm_fault "constexpr uint32_t kFallbackBudget = 80000u")
