if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_contract_files
    "config/features/core.json"
    "cmake/jh_rp_native_sdk.cmake"
    "src/hal/impl/rp2040/drivers/rp2040/rp2040_fault.cpp"
    "src/hal/impl/rp2040/drivers/rp2040/rp2040_fault_entry.S"
    "src/hal/impl/rp2040/freertos/FreeRTOSConfig.h"
    "src/hal/impl/rp2040/freertos/freertos_hooks.c"
    "src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.cpp"
    "src/hal/impl/stm32g474/freertos/FreeRTOSConfig.h"
    "src/hal/impl/stm32g474/freertos/freertos_hooks.c"
    "src/hal/impl/stm32g474/port/exception_info.c"
    "src/hal/impl/stm32g474/port/system_stm32g474.c")

foreach(_relative IN LISTS _contract_files)
    if(NOT EXISTS "${JH_ROOT}/${_relative}")
        message(FATAL_ERROR "Stack-guard contract file is missing: ${_relative}")
    endif()
endforeach()

file(READ "${JH_ROOT}/config/features/core.json" _registry)
file(READ "${JH_ROOT}/cmake/jh_rp_native_sdk.cmake" _rp_cmake)
file(READ
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_fault.cpp"
    _rp_fault)
file(READ
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_fault_entry.S"
    _rp_fault_entry)
file(READ
    "${JH_ROOT}/src/hal/impl/rp2040/freertos/freertos_hooks.c"
    _rp_freertos_hooks)
file(READ
    "${JH_ROOT}/src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.cpp"
    _stm_fault)
file(READ
    "${JH_ROOT}/src/hal/impl/stm32g474/freertos/freertos_hooks.c"
    _stm_freertos_hooks)
file(READ
    "${JH_ROOT}/src/hal/impl/stm32g474/port/exception_info.c"
    _stm_exception)
file(READ
    "${JH_ROOT}/src/hal/impl/stm32g474/port/system_stm32g474.c"
    _stm_system)

function(_jh_require_text VARIABLE NEEDLE)
    string(FIND "${${VARIABLE}}" "${NEEDLE}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "Stack-guard contract is missing: ${NEEDLE}")
    endif()
endfunction()

_jh_require_text(_registry "HAL_ENABLE_STACK_GUARD")
_jh_require_text(_rp_cmake "PICO_USE_STACK_GUARDS=1")
_jh_require_text(
    _rp_fault "HAL_ENABLE_STACK_GUARD requires PICO_USE_STACK_GUARDS=1")
_jh_require_text(_rp_fault "stack_guard_registers_match")
_jh_require_text(_rp_fault "kFlagStackOverflow")
_jh_require_text(_rp_fault "HAL_RESET_REASON_STACK_OVERFLOW")
_jh_require_text(_rp_fault
    "s_retained_detail.signature = 0u;\n  __dmb();\n  watchdog_hw->scratch[1] = pc;")
_jh_require_text(_rp_fault_entry "isr_riscv_machine_exception")
_jh_require_text(_rp_fault_entry "jh_rp_fault_emergency_stack0")
_jh_require_text(_rp_freertos_hooks "jh_stack_overflow_reset();")
_jh_require_text(_stm_fault "SCB_CFSR_MMFSR_DACCVIOL")
_jh_require_text(_stm_fault "MPU_RASR_SIZE_32B")
_jh_require_text(_stm_fault "kRetainedStackOverflow")
_jh_require_text(_stm_fault "HAL_EHW")
_jh_require_text(_stm_fault
    "MLSPERR is deliberately\n   * excluded")
_jh_require_text(_stm_fault
    "s_retained.signature = 0u;\n  __asm volatile(\"\" ::: \"memory\");")
_jh_require_text(_stm_freertos_hooks "jh_stack_overflow_reset();")
_jh_require_text(_stm_exception "jh_stm32_fault_emergency_stack")
_jh_require_text(_stm_exception
    "__asm volatile(\"cpsid i               \\n\"")
_jh_require_text(_stm_exception "basic_exception_frame")
_jh_require_text(_stm_exception "#ifdef HAL_ENABLE_STACK_GUARD")
_jh_require_text(_stm_exception "jh_stm32_stack_fault_reset(&s_exc);")
_jh_require_text(_stm_system "stm32g474_fault_stack_guard_init")

foreach(_config IN ITEMS
        "src/hal/impl/rp2040/freertos/FreeRTOSConfig.h"
        "src/hal/impl/stm32g474/freertos/FreeRTOSConfig.h")
    file(READ "${JH_ROOT}/${_config}" _freertos)
    if(NOT _freertos MATCHES
       "HAL_ENABLE_STACK_GUARD[\n\r]+#define configCHECK_FOR_STACK_OVERFLOW 2")
        message(FATAL_ERROR
            "FreeRTOS stack checking is not feature-gated in ${_config}")
    endif()
endforeach()
