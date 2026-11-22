include_guard(GLOBAL)

set(JH_RP_FREERTOS_CONFIG_DIR
    "${JH_ROOT}/src/hal/impl/rp2040/freertos")
set(JH_FREERTOS_KERNEL_DIR
    "${JH_ROOT}/third_party/FreeRTOS-Kernel" CACHE PATH
    "Path to the pinned FreeRTOS-Kernel checkout used by native RP builds")

function(jh_ensure_rp_freertos_kernel)
    get_property(_already_ensured GLOBAL PROPERTY JH_RP_FREERTOS_KERNEL_ENSURED)
    if(_already_ensured)
        return()
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_helper "${JH_ROOT}/scripts/component_manager.py")
    if(NOT EXISTS "${_helper}")
        message(FATAL_ERROR "Missing FreeRTOS dependency helper: ${_helper}")
    endif()

    set(_ensure_args component freertos --enable --repo-root "${JH_ROOT}")
    if(NOT "${JH_FREERTOS_KERNEL_DIR}" STREQUAL
       "${JH_ROOT}/third_party/FreeRTOS-Kernel")
        list(APPEND _ensure_args --kernel-dir "${JH_FREERTOS_KERNEL_DIR}")
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_helper}" ${_ensure_args}
        RESULT_VARIABLE _ensure_result)
    if(NOT _ensure_result EQUAL 0)
        message(FATAL_ERROR "Failed to prepare FreeRTOS-Kernel for native RP")
    endif()
    set_property(GLOBAL PROPERTY JH_RP_FREERTOS_KERNEL_ENSURED TRUE)
endfunction()

function(jh_rp_enable_freertos TARGET_NAME)
    if(NOT TARGET "${TARGET_NAME}")
        message(FATAL_ERROR
            "jh_rp_enable_freertos: target '${TARGET_NAME}' does not exist")
    endif()

    jh_ensure_rp_freertos_kernel()

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

    set(_kernel_hot_sources
        "${JH_FREERTOS_KERNEL_DIR}/list.c"
        "${JH_FREERTOS_KERNEL_DIR}/queue.c"
        "${JH_FREERTOS_KERNEL_DIR}/tasks.c"
        ${_port_sources})
    set(_kernel_sources
        "${JH_FREERTOS_KERNEL_DIR}/croutine.c"
        "${JH_FREERTOS_KERNEL_DIR}/event_groups.c"
        "${JH_FREERTOS_KERNEL_DIR}/stream_buffer.c"
        "${JH_FREERTOS_KERNEL_DIR}/timers.c"
        "${JH_FREERTOS_KERNEL_DIR}/portable/MemMang/heap_4.c"
        ${_kernel_hot_sources})
    set_source_files_properties(${_kernel_sources} PROPERTIES
        COMPILE_OPTIONS "-w")

    jh_hal_define_value(_ram_code HAL_FREERTOS_RAM_CODE)
    string(REGEX REPLACE "[uUlL]+$" "" _ram_code "${_ram_code}")
    # A definition without a value reads as empty, like no definition; reject
    # it instead of silently leaving the option off.
    set(_ram_code_bare FALSE)
    if(COMMAND jh_project_defined)
        jh_project_defined(_ram_code_defined HAL_FREERTOS_RAM_CODE)
        if(_ram_code_defined AND "${_ram_code}" STREQUAL "")
            set(_ram_code_bare TRUE)
        endif()
    endif()
    foreach(_definition IN LISTS JH_RP_BOARD_DEFINES)
        if("${_definition}" MATCHES "^(-D)?HAL_FREERTOS_RAM_CODE=?$")
            set(_ram_code_bare TRUE)
        endif()
    endforeach()
    if(_ram_code_bare OR
       (NOT "${_ram_code}" STREQUAL "" AND NOT "${_ram_code}" MATCHES "^[01]$"))
        message(FATAL_ERROR "HAL_FREERTOS_RAM_CODE must be 0 or 1")
    endif()
    if(_ram_code)
        # The kernel objects join the RAM code list that
        # jh_add_rp_pico_firmware writes into the SDK text exclusions.
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/jh_ram_code.cmake")
        get_filename_component(_kernel_name "${JH_FREERTOS_KERNEL_DIR}" NAME)
        set(_patterns "")
        foreach(_source IN LISTS _kernel_hot_sources)
            file(RELATIVE_PATH _relative "${JH_FREERTOS_KERNEL_DIR}" "${_source}")
            get_filename_component(_relative_dir "${_relative}" DIRECTORY)
            jh_ram_code_object_name(_object "${_source}")
            if(NOT _relative_dir STREQUAL "")
                set(_object "${_relative_dir}/${_object}")
            endif()
            list(APPEND _patterns "*/${_kernel_name}/${_object}")
        endforeach()
        set_property(TARGET "${TARGET_NAME}" APPEND PROPERTY
            JH_RP_RAM_CODE_PATTERNS ${_patterns})
    endif()
endfunction()
