get_filename_component(_jh_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(JH_REPO_ROOT "${_jh_repo_root}" CACHE INTERNAL "JaszczurHAL repository root")
set(JH_FREERTOS_KERNEL_DIR "${JH_REPO_ROOT}/third_party/FreeRTOS-Kernel" CACHE PATH
    "Path to a local FreeRTOS-Kernel checkout used by STM32G474 builds")

set(JH_STM32G474_FREERTOS_CONFIG_DIR
    "${JH_REPO_ROOT}/src/hal/impl/stm32g474/freertos")

function(jh_cmake_defines_contain OUT_VAR MACRO_NAME)
    set(_found FALSE)
    foreach(_def IN LISTS ARGN)
        if("${_def}" STREQUAL "${MACRO_NAME}" OR "${_def}" MATCHES "^${MACRO_NAME}=")
            set(_found TRUE)
        endif()
    endforeach()
    set(${OUT_VAR} ${_found} PARENT_SCOPE)
endfunction()

function(jh_stm32g474_enable_freertos TARGET_NAME)
    set(_kernel "${JH_FREERTOS_KERNEL_DIR}")
    set(_config_dir "${JH_STM32G474_FREERTOS_CONFIG_DIR}")

    set(_freertos_sources
        "${_kernel}/tasks.c"
        "${_kernel}/queue.c"
        "${_kernel}/list.c"
        "${_kernel}/timers.c"
        "${_kernel}/event_groups.c"
        "${_kernel}/stream_buffer.c"
        "${_kernel}/portable/GCC/ARM_CM4F/port.c"
        "${_kernel}/portable/MemMang/heap_4.c"
    )

    set(_required_paths
        "${_kernel}/include/FreeRTOS.h"
        "${_kernel}/include/task.h"
        "${_kernel}/include/semphr.h"
        "${_kernel}/portable/GCC/ARM_CM4F/portmacro.h"
        "${_config_dir}/FreeRTOSConfig.h"
        "${_config_dir}/freertos_hooks.c"
        ${_freertos_sources}
    )

    foreach(_path IN LISTS _required_paths)
        if(NOT EXISTS "${_path}")
            message(FATAL_ERROR
                "HAL_ENABLE_FREERTOS for STM32G474 requires a local FreeRTOS-Kernel checkout. "
                "Missing: ${_path}\n"
                "Install it at third_party/FreeRTOS-Kernel or pass "
                "-DJH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel.")
        endif()
    endforeach()

    target_sources("${TARGET_NAME}" PRIVATE
        ${_freertos_sources}
        "${_config_dir}/freertos_hooks.c"
    )

    target_include_directories("${TARGET_NAME}" PUBLIC
        "${_kernel}/include"
        "${_kernel}/portable/GCC/ARM_CM4F"
        "${_config_dir}"
    )

    target_compile_definitions("${TARGET_NAME}" PUBLIC
        HAL_ENABLE_FREERTOS=1
    )

    set_source_files_properties(${_freertos_sources} PROPERTIES
        COMPILE_OPTIONS "-w"
    )
endfunction()
