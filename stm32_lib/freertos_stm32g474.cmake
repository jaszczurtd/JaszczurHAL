get_filename_component(_jh_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(JH_REPO_ROOT "${_jh_repo_root}" CACHE INTERNAL "JaszczurHAL repository root")

function(jh_read_shell_conf OUT_VAR KEY DEFAULT_VALUE)
    set(_value "${DEFAULT_VALUE}")
    set(_conf "${JH_REPO_ROOT}/freertos_core_version.conf")
    if(EXISTS "${_conf}")
        file(STRINGS "${_conf}" _lines REGEX "^${KEY}=")
        foreach(_line IN LISTS _lines)
            string(REGEX REPLACE "^${KEY}=(.*)$" "\\1" _raw "${_line}")
            string(STRIP "${_raw}" _raw)
            string(REGEX REPLACE "^['\"](.*)['\"]$" "\\1" _raw "${_raw}")
            set(_value "${_raw}")
        endforeach()
    endif()
    set(${OUT_VAR} "${_value}" PARENT_SCOPE)
endfunction()

jh_read_shell_conf(_jh_freertos_kernel_dir_default FREERTOS_KERNEL_DIR "third_party/FreeRTOS-Kernel")
if(NOT IS_ABSOLUTE "${_jh_freertos_kernel_dir_default}")
    set(_jh_freertos_kernel_dir_default "${JH_REPO_ROOT}/${_jh_freertos_kernel_dir_default}")
endif()
if(DEFINED ENV{JH_FREERTOS_KERNEL_DIR} AND NOT DEFINED JH_FREERTOS_KERNEL_DIR)
    set(_jh_freertos_kernel_dir_default "$ENV{JH_FREERTOS_KERNEL_DIR}")
endif()

set(JH_FREERTOS_KERNEL_DIR "${_jh_freertos_kernel_dir_default}" CACHE PATH
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

function(jh_ensure_freertos_kernel)
    get_property(_already_ensured GLOBAL PROPERTY JH_FREERTOS_KERNEL_ENSURED)
    if(_already_ensured)
        return()
    endif()

    set(_helper "${JH_REPO_ROOT}/scripts/ensure_freertos_kernel.sh")
    if(NOT EXISTS "${_helper}")
        message(FATAL_ERROR "Missing FreeRTOS dependency helper: ${_helper}")
    endif()

    set(_ensure_args --enable --repo-root "${JH_REPO_ROOT}")
    if(NOT "${JH_FREERTOS_KERNEL_DIR}" STREQUAL "${_jh_freertos_kernel_dir_default}")
        list(APPEND _ensure_args --kernel-dir "${JH_FREERTOS_KERNEL_DIR}")
    endif()

    execute_process(
        COMMAND "${_helper}" ${_ensure_args}
        RESULT_VARIABLE _ensure_result
    )
    if(NOT _ensure_result EQUAL 0)
        message(FATAL_ERROR "Failed to prepare FreeRTOS-Kernel for STM32G474 builds.")
    endif()

    set_property(GLOBAL PROPERTY JH_FREERTOS_KERNEL_ENSURED TRUE)
endfunction()

function(jh_stm32g474_enable_freertos TARGET_NAME)
    jh_ensure_freertos_kernel()

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
                "Run scripts/ensure_freertos_kernel.sh --enable, or pass "
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
