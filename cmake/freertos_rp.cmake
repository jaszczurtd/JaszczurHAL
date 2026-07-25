include_guard(GLOBAL)

set(JH_RP_FREERTOS_CONFIG_DIR
    "${JH_ROOT}/src/hal/impl/rp2040/freertos")
set(JH_FREERTOS_KERNEL_DIR
    "${JH_ROOT}/third_party/FreeRTOS-Kernel" CACHE PATH
    "Path to the pinned FreeRTOS-Kernel checkout used by native RP builds")

function(jh_rp_enable_freertos TARGET_NAME)
    if(NOT TARGET "${TARGET_NAME}")
        message(FATAL_ERROR
            "jh_rp_enable_freertos: target '${TARGET_NAME}' does not exist")
    endif()

    if(PICO_PLATFORM STREQUAL "rp2040")
        set(_port_dir
            "${JH_FREERTOS_KERNEL_DIR}/portable/ThirdParty/GCC/RP2040")
        set(_port_sources "${_port_dir}/port.c")
    elseif(PICO_PLATFORM STREQUAL "rp2350-arm-s")
        set(_port_dir
            "${JH_FREERTOS_KERNEL_DIR}/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ")
        set(_port_sources
            "${_port_dir}/non_secure/port.c"
            "${_port_dir}/non_secure/portasm.c")
    elseif(PICO_PLATFORM STREQUAL "rp2350-riscv")
        set(_port_dir
            "${JH_FREERTOS_KERNEL_DIR}/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_RISC-V")
        set(_port_sources
            "${_port_dir}/port.c"
            "${_port_dir}/portASM.S")
    else()
        message(FATAL_ERROR
            "No native FreeRTOS port for PICO_PLATFORM=${PICO_PLATFORM}")
    endif()

    set(_required_paths
        "${JH_FREERTOS_KERNEL_DIR}/include/FreeRTOS.h"
        "${JH_FREERTOS_KERNEL_DIR}/include/task.h"
        "${JH_FREERTOS_KERNEL_DIR}/include/semphr.h"
        "${JH_RP_FREERTOS_CONFIG_DIR}/FreeRTOSConfig.h"
        "${JH_RP_FREERTOS_CONFIG_DIR}/freertos_hooks.c"
        "${_port_dir}/CMakeLists.txt"
        ${_port_sources})
    foreach(_path IN LISTS _required_paths)
        if(NOT EXISTS "${_path}")
            message(FATAL_ERROR
                "Native RP FreeRTOS requires the pinned FreeRTOS-Kernel. "
                "Missing: ${_path}\n"
                "Run scripts/ensure_freertos_kernel.sh --enable.")
        endif()
    endforeach()

    set(FREERTOS_KERNEL_PATH "${JH_FREERTOS_KERNEL_DIR}" CACHE PATH
        "FreeRTOS kernel path for the native RP port" FORCE)
    set(FREERTOS_CONFIG_FILE_DIRECTORY "${JH_RP_FREERTOS_CONFIG_DIR}"
        CACHE PATH "FreeRTOS configuration for native RP targets" FORCE)

    if(NOT TARGET FreeRTOS-Kernel)
        add_subdirectory("${_port_dir}"
            "${CMAKE_CURRENT_BINARY_DIR}/jh_freertos_kernel")
    endif()

    target_sources("${TARGET_NAME}" PRIVATE
        "${JH_RP_FREERTOS_CONFIG_DIR}/freertos_hooks.c")
    if(PICO_PLATFORM STREQUAL "rp2350-riscv")
        target_sources("${TARGET_NAME}" INTERFACE
            "${JH_RP_FREERTOS_CONFIG_DIR}/rp2350_riscv_tick.c")
        target_link_libraries("${TARGET_NAME}" PUBLIC
            hardware_riscv_platform_timer)
    endif()
    target_compile_definitions("${TARGET_NAME}" PUBLIC
        HAL_ENABLE_FREERTOS=1
        __FREERTOS=1)
    target_include_directories("${TARGET_NAME}" PUBLIC
        "${JH_FREERTOS_KERNEL_DIR}/include"
        "${JH_RP_FREERTOS_CONFIG_DIR}")
    if(PICO_PLATFORM STREQUAL "rp2350-arm-s")
        target_include_directories("${TARGET_NAME}" PUBLIC
            "${_port_dir}/non_secure")
    else()
        target_include_directories("${TARGET_NAME}" PUBLIC
            "${_port_dir}/include")
    endif()
    # Pico SDK interface libraries (notably pico_flash) add sources to the
    # final firmware target. Propagate the kernel usage requirements so those
    # sources select the scheduler-aware FreeRTOS SMP implementation too.
    target_link_libraries("${TARGET_NAME}" PUBLIC FreeRTOS-Kernel-Heap4)

    set(_kernel_sources
        "${JH_FREERTOS_KERNEL_DIR}/croutine.c"
        "${JH_FREERTOS_KERNEL_DIR}/event_groups.c"
        "${JH_FREERTOS_KERNEL_DIR}/list.c"
        "${JH_FREERTOS_KERNEL_DIR}/queue.c"
        "${JH_FREERTOS_KERNEL_DIR}/stream_buffer.c"
        "${JH_FREERTOS_KERNEL_DIR}/tasks.c"
        "${JH_FREERTOS_KERNEL_DIR}/timers.c"
        "${JH_FREERTOS_KERNEL_DIR}/portable/MemMang/heap_4.c"
        ${_port_sources})
    set_source_files_properties(${_kernel_sources} PROPERTIES
        COMPILE_OPTIONS "-w")
endfunction()
