if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_cmake "${JH_ROOT}/rp_native_lib/CMakeLists.txt")
set(_native_common "${JH_ROOT}/cmake/jh_rp_native_sdk.cmake")
set(_dispatcher "${JH_ROOT}/cmake/jh_firmware_project/CMakeLists.txt")
set(_native_recipe "${JH_ROOT}/cmake/targets/rp-native.cmake")
set(_probe "${JH_ROOT}/rp_native_lib/artifact_probe.cpp")
set(_core1_probe "${JH_ROOT}/rp_native_lib/core1_probe.c")
set(_script "${JH_ROOT}/scripts/build_rp_native_lib.sh")
set(_board_generator "${JH_ROOT}/scripts/generate_board_config.py")
set(_board_cmake "${JH_ROOT}/cmake/jh_board_profiles.cmake")
set(_sources "${JH_ROOT}/cmake/jh_rp_hal_sources.cmake")
set(_app_entry "${JH_ROOT}/src/hal_app_entry.cpp")
set(_core_runtime "${JH_ROOT}/examples/01_core_runtime/app.cpp")
set(_usb_header "${JH_ROOT}/src/hal/hal_usb.h")
set(_usb_impl "${JH_ROOT}/src/hal/impl/rp2040/hal_usb.cpp")
set(_flash_engine_header
    "${JH_ROOT}/src/hal/impl/shared/drivers/flash/jh_flash_transaction_engine.h")
set(_flash_engine
    "${JH_ROOT}/src/hal/impl/shared/drivers/flash/jh_flash_transaction_engine.cpp")
set(_flash_header
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/flash/rp_flash_transaction.h")
set(_flash_impl
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/flash/rp_flash_transaction.cpp")
set(_flash_storage_header
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/flash/rp_flash_storage.h")
set(_flash_storage
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/flash/rp_flash_storage.cpp")
set(_flash_runtime
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/flash/rp_flash_runtime.h")
set(_rp_eeprom "${JH_ROOT}/src/hal/impl/rp2040/hal_eeprom.cpp")
set(_rp_littlefs "${JH_ROOT}/src/hal/impl/rp2040/hal_littlefs.cpp")
set(_littlefs_core
    "${JH_ROOT}/third_party/littlefs/lfs.c")
set(_littlefs_cmake "${JH_ROOT}/cmake/jh_littlefs.cmake")
set(_flash_probe "${JH_ROOT}/tests/hardware/rp_flash_transaction/app.c")
set(_flash_verifier
    "${JH_ROOT}/tests/hardware/rp_flash_transaction/verify_flash_transaction.py")
set(_usb_descriptors
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/usb/rp_usb_descriptors.c")
set(_tusb_config
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/usb/tusb_config.h")
set(_serial_impl "${JH_ROOT}/src/hal/impl/rp2040/hal_serial.cpp")
set(_freertos_cmake "${JH_ROOT}/cmake/freertos_rp.cmake")
set(_freertos_config
    "${JH_ROOT}/src/hal/impl/rp2040/freertos/FreeRTOSConfig.h")
set(_freertos_hooks
    "${JH_ROOT}/src/hal/impl/rp2040/freertos/freertos_hooks.c")
set(_freertos_riscv_tick
    "${JH_ROOT}/src/hal/impl/rp2040/freertos/rp2350_riscv_tick.c")
set(_freertos_probe "${JH_ROOT}/tests/hardware/rp_freertos_smp/app.c")
set(_freertos_verifier
    "${JH_ROOT}/tests/hardware/rp_freertos_smp/verify_freertos_smp.py")
set(_ota_probe "${JH_ROOT}/tests/hardware/rp_ota/app.c")
set(_ota_verifier "${JH_ROOT}/tests/hardware/rp_ota/verify_ota.py")
set(_ota_boot
    "${JH_ROOT}/src/hal/impl/rp2040/ota/rp_ota_boot.cpp")
set(_ota_manifest
    "${JH_ROOT}/tests/hardware/rp_ota/.vscode/jaszczurhal.project.json")
set(_ota_config
    "${JH_ROOT}/tests/hardware/rp_ota/hal_project_config.h")

foreach(_file IN ITEMS
        "${_cmake}" "${_native_common}" "${_dispatcher}" "${_native_recipe}"
        "${_probe}" "${_core1_probe}" "${_script}" "${_board_generator}"
        "${_board_cmake}" "${_sources}"
        "${_app_entry}" "${_core_runtime}" "${_usb_header}"
        "${_usb_impl}" "${_flash_engine_header}" "${_flash_engine}"
        "${_flash_header}" "${_flash_impl}" "${_flash_storage_header}"
        "${_flash_storage}" "${_flash_runtime}" "${_rp_eeprom}"
        "${_rp_littlefs}" "${_littlefs_core}" "${_littlefs_cmake}"
        "${_flash_probe}" "${_flash_verifier}"
        "${_usb_descriptors}" "${_tusb_config}"
        "${_serial_impl}" "${_freertos_cmake}" "${_freertos_config}"
        "${_freertos_hooks}" "${_freertos_riscv_tick}" "${_freertos_probe}"
        "${_freertos_verifier}" "${_ota_probe}" "${_ota_verifier}"
        "${_ota_boot}" "${_ota_manifest}" "${_ota_config}")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "Native RP build contract file is missing: ${_file}")
    endif()
endforeach()

file(READ "${_cmake}" _cmake_text)
file(READ "${_native_common}" _native_common_text)
file(READ "${_dispatcher}" _dispatcher_text)
file(READ "${_native_recipe}" _native_recipe_text)
file(READ "${_probe}" _probe_text)
file(READ "${_core1_probe}" _core1_probe_text)
file(READ "${_script}" _script_text)
file(READ "${_board_generator}" _board_generator_text)
file(READ "${_board_cmake}" _board_cmake_text)
file(READ "${_sources}" _sources_text)
file(READ "${_app_entry}" _app_entry_text)
file(READ "${_core_runtime}" _core_runtime_text)
file(READ "${_usb_header}" _usb_header_text)
file(READ "${_usb_impl}" _usb_impl_text)
file(READ "${_flash_engine_header}" _flash_engine_header_text)
file(READ "${_flash_engine}" _flash_engine_text)
file(READ "${_flash_header}" _flash_header_text)
file(READ "${_flash_impl}" _flash_impl_text)
file(READ "${_flash_storage_header}" _flash_storage_header_text)
file(READ "${_flash_storage}" _flash_storage_text)
file(READ "${_flash_runtime}" _flash_runtime_text)
file(READ "${_rp_eeprom}" _rp_eeprom_text)
file(READ "${_rp_littlefs}" _rp_littlefs_text)
file(READ "${_littlefs_core}" _littlefs_core_text)
file(READ "${_flash_probe}" _flash_probe_text)
file(READ "${_flash_verifier}" _flash_verifier_text)
file(READ "${_usb_descriptors}" _usb_descriptors_text)
file(READ "${_tusb_config}" _tusb_config_text)
file(READ "${_serial_impl}" _serial_impl_text)
file(READ "${_freertos_cmake}" _freertos_cmake_text)
file(READ "${_freertos_config}" _freertos_config_text)
file(READ "${_freertos_hooks}" _freertos_hooks_text)
file(READ "${_freertos_riscv_tick}" _freertos_riscv_tick_text)
file(READ "${_freertos_probe}" _freertos_probe_text)
file(READ "${_freertos_verifier}" _freertos_verifier_text)
file(READ "${_ota_probe}" _ota_probe_text)
file(READ "${_ota_verifier}" _ota_verifier_text)
file(READ "${_ota_boot}" _ota_boot_text)
file(READ "${_ota_manifest}" _ota_manifest_text)
file(READ "${_ota_config}" _ota_config_text)
set(_native_text
    "${_cmake_text}\n${_native_common_text}\n${_native_recipe_text}")

foreach(_target IN ITEMS rp2040 rp2350-arm rp2350-riscv)
    string(FIND "${_cmake_text}" "JH_TARGET STREQUAL \"${_target}\""
        _target_at)
    if(_target_at EQUAL -1)
        message(FATAL_ERROR
            "rp_native_lib does not map JH_TARGET=${_target}")
    endif()
    string(FIND "${_script_text}" "${_target})" _script_target_at)
    if(_script_target_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP build script does not accept ${_target}")
    endif()
endforeach()

foreach(_define IN ITEMS
        HAL_TARGET_RP2040 HAL_TARGET_RP2350_ARM HAL_TARGET_RP2350_RISCV)
    string(FIND "${_native_text}" "${_define}" _define_at)
    if(_define_at EQUAL -1)
        message(FATAL_ERROR "rp_native_lib is missing ${_define}")
    endif()
endforeach()

foreach(_sdk_contract IN ITEMS
        "pico_sdk_init()"
        "pico_stdlib"
        "hardware_flash"
        "pico_multicore"
        "tinyusb_device"
        "pico_add_extra_outputs")
    string(FIND "${_native_text}" "${_sdk_contract}" _sdk_at)
    if(_sdk_at EQUAL -1)
        message(FATAL_ERROR
            "rp_native_lib is missing Pico SDK contract: ${_sdk_contract}")
    endif()
endforeach()

foreach(_usb_contract IN ITEMS
        "hal_usb_init"
        "hal_usb_cdc_write"
        "hal_usb_cdc_flush"
        "HAL_USB_BOOTLOADER_TOUCH_BAUD")
    string(FIND
        "${_usb_header_text}\n${_usb_impl_text}"
        "${_usb_contract}" _usb_contract_at)
    if(_usb_contract_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP USB contract is missing: ${_usb_contract}")
    endif()
endforeach()

foreach(_usb_impl_contract IN ITEMS
        "tusb_init"
        "add_repeating_timer_us"
        "hal_mutex_try_lock"
        "xTaskCreateAffinitySet"
        "usb_worker_task"
        "tud_cdc_line_coding_cb"
        "tud_cdc_line_state_cb"
        "reset_usb_boot")
    string(FIND "${_usb_impl_text}" "${_usb_impl_contract}" _usb_impl_at)
    if(_usb_impl_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP USB implementation is missing: ${_usb_impl_contract}")
    endif()
endforeach()

foreach(_flash_build_contract IN ITEMS
        "pico_flash"
        "PICO_FLASH_ASSERT_ON_UNSAFE=0"
        "PICO_FLASH_ASSUME_CORE1_SAFE=1"
        "HAL_RP_FLASH_EEPROM_SIZE"
        "HAL_RP_FLASH_LITTLEFS_SIZE"
        "NDEBUG"
        "pico_set_linker_script"
        "_storage.ld")
    string(FIND "${_native_common_text}" "${_flash_build_contract}"
        _flash_build_at)
    if(_flash_build_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP flash build contract is missing: "
            "${_flash_build_contract}")
    endif()
endforeach()

foreach(_storage_contract IN ITEMS
        "jh_rp_flash_storage_partition"
        "jh_rp_flash_storage_program"
        "jh_rp_flash_storage_erase"
        "jh_rp_flash_storage_replace"
        "jh_rp_flash_transaction_execute"
        "flash_range_erase"
        "flash_range_program"
        "__no_inline_not_in_flash_func")
    string(FIND
        "${_flash_storage_header_text}\n${_flash_storage_text}"
        "${_storage_contract}" _storage_at)
    if(_storage_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP flash storage contract is missing: ${_storage_contract}")
    endif()
endforeach()

foreach(_storage_client_contract IN ITEMS
        "jh_rp_flash_storage_replace"
        "JH_RP_FLASH_PARTITION_EEPROM")
    string(FIND "${_rp_eeprom_text}" "${_storage_client_contract}"
        _eeprom_storage_at)
    if(_eeprom_storage_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP EEPROM storage contract is missing: "
            "${_storage_client_contract}")
    endif()
endforeach()

foreach(_storage_client_contract IN ITEMS
        "jh_rp_flash_storage_program"
        "jh_rp_flash_storage_erase"
        "JH_RP_FLASH_PARTITION_LITTLEFS"
        "lfs_mount"
        "lfs_format")
    string(FIND "${_rp_littlefs_text}" "${_storage_client_contract}"
        _littlefs_storage_at)
    if(_littlefs_storage_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP LittleFS storage contract is missing: "
            "${_storage_client_contract}")
    endif()
endforeach()

foreach(_direct_flash_call IN ITEMS flash_range_erase flash_range_program)
    string(FIND
        "${_rp_eeprom_text}\n${_rp_littlefs_text}"
        "${_direct_flash_call}" _direct_flash_at)
    if(NOT _direct_flash_at EQUAL -1)
        message(FATAL_ERROR
            "RP storage clients bypass the flash coordinator with "
            "${_direct_flash_call}")
    endif()
endforeach()

foreach(_flash_engine_contract IN ITEMS
        "jh_flash_transaction_engine_execute"
        "backend->acquire"
        "backend->quiesce"
        "backend->execute"
        "backend->resume"
        "backend->release")
    string(FIND
        "${_flash_engine_header_text}\n${_flash_engine_text}"
        "${_flash_engine_contract}" _flash_engine_at)
    if(_flash_engine_at EQUAL -1)
        message(FATAL_ERROR
            "Shared flash transaction engine is missing: "
            "${_flash_engine_contract}")
    endif()
endforeach()

foreach(_flash_impl_contract IN ITEMS
        "jh_rp_flash_transaction_core_init"
        "jh_rp_flash_transaction_execute"
        "flash_safe_execute_core_init"
        "flash_safe_execute"
        "NUM_DMA_CHANNELS"
        "DMA_CH0_CTRL_TRIG_BUSY_BITS"
        "__no_inline_not_in_flash_func"
        "jh_rp_usb_flash_quiesce"
        "jh_rp_usb_flash_resume"
        "HAL_ETIMEOUT"
        "HAL_EBUSY"
        "HAL_ESTATE")
    string(FIND
        "${_flash_header_text}\n${_flash_impl_text}\n${_flash_runtime_text}\n${_usb_impl_text}"
        "${_flash_impl_contract}" _flash_impl_at)
    if(_flash_impl_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP flash transaction contract is missing: "
            "${_flash_impl_contract}")
    endif()
endforeach()

foreach(_flash_probe_contract IN ITEMS
        "app_task1"
        "flash_range_erase"
        "flash_range_program"
        "dma_channel_is_busy"
        "flash_resident_operation"
        "recursive_operation"
        "JHFLASH-RESULT"
        "\"status\": \"pass\"")
    string(FIND
        "${_flash_probe_text}\n${_flash_verifier_text}"
        "${_flash_probe_contract}" _flash_probe_at)
    if(_flash_probe_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP flash hardware probe is missing: "
            "${_flash_probe_contract}")
    endif()
endforeach()

foreach(_ota_hardware_contract IN ITEMS
        "JHOTA-HW1"
        "HAL_BOARD_PROFILE_NAME"
        "hal_wifi_get_local_ip_ex"
        "\"board\""
        "\"ipv4\""
        "JH_OTA_TEST_WIFI_PASSWORD, false"
        "s_last_connect_ms = hal_millis()"
        "hal_ota_get_boot_info_ex"
        "hal_ota_confirm_boot_ex"
        "jh-ota-rp2040"
        "jh-ota-rp2350-arm"
        "--board"
        "pico-rm2"
        "wrongPassword"
        "rollbackBoots"
        "HAL_ENABLE_FREERTOS"
        "HAL_FREERTOS_TASK0_STACK 2048u"
        "passwordEnv")
    string(FIND
        "${_ota_probe_text}\n${_ota_verifier_text}\n${_ota_manifest_text}\n${_ota_config_text}"
        "${_ota_hardware_contract}" _ota_hardware_at)
    if(_ota_hardware_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP OTA hardware probe is missing: "
            "${_ota_hardware_contract}")
    endif()
endforeach()

foreach(_ota_boot_contract IN ITEMS
        "HAL_RP_OTA_PROGRAM_OFFSET + FLASH_PAGE_SIZE"
        "jump_to_vectors(vectors)"
        "msr msp, r1"
        "str r0, [r3]")
    string(FIND "${_ota_boot_text}" "${_ota_boot_contract}" _ota_boot_at)
    if(_ota_boot_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP OTA boot handoff is missing: ${_ota_boot_contract}")
    endif()
endforeach()

foreach(_ota_target_contract IN ITEMS
        "if(_jh_native_ota AND JH_RP_TARGET_NAME STREQUAL \"rp2350-riscv\")"
        "HAL_ENABLE_OTA is not supported for rp2350-riscv"
        "use rp2040 or rp2350-arm")
    string(FIND "${_native_common_text}" "${_ota_target_contract}"
        _ota_target_at)
    if(_ota_target_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP OTA target guard is missing: ${_ota_target_contract}")
    endif()
endforeach()

foreach(_unsupported_picotool_contract IN ITEMS
        "PICOTOOL_EXTRA_UF2_ARGS"
        "--platform")
    string(FIND "${_native_common_text}" "${_unsupported_picotool_contract}"
        _unsupported_picotool_at)
    if(NOT _unsupported_picotool_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP UF2 conversion must let the Pico SDK select the family; "
            "found unsupported contract: ${_unsupported_picotool_contract}")
    endif()
endforeach()

foreach(_freertos_port IN ITEMS RP2040 RP2350_ARM_NTZ RP2350_RISC-V)
    string(FIND "${_freertos_cmake_text}" "${_freertos_port}" _port_at)
    if(_port_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP FreeRTOS CMake is missing port ${_freertos_port}")
    endif()
endforeach()

foreach(_freertos_cmake_contract IN ITEMS
        "PUBLIC FreeRTOS-Kernel-Heap4"
        "HAL_ENABLE_FREERTOS=1"
        "__FREERTOS=1"
        "Community-Supported-Ports/GCC/RP2350_ARM_NTZ"
        "Community-Supported-Ports/GCC/RP2350_RISC-V"
        "ensure_freertos_kernel.sh")
    string(FIND
        "${_freertos_cmake_text}\n${_script_text}"
        "${_freertos_cmake_contract}"
        _freertos_cmake_at)
    if(_freertos_cmake_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP FreeRTOS CMake contract is missing: "
            "${_freertos_cmake_contract}")
    endif()
endforeach()

foreach(_freertos_riscv_tick_contract IN ITEMS
        "HAL_TARGET_RP2350_RISCV"
        "HAL_ENABLE_FREERTOS"
        "vPortSetupTimerInterrupt"
        "xTaskIncrementTick"
        "callTaskExitCriticalFromISR"
        "portYIELD_FROM_ISR"
        "SIO_IRQ_MTIMECMP")
    string(FIND
        "${_freertos_cmake_text}\n${_freertos_riscv_tick_text}"
        "${_freertos_riscv_tick_contract}"
        _freertos_riscv_tick_at)
    if(_freertos_riscv_tick_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP2350 RISC-V tick workaround is missing: "
            "${_freertos_riscv_tick_contract}")
    endif()
endforeach()

foreach(_freertos_config_contract IN ITEMS
        "HAL_FREERTOS_CORE_COUNT 2"
        "configNUMBER_OF_CORES HAL_FREERTOS_CORE_COUNT"
        "configUSE_CORE_AFFINITY (HAL_FREERTOS_CORE_COUNT > 1)"
        "configSUPPORT_PICO_SYNC_INTEROP 1"
        "configSUPPORT_PICO_TIME_INTEROP 1"
        "configTICK_CORE 0"
        "configTOTAL_HEAP_SIZE"
        "configASSERT")
    string(FIND "${_freertos_config_text}" "${_freertos_config_contract}"
        _freertos_config_at)
    if(_freertos_config_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP FreeRTOS config is missing: "
            "${_freertos_config_contract}")
    endif()
endforeach()

foreach(_freertos_entry_contract IN ITEMS
        "xTaskCreateAffinitySet"
        "1u << 0u"
        "1u << 1u"
        "vTaskStartScheduler")
    string(FIND "${_app_entry_text}" "${_freertos_entry_contract}"
        _freertos_entry_at)
    if(_freertos_entry_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP FreeRTOS entry is missing: "
            "${_freertos_entry_contract}")
    endif()
endforeach()

foreach(_freertos_probe_contract IN ITEMS
        "get_core_num"
        "hal_mutex_lock"
        "xTaskGetSchedulerState"
        "hal_get_free_heap"
        "hal_usb_cdc_write")
    string(FIND
        "${_freertos_probe_text}\n${_freertos_verifier_text}"
        "${_freertos_probe_contract}" _freertos_probe_at)
    if(_freertos_probe_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP FreeRTOS hardware probe is missing: "
            "${_freertos_probe_contract}")
    endif()
endforeach()

foreach(_descriptor_contract IN ITEMS
        "tud_descriptor_device_cb"
        "tud_descriptor_configuration_cb"
        "tud_descriptor_string_cb"
        "TUD_CDC_DESCRIPTOR")
    string(FIND
        "${_usb_descriptors_text}\n${_tusb_config_text}"
        "${_descriptor_contract}" _descriptor_at)
    if(_descriptor_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP USB descriptors are missing: ${_descriptor_contract}")
    endif()
endforeach()

foreach(_forbidden_serial_owner IN ITEMS "tud_" "CoreMutex" "USB.mutex")
    string(FIND
        "${_serial_impl_text}" "${_forbidden_serial_owner}" _owner_at)
    if(NOT _owner_at EQUAL -1)
        message(FATAL_ERROR
            "hal_serial still owns USB directly: ${_forbidden_serial_owner}")
    endif()
endforeach()

string(FIND "${_native_common_text}" "NO_USB=1" _native_no_usb_at)
if(NOT _native_no_usb_at EQUAL -1)
    message(FATAL_ERROR "Native RP build still disables USB")
endif()

foreach(_artifact IN ITEMS elf bin uf2)
    string(FIND "${_script_text}" "${_artifact}" _artifact_at)
    if(_artifact_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP build script does not verify ${_artifact} artifacts")
    endif()
endforeach()

foreach(_helper IN ITEMS
        ensure_pico_sdk.sh ensure_picotool.sh ensure_riscv_toolchain.sh)
    string(FIND "${_script_text}" "${_helper}" _helper_at)
    if(_helper_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP build script does not use ${_helper}")
    endif()
endforeach()

string(FIND "${_native_common_text}" "jh_collect_rp_hal_sources" _shared_at)
if(_shared_at EQUAL -1)
    message(FATAL_ERROR "RP build does not use the shared source inventory")
endif()

foreach(_entry_contract IN ITEMS
        "multicore_launch_core1"
        "jh_rp_flash_transaction_core_init"
        "app_start"
        "app_task0"
        "app_task1")
    string(FIND "${_app_entry_text}" "${_entry_contract}" _entry_at)
    if(_entry_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP app entry is missing: ${_entry_contract}")
    endif()
endforeach()
string(FIND "${_app_entry_text}"
    "native Pico SDK app entry is not implemented" _stale_entry_at)
if(NOT _stale_entry_at EQUAL -1)
    message(FATAL_ERROR "Native RP app entry still contains its old blocker")
endif()

foreach(_cmake_contract IN ITEMS
        "CUSTOM_ENTRY"
        "HAL_PROVIDE_APP_ENTRY=1"
        "JH_RP_NATIVE_APP_DIR"
        "JH_RP_NATIVE_APP_SOURCES"
        "jh_generate_board_config"
        "jh_apply_board_components"
        "JH_BOARD"
        "JH_BOARD_GENERATED_DIR"
        "jh_link_contract_definition.c"
        "JH_BOARD_EXPECTED_FLASH_BYTES"
        "JH_RP_CYW43_LED_ONLY"
        "jh_target_enable_cyw43_driver"
        "set(PICO_CYW43_SUPPORTED 0)"
        "jh_rp_native_core1_probe"
        "jh_rp_native_firmware"
        "EXCLUDE_APP_ENTRY"
        "PICO_STACK_SIZE"
        "PICO_CORE1_STACK_SIZE")
    string(FIND "${_native_text}" "${_cmake_contract}" _cmake_contract_at)
    if(_cmake_contract_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP CMake app contract is missing: ${_cmake_contract}")
    endif()
endforeach()

foreach(_board_contract IN ITEMS
        "--boards-root"
        "--target"
        "--board"
        "jh_board_config.cmake"
        "jh_link_contract.h"
        "contractSymbol")
    string(FIND
        "${_board_generator_text}\n${_board_cmake_text}"
        "${_board_contract}" _board_contract_at)
    if(_board_contract_at EQUAL -1)
        message(FATAL_ERROR
            "Declarative board build contract is missing: ${_board_contract}")
    endif()
endforeach()

foreach(_dispatcher_contract IN ITEMS
        "rp2350-arm"
        "rp2350-riscv"
        "cmake/targets/rp-native.cmake")
    string(FIND "${_dispatcher_text}" "${_dispatcher_contract}" _dispatcher_at)
    if(_dispatcher_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP dispatcher contract is missing: ${_dispatcher_contract}")
    endif()
endforeach()

foreach(_script_contract IN ITEMS
        "--example"
        "--example-source"
        "examples/"
        "jh_rp_native_core1_probe"
        "jh_rp_native_firmware")
    string(FIND "${_script_text}" "${_script_contract}" _script_contract_at)
    if(_script_contract_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP build helper is missing: ${_script_contract}")
    endif()
endforeach()

foreach(_core1_contract IN ITEMS app_start app_task0 app_task1)
    string(FIND "${_core1_probe_text}" "${_core1_contract}" _core1_at)
    if(_core1_at EQUAL -1)
        message(FATAL_ERROR
            "Native RP core-1 probe is missing: ${_core1_contract}")
    endif()
endforeach()

foreach(_core_runtime_contract IN ITEMS app_start app_task0 HAL_LED_BUILTIN)
    string(FIND "${_core_runtime_text}" "${_core_runtime_contract}"
        _core_runtime_at)
    if(_core_runtime_at EQUAL -1)
        message(FATAL_ERROR
            "Portable core runtime example is missing: "
            "${_core_runtime_contract}")
    endif()
endforeach()

include("${_sources}")
set(EXTRA_HAL_DEFINES
    "HAL_RP2040_STACK_SIZE=3072u"
    "HAL_RP2040_CORE1_STACK_SIZE=2048u")
jh_hal_define_value(_stack0 HAL_RP2040_STACK_SIZE)
jh_hal_define_value(_stack1 HAL_RP2040_CORE1_STACK_SIZE)
if(NOT "${_stack0}" STREQUAL "3072u" OR
   NOT "${_stack1}" STREQUAL "2048u")
    message(FATAL_ERROR "RP native stack define mapping is broken")
endif()
unset(EXTRA_HAL_DEFINES)

foreach(_source_contract IN ITEMS
        "jh_hal_define_value"
        "EXCLUDE_APP_ENTRY"
        "hal_app_entry.cpp")
    string(FIND "${_sources_text}" "${_source_contract}" _source_contract_at)
    if(_source_contract_at EQUAL -1)
        message(FATAL_ERROR
            "Shared RP source contract is missing: ${_source_contract}")
    endif()
endforeach()

if(NOT _probe_text MATCHES "int[ \t\r\n]+main[ \t\r\n]*\\(" OR
   NOT _probe_text MATCHES "hal_millis[ \t\r\n]*\\(")
    message(FATAL_ERROR
        "Native RP artifact probe must link a HAL call through a local main()")
endif()

if(NOT _sources_text MATCHES "function\\(jh_collect_rp_hal_sources")
    message(FATAL_ERROR "Shared RP source inventory function is missing")
endif()
