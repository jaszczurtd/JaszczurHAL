cmake_minimum_required(VERSION 3.16)
project(JaszczurHAL_STM32G474 C CXX)

# The backend also compiles with the host compiler for sanity and static
# analysis (hal_target.h leaves JH_STM32G474_HW undefined there). That mode is
# opt-in so a missing cross toolchain cannot silently yield a host library.
option(JH_STM32_HOST_SANITY
    "Configure the STM32G474 backend with the host compiler for analysis"
    OFF)
if(CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH
        "STM32 toolchain used by the static-library build" FORCE)
elseif(JH_STM32_HOST_SANITY)
    message(STATUS
        "stm32_lib: host-compiler sanity build; JH_STM32G474_HW stays undefined")
else()
    message(FATAL_ERROR
        "CMAKE_TOOLCHAIN_FILE is required for stm32_lib. Pass "
        "-DJH_STM32_HOST_SANITY=ON for the host-compiler analysis build.")
endif()

set(JH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(JH_TARGET "stm32g474" CACHE STRING "JaszczurHAL target ID")
set(JH_BOARD "nucleo-g474re" CACHE STRING "Physical board profile")
set(EXTRA_HAL_DEFINES "" CACHE STRING "Extra HAL compile definitions")
option(JH_ENABLE_ALL_FEATURES
    "Enable every feature supported by the selected production target"
    OFF)
include("${JH_ROOT}/cmake/jh_project_features.cmake")
if(JH_ENABLE_ALL_FEATURES)
    jh_all_features_for_target(_jh_all_features "${JH_TARGET}")
    list(APPEND EXTRA_HAL_DEFINES ${_jh_all_features})
endif()
jh_collect_cmake_feature_defines(_jh_cmake_feature_defines)
list(APPEND EXTRA_HAL_DEFINES ${_jh_cmake_feature_defines})
jh_normalize_feature_defines(EXTRA_HAL_DEFINES ${EXTRA_HAL_DEFINES})
set(_jh_project_features "")
if(DEFINED HAL_PROJECT_CONFIG_DIR)
    foreach(_jh_project_config_dir IN LISTS HAL_PROJECT_CONFIG_DIR)
        jh_collect_project_feature_defines(_jh_project_config_features
            "${_jh_project_config_dir}")
        list(APPEND _jh_project_features ${_jh_project_config_features})
    endforeach()
    list(REMOVE_DUPLICATES _jh_project_features)
endif()
# Default STM32G474 feature profile. Keep this as the single requested set used
# by compilation, source/link selection, and the board feature signature.
set(_jh_stm32_default_features
    HAL_ENABLE_I2C
    HAL_ENABLE_I2C_SLAVE
    HAL_ENABLE_SPI
    HAL_ENABLE_MCP2515
    HAL_ENABLE_UART
    HAL_ENABLE_DAC
    HAL_ENABLE_DACLESS
    HAL_ENABLE_PCNT
    HAL_ENABLE_MCP401X
    HAL_ENABLE_MAX5395
    HAL_ENABLE_MCP9600
    HAL_ENABLE_MAX6675
    HAL_ENABLE_BH1750
    HAL_ENABLE_TSC2007
    HAL_ENABLE_STMPE610
    HAL_ENABLE_IRSMALL_DECODER
    HAL_ENABLE_EXTERNAL_ADC
    HAL_ENABLE_DS18B20
    HAL_ENABLE_DHT
    HAL_ENABLE_GPS
    HAL_ENABLE_HD44780)
jh_resolve_feature_defines(
    JH_REQUESTED_FEATURES
    JH_RESOLVED_FEATURES
    ${_jh_stm32_default_features}
    ${EXTRA_HAL_DEFINES}
    ${_jh_project_features})
include("${JH_ROOT}/cmake/jh_board_profiles.cmake")
jh_generate_board_config(
    ROOT "${JH_ROOT}"
    TARGET "${JH_TARGET}"
    BOARD "${JH_BOARD}"
    OUTPUT_DIR
        "${CMAKE_BINARY_DIR}/generated/boards/${JH_TARGET}/${JH_BOARD}"
    OUTPUT_ROOT "${CMAKE_BINARY_DIR}"
    DEFINES
        ${_jh_stm32_default_features}
        ${EXTRA_HAL_DEFINES}
        ${_jh_project_features}
    REQUESTED_FEATURES ${JH_REQUESTED_FEATURES}
    RESOLVED_FEATURES ${JH_RESOLVED_FEATURES})
if(NOT JH_RESOLVED_TARGET STREQUAL "stm32g474")
    message(FATAL_ERROR "stm32_lib requires target stm32g474")
endif()

include("${JH_ROOT}/cmake/jh_board_components.cmake")
jh_apply_board_components(PROVIDER "${JH_BOARD_PROVIDER}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
include(${CMAKE_CURRENT_SOURCE_DIR}/freertos_stm32g474.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_bearssl.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_feature_build_effects.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_managed_frameworks.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_cyw43_driver.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_stack_protector.cmake)
jh_managed_framework_include_dirs(_jh_stm32_framework_include_dirs)
jh_managed_framework_configure_sources()
set(_jh_extra_hal_defines
    ${JH_BOARD_COMPILE_DEFINITIONS}
    ${EXTRA_HAL_DEFINES})
set(_jh_stm32_selection_defines
    ${_jh_extra_hal_defines}
    ${_jh_project_features}
    ${JH_RESOLVED_FEATURES})
jh_collect_feature_build_effects(_jh_stm32_build_effects
    ROOT "${JH_ROOT}"
    FEATURES ${JH_RESOLVED_FEATURES})
jh_feature_build_dependency_enabled(_jh_stm32_has_littlefs littlefs
    FEATURES ${JH_RESOLVED_FEATURES})
jh_feature_build_dependency_enabled(_jh_stm32_has_tls bearssl
    FEATURES ${JH_RESOLVED_FEATURES})
set(STM32_LITTLEFS_SOURCES
    ${_jh_stm32_build_effects_LITTLEFS_SOURCES})
set(STM32_SX126X_SOURCES
    ${_jh_stm32_build_effects_SX126X_SOURCES})

# ── HAL implementation sources (STM32G474 skeleton) ────────────────────────
file(GLOB STM32_IMPL_SOURCES ${SRC}/hal/impl/stm32g474/*.cpp)
# SoC-specific drivers under impl/stm32g474/drivers/<soc>/
file(GLOB STM32_DRIVER_SOURCES ${SRC}/hal/impl/stm32g474/drivers/*/*.cpp)
list(APPEND STM32_IMPL_SOURCES ${STM32_DRIVER_SOURCES})
# Target-independent implementations are co-located with their public headers
# in thematic directories below hal/.
file(GLOB_RECURSE STM32_SHARED_SOURCES
    ${SRC}/hal/*.cpp
    ${SRC}/hal/*.c
)
list(FILTER STM32_SHARED_SOURCES EXCLUDE REGEX "/hal/impl/")
list(FILTER STM32_SHARED_SOURCES EXCLUDE REGEX "/network/mqtt/PubSubClient/")
list(FILTER STM32_SHARED_SOURCES EXCLUDE REGEX "/bluetooth/")
list(APPEND STM32_SHARED_SOURCES
    ${SRC}/hal/bluetooth/hal_ble.cpp
    ${SRC}/hal/bluetooth/hal_ble_commands.cpp
    ${SRC}/hal/bluetooth/hal_ble_stream.cpp
)
list(APPEND STM32_IMPL_SOURCES ${STM32_SHARED_SOURCES})
list(APPEND STM32_IMPL_SOURCES
    ${_jh_stm32_build_effects_FEATURE_SOURCES}
    ${_jh_stm32_build_effects_PORTABLE_SOURCES}
    ${_jh_stm32_build_effects_DEPENDENCY_SOURCES})
list(REMOVE_DUPLICATES STM32_IMPL_SOURCES)

# ── Common entry source ─────────────────────────────────────────────────────
set(HAL_COMMON_SOURCES
    ${SRC}/hal_app_entry.cpp
)

# ── Target-independent utility sources ──────────────────────────────────────
set(UTIL_SOURCES
    ${SRC}/utils/pidController.cpp
    ${SRC}/utils/draw7Segment.cpp
)

add_library(JaszczurHAL STATIC
    ${STM32_IMPL_SOURCES}
    ${HAL_COMMON_SOURCES}
    ${UTIL_SOURCES}
    "${JH_BOARD_GENERATED_DIR}/jh_link_contract_definition.c"
)

set_source_files_properties(${STM32_LITTLEFS_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-w"
)
if(STM32_SX126X_SOURCES)
    set_source_files_properties(${STM32_SX126X_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-w")
endif()

target_include_directories(JaszczurHAL PUBLIC
    ${SRC}
    ${JH_BOARD_GENERATED_DIR}
    ${_jh_stm32_build_effects_INCLUDE_DIRS}
    ${_jh_stm32_framework_include_dirs}
)
file(REMOVE "${CMAKE_BINARY_DIR}/include/generated/jh_board_registry.h")
add_custom_command(TARGET JaszczurHAL POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_BINARY_DIR}/include/generated"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${JH_BOARD_GENERATED_DIR}/jh_board_config.h"
        "${JH_BOARD_GENERATED_DIR}/jh_link_contract.h"
        "${CMAKE_BINARY_DIR}/include/generated"
    VERBATIM)

jh_cmake_defines_contain(_jh_stm32_has_littlefs_size
    HAL_STM32_FLASH_LITTLEFS_SIZE ${_jh_stm32_selection_defines})
set(_jh_stm32_littlefs_link_size "")
if(_jh_stm32_has_littlefs)
    if(NOT _jh_stm32_has_littlefs_size)
        list(APPEND _jh_extra_hal_defines HAL_STM32_FLASH_LITTLEFS_SIZE=65536u)
        set(_jh_stm32_littlefs_link_size "65536")
    else()
        foreach(_def IN LISTS _jh_extra_hal_defines)
            if("${_def}" MATCHES "^HAL_STM32_FLASH_LITTLEFS_SIZE=(.+)$")
                set(_jh_stm32_littlefs_link_size "${CMAKE_MATCH_1}")
                string(REGEX REPLACE "[uUlL]+$" "" _jh_stm32_littlefs_link_size "${_jh_stm32_littlefs_link_size}")
            endif()
        endforeach()
    endif()
endif()

# Default STM32G474 profile for the initial skeleton backend.
#
# Opt-in flag model: modules are OFF by default; only the bus / UART /
# system-time pieces and shared HAL-level drivers that the STM32 backend
# currently provides are
# enabled here. Application projects can enable extra modules via
# EXTRA_HAL_DEFINES (e.g. -DEXTRA_HAL_DEFINES=HAL_ENABLE_MCP2515) once their
# STM32G474 backend lands.
target_compile_definitions(JaszczurHAL PUBLIC
    HAL_TARGET_STM32G474=1   # exact backend selection (see src/hal/core/hal_target.h)
    JH_TARGET_STM32G474=1    # legacy alias, kept until all refs migrate
    ${_jh_stm32_default_features}
)

if(_jh_extra_hal_defines)
    target_compile_definitions(JaszczurHAL PUBLIC ${_jh_extra_hal_defines})
endif()

jh_cmake_defines_contain(_jh_stm32_has_cyw43_gspi HAL_CYW43_BUS_STM32_GSPI
    ${_jh_stm32_selection_defines})
if(_jh_stm32_has_cyw43_gspi)
    jh_cmake_defines_contain(_jh_stm32_has_cyw43_lwip HAL_CYW43_STACK_LWIP
        ${_jh_stm32_selection_defines})
    jh_cmake_defines_contain(_jh_stm32_has_bluetooth_stage1
        JH_BLUETOOTH_STAGE1_PROBE ${_jh_stm32_selection_defines})
    jh_cmake_defines_contain(_jh_stm32_has_ble
        HAL_ENABLE_BLE ${_jh_stm32_selection_defines})
    jh_cmake_defines_contain(_jh_stm32_has_ble_stream
        HAL_ENABLE_BLE_STREAM ${_jh_stm32_selection_defines})
    jh_cmake_defines_contain(_jh_stm32_has_bluetooth_classic
        HAL_ENABLE_BLUETOOTH_CLASSIC ${_jh_stm32_selection_defines})
    jh_cmake_defines_contain(_jh_stm32_has_bluetooth_hid_host
        HAL_ENABLE_BLUETOOTH_HID_HOST ${_jh_stm32_selection_defines})
    jh_cmake_defines_contain(_jh_stm32_has_ota
        HAL_ENABLE_OTA ${_jh_stm32_selection_defines})
    jh_target_enable_cyw43_feature_stack(JaszczurHAL
        LWIP "${_jh_stm32_has_cyw43_lwip}"
        OTA "${_jh_stm32_has_ota}"
        BLUETOOTH_STAGE1 "${_jh_stm32_has_bluetooth_stage1}"
        CLASSIC "${_jh_stm32_has_bluetooth_classic}"
        HID_HOST "${_jh_stm32_has_bluetooth_hid_host}"
        BLE "${_jh_stm32_has_ble}"
        BLE_STREAM "${_jh_stm32_has_ble_stream}")
endif()

if(_jh_stm32_has_tls)
    jh_add_bearssl_source_library(jh_bearssl_stm32)
    target_link_libraries(JaszczurHAL PUBLIC jh_bearssl_stm32)
endif()

if(DEFINED HAL_PROJECT_CONFIG_DIR)
    target_include_directories(JaszczurHAL PUBLIC ${HAL_PROJECT_CONFIG_DIR})
endif()

if(DEFINED JH_STM32_FREERTOS)
    message(FATAL_ERROR
        "JH_STM32_FREERTOS has been removed as a user-facing switch. "
        "Use -DEXTRA_HAL_DEFINES=HAL_ENABLE_FREERTOS instead.")
endif()

jh_cmake_defines_contain(_jh_stm32_freertos_enabled HAL_ENABLE_FREERTOS
    ${_jh_stm32_selection_defines})
jh_cmake_defines_contain(_jh_stm32_stack_protector_enabled
    HAL_ENABLE_STACK_PROTECTOR ${_jh_stm32_selection_defines})

if(_jh_stm32_freertos_enabled)
    jh_stm32g474_enable_freertos(JaszczurHAL)
endif()

if(NOT "${_jh_stm32_littlefs_link_size}" STREQUAL "")
    target_link_options(JaszczurHAL PUBLIC
        "-Wl,--defsym=HAL_STM32_FLASH_LITTLEFS_SIZE=${_jh_stm32_littlefs_link_size}"
    )
endif()

set(_common_flags
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Wall
    -Wextra
    -Werror=return-type
    -pipe
)

target_compile_options(JaszczurHAL PRIVATE
    ${_common_flags}
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
)

if(_jh_stm32_stack_protector_enabled)
    # Preserve the usage requirement for consumers that link this target in
    # the same CMake build. Installed archive consumers must pass the option
    # recorded in the feature documentation explicitly.
    jh_target_enable_stack_protector(JaszczurHAL PUBLIC)
    jh_stack_protector_disable_sources(
        "${SRC}/hal/system/jh_stack_protector.c"
        "${SRC}/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.cpp"
        "${SRC}/hal/impl/stm32g474/freertos/freertos_hooks.c")
endif()

install(TARGETS JaszczurHAL ARCHIVE DESTINATION lib)
install(DIRECTORY ${SRC}/ DESTINATION include
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "impl/.mock" EXCLUDE)
install(CODE [[
    file(REMOVE
        "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/include/generated/jh_board_registry.h")
]])
install(FILES
        "${JH_BOARD_GENERATED_DIR}/jh_board_config.h"
        "${JH_BOARD_GENERATED_DIR}/jh_link_contract.h"
        DESTINATION include/generated)
install(FILES
        "${JH_BOARD_GENERATED_DIR}/jh_link_contract_reference.c"
        "${JH_BOARD_GENERATED_DIR}/jh_board_resolved.json"
        DESTINATION share/JaszczurHAL/generated)
