include_guard(GLOBAL)

# FEATURES is the already-resolved registry closure. Reserve CCM for the TLS
# provider when enabled; otherwise let CPU-only BSS use that bank.
function(jh_stm32g474_configure_memory TARGET SCOPE)
    cmake_parse_arguments(ARG "" "" "FEATURES" ${ARGN})
    if(NOT "HAL_ENABLE_TLS" IN_LIST ARG_FEATURES)
        target_compile_definitions(${TARGET} ${SCOPE}
            "JH_CPU_ONLY_BSS_SECTION=\".bss.hal_cpu.ccm\"")
    endif()
endfunction()
