# ─────────────────────────────────────────────────────────────────────────────
# jh_stm32g474_firmware.cmake
#
# Reusable STM32G474 firmware build for JaszczurHAL consumers. This is the SINGLE
# source of truth for "how to link a JaszczurHAL app into an STM32G474 ELF" -
# examples, VS Code projects, and downstream firmware (e.g. Fiesta modules) all
# call it instead of copy-pasting the recipe.
#
# It is target-only: consuming project source trees stay target-unaware. The
# caller passes portable app sources; this module adds the STM32G474 backend,
# shared drivers, startup, linker script, arch flags and objcopy steps.
#
# Requires the STM32 cross toolchain (see stm32_lib/toolchain_stm32g474.cmake),
# so the consuming project must be configured with
#   -DCMAKE_TOOLCHAIN_FILE=<jh>/stm32_lib/toolchain_stm32g474.cmake
# and must enable_language(C CXX ASM) (i.e. project(... C CXX ASM)).
#
# Usage:
#   include(<jh>/stm32_lib/jh_stm32g474_firmware.cmake)
#   jh_add_stm32g474_firmware(firmware
#       SOURCES  ${app_sources}
#       INCLUDES ${app_include_dirs}
#       DEFINES  HAL_PROVIDE_APP_ENTRY=1 HAL_ENABLE_APP_TASK1=1)
#   jh_stm32g474_add_openocd_upload(firmware_upload ELF_TARGET firmware)
# ─────────────────────────────────────────────────────────────────────────────

include_guard(GLOBAL)

# Directory of this module == <jh_root>/stm32_lib.
set(_JH_STM32_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/jh_project_features.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/jh_stack_protector.cmake")

# jh_add_stm32g474_firmware(<target>
#     SOURCES  <portable app sources (.c/.cpp)>
#     INCLUDES <extra include dirs>
#     DEFINES  <extra compile definitions, e.g. HAL_ENABLE_*>
#     FEATURES <requested feature names defined by a project config header>
#     RESOLVED_FEATURES <optional registry closure for source/link selection>
#     LIBRARIES <precompiled static archives or CMake library targets>
#     [JH_ROOT <path>]   # JaszczurHAL repo root; defaults to this module's ../
# )
# Produces an executable <target> named "<target>.elf" plus <target>.bin/.hex.
function(jh_add_stm32g474_firmware TARGET)
    cmake_parse_arguments(ARG "" "JH_ROOT"
        "SOURCES;INCLUDES;DEFINES;FEATURES;RESOLVED_FEATURES;OPTIONS;LIBRARIES"
        ${ARGN})

    jh_normalize_feature_defines(ARG_DEFINES ${ARG_DEFINES})
    jh_normalize_feature_defines(ARG_FEATURES ${ARG_FEATURES})
    jh_resolve_feature_defines(
        _jh_requested_features _jh_expected_selection_features
        ${ARG_DEFINES} ${ARG_FEATURES})
    if(ARG_RESOLVED_FEATURES)
        jh_split_feature_defines(
            _jh_selection_features _jh_resolved_non_features
            ${ARG_RESOLVED_FEATURES})
        if(_jh_resolved_non_features OR
           NOT "${_jh_selection_features}" STREQUAL
               "${_jh_expected_selection_features}")
            message(FATAL_ERROR
                "[JH-CFG-PARITY] jh_add_stm32g474_firmware(${TARGET}) "
                "RESOLVED_FEATURES differs from DEFINES/FEATURES: expected "
                "'${_jh_expected_selection_features}', got "
                "'${_jh_selection_features}'")
        endif()
    else()
        set(_jh_selection_features ${_jh_expected_selection_features})
    endif()

    if(NOT ARG_JH_ROOT)
        get_filename_component(ARG_JH_ROOT "${_JH_STM32_MODULE_DIR}/.." ABSOLUTE)
    endif()
    if(NOT EXISTS "${ARG_JH_ROOT}/src/hal/hal.h")
        message(FATAL_ERROR "jh_add_stm32g474_firmware: JaszczurHAL not found at '${ARG_JH_ROOT}'")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "jh_add_stm32g474_firmware(${TARGET}): SOURCES is required")
    endif()

    set(_jh_src "${ARG_JH_ROOT}/src")
    set(_g474 "${_jh_src}/hal/impl/stm32g474")
    set(_ldscript "${ARG_JH_ROOT}/stm32_lib/STM32G474RETx_FLASH.ld")
    include("${ARG_JH_ROOT}/cmake/jh_bearssl.cmake")
    include("${ARG_JH_ROOT}/cmake/jh_feature_build_effects.cmake")
    include("${ARG_JH_ROOT}/cmake/jh_managed_frameworks.cmake")
    jh_managed_framework_include_dirs(_jh_framework_include_dirs)
    jh_managed_framework_configure_sources()
    jh_collect_feature_build_effects(_jh_build_effects
        ROOT "${ARG_JH_ROOT}"
        FEATURES ${_jh_selection_features})
    jh_feature_build_dependency_enabled(_jh_has_tls bearssl
        FEATURES ${_jh_selection_features})
    jh_cmake_defines_contain(_jh_has_stack_protector
        HAL_ENABLE_STACK_PROTECTOR ${_jh_selection_features})
    set(_littlefs ${_jh_build_effects_LITTLEFS_SOURCES})
    set(_jh_sx126x_sources ${_jh_build_effects_SX126X_SOURCES})

    # Backend + target-independent thematic sources.
    file(GLOB _impl CONFIGURE_DEPENDS "${_g474}/*.cpp")
    file(GLOB _drivers CONFIGURE_DEPENDS "${_g474}/drivers/*/*.cpp")
    file(GLOB_RECURSE _hal_common CONFIGURE_DEPENDS
        "${_jh_src}/hal/*.cpp"
        "${_jh_src}/hal/*.c"
    )
    list(FILTER _hal_common EXCLUDE REGEX "/hal/impl/")
    list(FILTER _hal_common EXCLUDE REGEX "/network/mqtt/PubSubClient/")
    list(FILTER _hal_common EXCLUDE REGEX "/bluetooth/")
    list(APPEND _hal_common
        "${_jh_src}/hal/bluetooth/hal_ble.cpp"
        "${_jh_src}/hal/bluetooth/hal_ble_commands.cpp"
        "${_jh_src}/hal/bluetooth/hal_ble_stream.cpp"
        ${_jh_build_effects_PORTABLE_SOURCES}
        "${_jh_src}/hal_app_entry.cpp"
    )
    list(REMOVE_DUPLICATES _hal_common)
    # Base portable utils. SmartTimers/cJSON are already covered by the thematic
    # common-source inventory above.
    # multicoreWatchdog is kept (some consumers use it); --gc-sections strips it
    # when unused.
    set(_utils
        "${_jh_src}/utils/tools.cpp"
        "${_jh_src}/utils/pidController.cpp"
        "${_jh_src}/utils/draw7Segment.cpp"
        "${_jh_src}/utils/multicoreWatchdog.cpp"
    )
    add_executable(${TARGET}
        ${ARG_SOURCES}
        "${_g474}/port/startup_stm32g474.c"
        "${_g474}/port/system_stm32g474.c"
        "${_g474}/port/g474_debug_uart.c"
        "${_g474}/port/exception_info.c"
        "${_g474}/port/atomic_stubs_cm4.c"
        "${_g474}/port/runtime/stm32g474_syscalls.c"
        ${_impl}
        ${_drivers}
        ${_jh_build_effects_FEATURE_SOURCES}
        ${_jh_build_effects_DEPENDENCY_SOURCES}
        ${_hal_common}
        ${_utils}
    )

    set_target_properties(${TARGET} PROPERTIES
        OUTPUT_NAME "${TARGET}"
        SUFFIX ".elf"
        LINKER_LANGUAGE CXX
    )

    target_include_directories(${TARGET} PRIVATE
        "${_jh_src}"
        "${_jh_src}/hal"
        "${_g474}"
        ${_jh_build_effects_INCLUDE_DIRS}
        ${_jh_framework_include_dirs}
        ${ARG_INCLUDES}
    )

    target_compile_definitions(${TARGET} PRIVATE
        HAL_TARGET_STM32G474=1
        ${ARG_DEFINES}
    )

    set(_arch -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
    target_compile_options(${TARGET} PRIVATE
        ${_arch}
        -g
        -ffunction-sections
        -fdata-sections
        -Wall
        -Wextra
        -include "${_jh_src}/hal/core/hal_target.h"
        $<$<COMPILE_LANGUAGE:C>:-ffreestanding>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
        ${ARG_OPTIONS}
    )
    if(_jh_has_stack_protector)
        jh_target_enable_stack_protector(${TARGET} PRIVATE)
        jh_stack_protector_disable_sources(
            "${_jh_src}/hal/system/jh_stack_protector.c"
            "${_g474}/port/startup_stm32g474.c"
            "${_g474}/port/system_stm32g474.c"
            "${_g474}/port/exception_info.c"
            "${_g474}/drivers/stm32g474/stm32g474_fault.cpp"
            "${_g474}/freertos/freertos_hooks.c")
    endif()
    set_source_files_properties(${_littlefs} PROPERTIES COMPILE_OPTIONS "-w")
    if(_jh_sx126x_sources)
        set_source_files_properties(${_jh_sx126x_sources}
            PROPERTIES COMPILE_OPTIONS "-w")
    endif()

    target_link_options(${TARGET} PRIVATE
        ${_arch}
        "-T${_ldscript}"
        "-Wl,-Map,$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.map"
        -nostartfiles
        -Wl,--gc-sections
        -Wl,-u,_printf_float
        --specs=nano.specs
        --specs=nosys.specs
    )
    set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${_ldscript}")

    if(ARG_LIBRARIES)
        target_link_libraries(${TARGET} PRIVATE ${ARG_LIBRARIES})
    endif()

    if(_jh_has_tls)
        set(_jh_bearssl_target "${TARGET}_jh_bearssl")
        jh_add_bearssl_source_library("${_jh_bearssl_target}")
        target_compile_options("${_jh_bearssl_target}" PRIVATE ${_arch})
        target_link_libraries(${TARGET} PRIVATE "${_jh_bearssl_target}")
    endif()

    if(CMAKE_OBJCOPY)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${TARGET}>" "$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.bin"
            COMMAND "${CMAKE_OBJCOPY}" -O ihex   "$<TARGET_FILE:${TARGET}>" "$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.hex"
            VERBATIM
        )
    endif()
endfunction()

# jh_stm32g474_add_openocd_upload(<upload_target> ELF_TARGET <exe> [OPENOCD_BIN ...] ...)
# Adds a custom target that flashes the built ELF over SWD (default: on-board ST-Link).
function(jh_stm32g474_add_openocd_upload UPLOAD_TARGET)
    cmake_parse_arguments(ARG "" "ELF_TARGET;OPENOCD_BIN;OPENOCD_INTERFACE;OPENOCD_TARGET" "" ${ARGN})
    if(NOT ARG_ELF_TARGET)
        message(FATAL_ERROR "jh_stm32g474_add_openocd_upload(${UPLOAD_TARGET}): ELF_TARGET is required")
    endif()
    if(NOT ARG_OPENOCD_BIN)
        set(ARG_OPENOCD_BIN "${OPENOCD_BIN}")
    endif()
    if(NOT ARG_OPENOCD_BIN)
        set(ARG_OPENOCD_BIN "openocd")
    endif()
    if(NOT ARG_OPENOCD_INTERFACE)
        set(ARG_OPENOCD_INTERFACE "interface/stlink.cfg")
    endif()
    if(NOT ARG_OPENOCD_TARGET)
        set(ARG_OPENOCD_TARGET "target/stm32g4x.cfg")
    endif()

    add_custom_target(${UPLOAD_TARGET}
        COMMAND "${ARG_OPENOCD_BIN}" -f "${ARG_OPENOCD_INTERFACE}" -f "${ARG_OPENOCD_TARGET}"
                -c "reset_config srst_only srst_nogate connect_assert_srst"
                -c "program $<TARGET_FILE:${ARG_ELF_TARGET}> verify reset exit"
        DEPENDS ${ARG_ELF_TARGET}
        USES_TERMINAL
        VERBATIM
    )
endfunction()
