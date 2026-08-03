include_guard(GLOBAL)

if(NOT DEFINED JH_ROOT OR NOT IS_DIRECTORY "${JH_ROOT}/src")
    message(FATAL_ERROR "JH_ROOT must point to the JaszczurHAL repository")
endif()
if(NOT DEFINED JH_RP_TARGET_DEFINE OR "${JH_RP_TARGET_DEFINE}" STREQUAL "")
    message(FATAL_ERROR "JH_RP_TARGET_DEFINE must select an RP target")
endif()
if(JH_RP_TARGET_DEFINE STREQUAL "HAL_TARGET_RP2040")
    set(JH_RP_TARGET_NAME "rp2040")
elseif(JH_RP_TARGET_DEFINE STREQUAL "HAL_TARGET_RP2350_ARM")
    set(JH_RP_TARGET_NAME "rp2350-arm")
elseif(JH_RP_TARGET_DEFINE STREQUAL "HAL_TARGET_RP2350_RISCV")
    set(JH_RP_TARGET_NAME "rp2350-riscv")
else()
    message(FATAL_ERROR
        "Unknown native RP target define: ${JH_RP_TARGET_DEFINE}")
endif()

pico_sdk_init()

set(SRC "${JH_ROOT}/src")
include("${JH_ROOT}/cmake/jh_cyw43_driver.cmake")
include("${JH_ROOT}/cmake/jh_rp_hal_sources.cmake")
include("${JH_ROOT}/cmake/jh_bearssl.cmake")
include("${JH_ROOT}/cmake/jh_managed_frameworks.cmake")
include("${JH_ROOT}/cmake/jh_littlefs.cmake")
jh_bearssl_source_manifest(
    _jh_native_bearssl_sources
    _jh_native_bearssl_include_dirs)
jh_managed_framework_include_dirs(_jh_native_framework_include_dirs)
jh_managed_framework_configure_sources()

jh_hal_define_enabled(_jh_native_eeprom HAL_ENABLE_EEPROM)
jh_hal_define_enabled(_jh_native_kv HAL_ENABLE_KV)
jh_hal_define_enabled(_jh_native_sdlogger HAL_ENABLE_SDLOGGER)
if(_jh_native_kv OR _jh_native_sdlogger)
    set(_jh_native_eeprom TRUE)
endif()
jh_hal_define_enabled(_jh_native_littlefs HAL_ENABLE_LITTLEFS)
jh_hal_define_enabled(_jh_native_ota HAL_ENABLE_OTA)
if(_jh_native_ota AND JH_RP_TARGET_NAME STREQUAL "rp2350-riscv")
    message(FATAL_ERROR
        "HAL_ENABLE_OTA is not supported for rp2350-riscv; "
        "use rp2040 or rp2350-arm")
endif()
jh_hal_define_value(_jh_native_eeprom_size HAL_RP_FLASH_EEPROM_SIZE)
jh_hal_define_value(_jh_native_littlefs_size HAL_RP_FLASH_LITTLEFS_SIZE)

if(NOT _jh_native_eeprom_size)
    set(_jh_native_eeprom_size 4096)
endif()
if(_jh_native_littlefs AND NOT _jh_native_littlefs_size)
    set(_jh_native_littlefs_size 65536)
elseif(NOT _jh_native_littlefs_size)
    set(_jh_native_littlefs_size 0)
endif()

if(_jh_native_eeprom OR _jh_native_littlefs)
    set(_jh_native_eeprom_reservation "${_jh_native_eeprom_size}")
else()
    set(_jh_native_eeprom_reservation 0)
endif()
math(EXPR _jh_native_storage_reservation
    "${_jh_native_eeprom_reservation} + ${_jh_native_littlefs_size}")
math(EXPR _jh_native_physical_flash_size "${PICO_FLASH_SIZE_BYTES}")
set(_jh_native_ota_boot_size 0)
set(_jh_native_ota_control_size 0)
set(_jh_native_ota_slot_size 0)
set(_jh_native_program_offset 0)
set(_jh_native_staging_offset 0)
set(_jh_native_ota_phase_offset 0)
set(_jh_native_ota_scratch_offset 0)
set(_jh_native_ota_state_a_offset 0)
set(_jh_native_ota_state_b_offset 0)
if(_jh_native_ota)
    set(_jh_native_ota_boot_size 16384)
    set(_jh_native_ota_control_size 16384)
    math(EXPR _jh_native_ota_slots_available
        "${_jh_native_physical_flash_size} - ${_jh_native_storage_reservation} - ${_jh_native_ota_boot_size} - ${_jh_native_ota_control_size}")
    if(_jh_native_ota_slots_available LESS 8192)
        message(FATAL_ERROR
            "RP flash is too small for OTA boot/control regions and two "
            "sector-aligned firmware slots")
    endif()
    math(EXPR _jh_native_firmware_flash_size
        "(${_jh_native_ota_slots_available} / 8192) * 4096")
    set(_jh_native_ota_slot_size "${_jh_native_firmware_flash_size}")
    set(_jh_native_program_offset "${_jh_native_ota_boot_size}")
    math(EXPR _jh_native_staging_offset
        "${_jh_native_program_offset} + ${_jh_native_firmware_flash_size}")
    math(EXPR _jh_native_ota_phase_offset
        "${_jh_native_staging_offset} + ${_jh_native_firmware_flash_size}")
    math(EXPR _jh_native_ota_scratch_offset
        "${_jh_native_ota_phase_offset} + 4096")
    math(EXPR _jh_native_ota_state_a_offset
        "${_jh_native_ota_scratch_offset} + 4096")
    math(EXPR _jh_native_ota_state_b_offset
        "${_jh_native_ota_state_a_offset} + 4096")
    math(EXPR _jh_native_ota_control_end
        "${_jh_native_ota_state_b_offset} + 4096")
    math(EXPR _jh_native_storage_begin
        "${_jh_native_physical_flash_size} - ${_jh_native_storage_reservation}")
    if(_jh_native_ota_control_end GREATER _jh_native_storage_begin)
        message(FATAL_ERROR
            "RP OTA program/staging/control layout overlaps EEPROM/LittleFS")
    endif()
else()
    math(EXPR _jh_native_firmware_flash_size
        "${_jh_native_physical_flash_size} - ${_jh_native_storage_reservation}")
endif()
if(_jh_native_firmware_flash_size LESS_EQUAL 0)
    message(FATAL_ERROR
        "RP EEPROM/LittleFS reservations exceed physical flash")
endif()
foreach(_storage_size IN ITEMS
        _jh_native_eeprom_reservation
        _jh_native_littlefs_size)
    math(EXPR _storage_remainder "${${_storage_size}} % 4096")
    if(NOT _storage_remainder EQUAL 0)
        message(FATAL_ERROR
            "${_storage_size} must be a multiple of the RP flash sector size "
            "(4096 bytes)")
    endif()
endforeach()
if(_jh_native_ota)
    foreach(_ota_value IN ITEMS
            _jh_native_ota_boot_size
            _jh_native_program_offset
            _jh_native_firmware_flash_size
            _jh_native_staging_offset
            _jh_native_ota_phase_offset
            _jh_native_ota_scratch_offset
            _jh_native_ota_state_a_offset
            _jh_native_ota_state_b_offset)
        math(EXPR _ota_remainder "${${_ota_value}} % 4096")
        if(NOT _ota_remainder EQUAL 0)
            message(FATAL_ERROR
                "${_ota_value} must be aligned to the RP flash sector size "
                "(4096 bytes)")
        endif()
    endforeach()
endif()

jh_collect_rp_hal_sources(JH_RP_HAL_SOURCES "${SRC}" EXCLUDE_APP_ENTRY)
add_library(JaszczurHAL STATIC ${JH_RP_HAL_SOURCES})
if(_jh_native_littlefs)
    jh_littlefs_source_manifest(
        _jh_native_littlefs_sources
        _jh_native_littlefs_include_dirs)
    target_sources(JaszczurHAL PRIVATE ${_jh_native_littlefs_sources})
    target_include_directories(JaszczurHAL PRIVATE
        ${_jh_native_littlefs_include_dirs})
    set_source_files_properties(${_jh_native_littlefs_sources}
        PROPERTIES COMPILE_DEFINITIONS LFS_NO_ASSERT)
endif()

target_include_directories(JaszczurHAL PUBLIC
    "${SRC}"
    "${SRC}/hal/impl/rp2040/drivers/usb"
    ${_jh_native_bearssl_include_dirs}
    ${_jh_native_framework_include_dirs})
if(DEFINED HAL_PROJECT_CONFIG_DIR)
    target_include_directories(JaszczurHAL PUBLIC ${HAL_PROJECT_CONFIG_DIR})
endif()

target_compile_definitions(JaszczurHAL PUBLIC
    ${JH_RP_TARGET_DEFINE}=1
    HAL_RP_FLASH_EEPROM_SIZE=${_jh_native_eeprom_reservation}u
    HAL_RP_FLASH_LITTLEFS_SIZE=${_jh_native_littlefs_size}u
    HAL_RP_OTA_BOOT_SIZE=${_jh_native_ota_boot_size}u
    HAL_RP_OTA_PROGRAM_OFFSET=${_jh_native_program_offset}u
    HAL_RP_OTA_SLOT_SIZE=${_jh_native_ota_slot_size}u
    HAL_RP_OTA_STAGING_OFFSET=${_jh_native_staging_offset}u
    HAL_RP_OTA_PHASE_OFFSET=${_jh_native_ota_phase_offset}u
    HAL_RP_OTA_SCRATCH_OFFSET=${_jh_native_ota_scratch_offset}u
    HAL_RP_OTA_STATE_A_OFFSET=${_jh_native_ota_state_a_offset}u
    HAL_RP_OTA_STATE_B_OFFSET=${_jh_native_ota_state_b_offset}u
    PICO_FLASH_ASSERT_ON_UNSAFE=0
    ${JH_RP_BOARD_DEFINES}
)
if(DEFINED JH_USB_MANUFACTURER AND NOT "${JH_USB_MANUFACTURER}" STREQUAL "")
    target_compile_definitions(JaszczurHAL PRIVATE
        "JH_USB_MANUFACTURER=\"${JH_USB_MANUFACTURER}\"")
endif()
if(DEFINED JH_USB_PRODUCT AND NOT "${JH_USB_PRODUCT}" STREQUAL "")
    target_compile_definitions(JaszczurHAL PRIVATE
        "JH_USB_PRODUCT=\"${JH_USB_PRODUCT}\"")
endif()
if(DEFINED EXTRA_HAL_DEFINES)
    target_compile_definitions(JaszczurHAL PUBLIC ${EXTRA_HAL_DEFINES})
endif()
jh_hal_define_value(_jh_core0_stack_size HAL_RP2040_STACK_SIZE)
if(_jh_core0_stack_size)
    target_compile_definitions(JaszczurHAL PUBLIC
        PICO_STACK_SIZE=${_jh_core0_stack_size})
endif()
jh_hal_define_value(_jh_core1_stack_size HAL_RP2040_CORE1_STACK_SIZE)
if(_jh_core1_stack_size)
    target_compile_definitions(JaszczurHAL PUBLIC
        PICO_CORE1_STACK_SIZE=${_jh_core1_stack_size})
endif()

target_compile_options(JaszczurHAL PRIVATE
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_link_libraries(JaszczurHAL PUBLIC
    pico_stdlib
    pico_bootrom
    pico_flash
    pico_multicore
    pico_rand
    pico_unique_id
    hardware_adc
    hardware_clocks
    hardware_dma
    hardware_flash
    hardware_gpio
    hardware_i2c
    hardware_irq
    hardware_pio
    hardware_pwm
    hardware_spi
    hardware_sync
    hardware_timer
    hardware_uart
    hardware_watchdog
)

jh_hal_define_enabled(_jh_native_tls HAL_ENABLE_TLS)
if(_jh_native_tls)
    jh_add_bearssl_source_library(jh_bearssl_rp_native)
    target_link_libraries(jh_bearssl_rp_native PRIVATE pico_stdlib)
    target_link_libraries(JaszczurHAL PUBLIC jh_bearssl_rp_native)
endif()

jh_hal_define_enabled(_jh_native_freertos HAL_ENABLE_FREERTOS)
set(_jh_native_core1_active FALSE)
if(_jh_native_freertos)
    jh_hal_define_value(_jh_freertos_core_count HAL_FREERTOS_CORE_COUNT)
    if(NOT _jh_freertos_core_count)
        set(_jh_freertos_core_count "2")
    endif()
    if(NOT "${_jh_freertos_core_count}" MATCHES "^1[uUlL]*$")
        set(_jh_native_core1_active TRUE)
    endif()
    include("${JH_ROOT}/cmake/freertos_rp.cmake")
    jh_rp_enable_freertos(JaszczurHAL)
else()
    jh_hal_define_enabled(_jh_native_app_task1 HAL_ENABLE_APP_TASK1)
    if(_jh_native_app_task1)
        set(_jh_native_core1_active TRUE)
    endif()
endif()
if(NOT _jh_native_core1_active)
    target_compile_definitions(JaszczurHAL PUBLIC
        PICO_FLASH_ASSUME_CORE1_SAFE=1)
endif()

if(NOT TARGET tinyusb_device)
    message(FATAL_ERROR
        "Native RP USB requires the Pico SDK TinyUSB device submodule")
endif()
target_link_libraries(JaszczurHAL PUBLIC tinyusb_device)

jh_hal_define_enabled(_jh_native_cyw43_backend HAL_NETWORK_BACKEND_CYW43)
if(_jh_native_cyw43_backend)
    jh_target_enable_cyw43_driver(JaszczurHAL LWIP)
elseif(_jh_pico_board_has_cyw43)
    target_compile_definitions(JaszczurHAL PUBLIC
        JH_RP_CYW43_LED_ONLY=1
        HAL_CYW43_BUS_PICO_PIO=1
        HAL_CYW43_MAX_TRANSACTION_BYTES=2048u)
    jh_target_enable_cyw43_driver(JaszczurHAL)
endif()

function(jh_add_rp_ota_boot_target BOOT_TARGET)
    add_executable("${BOOT_TARGET}"
        "${SRC}/hal/impl/rp2040/ota/rp_ota_boot.cpp"
        "${SRC}/hal/impl/shared/network/ota/jh_ota_image.cpp"
        "${SRC}/hal/impl/shared/network/ota/jh_ota_swap_engine.cpp"
        "${SRC}/hal/hal_crypto.cpp"
        "${SRC}/hal/hal_crc.cpp")
    target_include_directories("${BOOT_TARGET}" PRIVATE
        "${SRC}"
        "${JH_BOARD_GENERATED_DIR}")
    target_compile_definitions("${BOOT_TARGET}" PRIVATE
        ${JH_RP_TARGET_DEFINE}=1
        ${JH_RP_BOARD_DEFINES}
        JH_RP_OTA_BOOT_IMAGE=1
        HAL_ENABLE_CRYPTO=1
        HAL_ENABLE_CRC=1
        HAL_RP_OTA_PROGRAM_OFFSET=${_jh_native_program_offset}u
        HAL_RP_OTA_SLOT_SIZE=${_jh_native_ota_slot_size}u
        HAL_RP_OTA_STAGING_OFFSET=${_jh_native_staging_offset}u
        HAL_RP_OTA_PHASE_OFFSET=${_jh_native_ota_phase_offset}u
        HAL_RP_OTA_SCRATCH_OFFSET=${_jh_native_ota_scratch_offset}u
        HAL_RP_OTA_STATE_A_OFFSET=${_jh_native_ota_state_a_offset}u
        HAL_RP_OTA_STATE_B_OFFSET=${_jh_native_ota_state_b_offset}u)
    target_compile_options("${BOOT_TARGET}" PRIVATE
        -Os -ffunction-sections -fdata-sections
        -Wall -Wextra -Werror
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    target_link_options("${BOOT_TARGET}" PRIVATE
        -Wl,--gc-sections)
    target_link_libraries("${BOOT_TARGET}" PRIVATE
        pico_stdlib
        pico_bootrom
        hardware_flash
        hardware_sync
        hardware_watchdog)
    set_target_properties("${BOOT_TARGET}" PROPERTIES LINKER_LANGUAGE CXX)
    pico_set_binary_type("${BOOT_TARGET}" copy_to_ram)

    set(_jh_boot_linker_source
        "${PICO_LINKER_SCRIPT_PATH}/memmap_copy_to_ram.ld")
    if(NOT EXISTS "${_jh_boot_linker_source}")
        message(FATAL_ERROR
            "Pico SDK copy-to-RAM linker script not found: "
            "${_jh_boot_linker_source}")
    endif()
    file(READ "${_jh_boot_linker_source}" _jh_boot_linker_contents)
    string(REPLACE
        "INCLUDE \"pico_flash_region.ld\""
        "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = ${_jh_native_ota_boot_size}"
        _jh_boot_linker_contents "${_jh_boot_linker_contents}")
    set(_jh_boot_linker_script
        "${CMAKE_CURRENT_BINARY_DIR}/${BOOT_TARGET}.ld")
    file(WRITE "${_jh_boot_linker_script}" "${_jh_boot_linker_contents}")
    pico_set_linker_script("${BOOT_TARGET}" "${_jh_boot_linker_script}")
    pico_enable_stdio_uart("${BOOT_TARGET}" 0)
    pico_enable_stdio_usb("${BOOT_TARGET}" 0)
    pico_add_extra_outputs("${BOOT_TARGET}")
endfunction()

function(jh_add_rp_native_firmware TARGET_NAME)
    cmake_parse_arguments(JH_NATIVE "CUSTOM_ENTRY" "" "" ${ARGN})
    if(NOT TARGET "${TARGET_NAME}")
        message(FATAL_ERROR
            "jh_add_rp_native_firmware: target '${TARGET_NAME}' does not exist")
    endif()
    target_link_libraries("${TARGET_NAME}" PRIVATE JaszczurHAL)
    target_compile_options("${TARGET_NAME}" PRIVATE -Wall -Wextra -Werror)
    set_target_properties("${TARGET_NAME}" PROPERTIES LINKER_LANGUAGE CXX)
    if(NOT JH_NATIVE_CUSTOM_ENTRY)
        target_sources("${TARGET_NAME}" PRIVATE "${SRC}/hal_app_entry.cpp")
        target_compile_definitions("${TARGET_NAME}" PRIVATE
            HAL_PROVIDE_APP_ENTRY=1)
    endif()
    if(_jh_native_storage_reservation GREATER 0 OR _jh_native_ota)
        set(_jh_default_linker_script
            "${PICO_LINKER_SCRIPT_PATH}/memmap_default.ld")
        if(NOT EXISTS "${_jh_default_linker_script}")
            message(FATAL_ERROR
                "Pico SDK default linker script not found: "
                "${_jh_default_linker_script}")
        endif()
        file(READ "${_jh_default_linker_script}" _jh_linker_contents)
        string(REPLACE
            "INCLUDE \"pico_flash_region.ld\""
            "FLASH(rx) : ORIGIN = 0x10000000 + ${_jh_native_program_offset}, LENGTH = ${_jh_native_firmware_flash_size}"
            _jh_linker_contents "${_jh_linker_contents}")
        set(_jh_storage_linker_script
            "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_storage.ld")
        file(WRITE "${_jh_storage_linker_script}" "${_jh_linker_contents}")
        pico_set_linker_script("${TARGET_NAME}" "${_jh_storage_linker_script}")
    endif()
    pico_enable_stdio_uart("${TARGET_NAME}" 0)
    pico_enable_stdio_usb("${TARGET_NAME}" 0)
    # The Pico SDK derives the UF2 family from PICO_PLATFORM. The linked ELF
    # already carries the offset OTA application's absolute flash addresses.
    pico_add_extra_outputs("${TARGET_NAME}")
    if(_jh_native_ota)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(_jh_boot_target "${TARGET_NAME}_ota_boot")
        jh_add_rp_ota_boot_target("${_jh_boot_target}")
        add_dependencies("${TARGET_NAME}" "${_jh_boot_target}")
        if(NOT DEFINED JH_OTA_GENERATION)
            set(JH_OTA_GENERATION 1)
        endif()
        if(NOT DEFINED JH_OTA_VERSION OR "${JH_OTA_VERSION}" STREQUAL "")
            set(JH_OTA_VERSION "dev")
        endif()
        add_custom_command(TARGET "${TARGET_NAME}" POST_BUILD
            COMMAND "${Python3_EXECUTABLE}"
                "${JH_ROOT}/scripts/rp_ota_artifacts.py"
                package
                --binary "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.bin"
                --output "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ota"
                --target "${JH_RP_TARGET_NAME}"
                --program-offset "${_jh_native_program_offset}"
                --generation "${JH_OTA_GENERATION}"
                --version "${JH_OTA_VERSION}"
            COMMAND "${Python3_EXECUTABLE}"
                "${JH_ROOT}/scripts/rp_ota_artifacts.py"
                merge-uf2
                --boot "${CMAKE_CURRENT_BINARY_DIR}/${_jh_boot_target}.uf2"
                --application "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.uf2"
                --output "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.uf2"
            VERBATIM)
    endif()
endfunction()
