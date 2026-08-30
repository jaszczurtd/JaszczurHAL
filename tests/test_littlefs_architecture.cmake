if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_facade "${JH_ROOT}/src/hal/storage/hal_littlefs.cpp")
set(_provider_header
    "${JH_ROOT}/src/hal/storage/jh_littlefs_provider.h")
set(_lfs_provider
    "${JH_ROOT}/src/hal/storage/jh_littlefs_lfs_provider.cpp")
set(_mock_backend "${JH_ROOT}/src/hal/impl/.mock/hal_littlefs.cpp")
set(_rp_backend "${JH_ROOT}/src/hal/impl/rp2040/hal_littlefs.cpp")
set(_stm_backend "${JH_ROOT}/src/hal/impl/stm32g474/hal_littlefs.cpp")
set(_stm_flash
    "${JH_ROOT}/src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_flash.cpp")
set(_stm_eeprom "${JH_ROOT}/src/hal/impl/stm32g474/hal_eeprom.cpp")
set(_rp_flash_storage
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/flash/rp_flash_storage.cpp")
set(_stm_linker "${JH_ROOT}/stm32_lib/STM32G474RETx_FLASH.ld")
set(_root_cmake "${JH_ROOT}/CMakeLists.txt")
set(_rp_sources "${JH_ROOT}/cmake/jh_rp_hal_sources.cmake")
set(_stm_sources "${JH_ROOT}/stm32_lib/CMakeLists.txt")
set(_stm_firmware_sources
    "${JH_ROOT}/stm32_lib/jh_stm32g474_firmware.cmake")

foreach(_required IN ITEMS
        "${_facade}"
        "${_provider_header}"
        "${_lfs_provider}"
        "${_mock_backend}"
        "${_rp_backend}"
        "${_stm_backend}"
        "${_stm_flash}"
        "${_stm_eeprom}"
        "${_rp_flash_storage}"
        "${_stm_linker}"
        "${_root_cmake}"
        "${_rp_sources}"
        "${_stm_sources}"
        "${_stm_firmware_sources}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "LittleFS architecture file is missing: ${_required}")
    endif()
endforeach()

file(READ "${_facade}" _facade_text)
file(READ "${_provider_header}" _provider_header_text)
file(READ "${_lfs_provider}" _lfs_provider_text)
file(READ "${_mock_backend}" _mock_text)
file(READ "${_rp_backend}" _rp_text)
file(READ "${_stm_backend}" _stm_text)
file(READ "${_stm_flash}" _stm_flash_text)
file(READ "${_stm_eeprom}" _stm_eeprom_text)
file(READ "${_rp_flash_storage}" _rp_flash_storage_text)
file(READ "${_stm_linker}" _stm_linker_text)
file(READ "${_root_cmake}" _root_cmake_text)
file(READ "${_rp_sources}" _rp_sources_text)
file(READ "${_stm_sources}" _stm_sources_text)
file(READ "${_stm_firmware_sources}" _stm_firmware_sources_text)

foreach(_public_symbol IN ITEMS
        hal_littlefs_set_progress_callback
        hal_littlefs_begin_ex
        hal_littlefs_begin
        hal_littlefs_end
        hal_littlefs_format_ex
        hal_littlefs_format
        hal_littlefs_is_mounted
        hal_littlefs_exists_ex
        hal_littlefs_exists
        hal_littlefs_remove_ex
        hal_littlefs_remove
        hal_littlefs_total_bytes_ex
        hal_littlefs_total_bytes
        hal_littlefs_used_bytes_ex
        hal_littlefs_used_bytes)
    if(NOT _facade_text MATCHES
       "(bool|size_t|hal_status_t)[ \t\r\n]+${_public_symbol}[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "LittleFS facade does not own ${_public_symbol}")
    endif()
endforeach()

if(_lfs_provider_text MATCHES
   "(bool|size_t|hal_status_t)[ \t\r\n]+hal_littlefs_[a-z_]+[ \t\r\n]*\\(")
    message(FATAL_ERROR
        "Shared littlefs provider exports a public facade operation")
endif()

foreach(_provider_header_requirement IN ITEMS
        jh_littlefs_geometry_t
        jh_littlefs_block_backend_t
        jh_littlefs_provider_ops_t
        jh_littlefs_provider_t
        jh_littlefs_provider_get
        jh_littlefs_lfs_provider_configure)
    string(FIND "${_provider_header_text}" "${_provider_header_requirement}"
        _provider_header_at)
    if(_provider_header_at EQUAL -1)
        message(FATAL_ERROR
            "LittleFS provider interface is missing ${_provider_header_requirement}")
    endif()
endforeach()

foreach(_facade_requirement IN ITEMS
        jh_littlefs_provider_get
        jh_hal_mutex_create_once
        hal_mutex_lock
        hal_mutex_unlock
        validate_path)
    string(FIND "${_facade_text}" "${_facade_requirement}" _facade_at)
    if(_facade_at EQUAL -1)
        message(FATAL_ERROR
            "LittleFS facade is missing ${_facade_requirement}")
    endif()
endforeach()

foreach(_target_coupling IN ITEMS
        HAL_TARGET_IS_
        jh_rp_flash_
        jh_stm32g474_flash_
        lfs_mount
        lfs_unmount
        lfs_format
        lfs_stat
        lfs_remove
        lfs_fs_size)
    string(FIND "${_facade_text}" "${_target_coupling}" _coupling_at)
    if(NOT _coupling_at EQUAL -1)
        message(FATAL_ERROR
            "LittleFS facade gained target/library coupling: ${_target_coupling}")
    endif()
endforeach()

foreach(_provider_requirement IN ITEMS
        lfs_mount
        lfs_unmount
        lfs_format
        lfs_stat
        lfs_remove
        lfs_fs_size
        jh_littlefs_block_read
        jh_littlefs_block_program
        jh_littlefs_block_erase
        jh_littlefs_block_sync)
    string(FIND "${_lfs_provider_text}" "${_provider_requirement}"
        _provider_at)
    if(_provider_at EQUAL -1)
        message(FATAL_ERROR
            "Shared littlefs provider is missing ${_provider_requirement}")
    endif()
endforeach()

foreach(_backend IN ITEMS "${_mock_backend}" "${_rp_backend}" "${_stm_backend}")
    file(READ "${_backend}" _backend_text)
    if(_backend_text MATCHES
       "(bool|size_t|hal_status_t)[ \t\r\n]+hal_littlefs_[a-z_]+[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "LittleFS backend exports a public facade operation: ${_backend}")
    endif()
    foreach(_lifecycle IN ITEMS
            lfs_mount lfs_unmount lfs_format lfs_stat lfs_remove lfs_fs_size)
        string(FIND "${_backend_text}" "${_lifecycle}" _lifecycle_at)
        if(NOT _lifecycle_at EQUAL -1)
            message(FATAL_ERROR
                "LittleFS backend owns library lifecycle ${_lifecycle}: ${_backend}")
        endif()
    endforeach()
endforeach()

foreach(_rp_requirement IN ITEMS
        jh_littlefs_geometry_t
        jh_littlefs_block_backend_t
        jh_littlefs_lfs_provider_configure
        jh_rp_flash_storage_partition
        jh_rp_flash_storage_read
        jh_rp_flash_storage_program
        jh_rp_flash_storage_erase
        JH_RP_FLASH_PARTITION_LITTLEFS)
    string(FIND "${_rp_text}" "${_rp_requirement}" _rp_at)
    if(_rp_at EQUAL -1)
        message(FATAL_ERROR "RP LittleFS backend is missing ${_rp_requirement}")
    endif()
endforeach()

foreach(_stm_requirement IN ITEMS
        jh_littlefs_geometry_t
        jh_littlefs_block_backend_t
        jh_littlefs_lfs_provider_configure
        HAL_STM32_FLASH_PAGE_SIZE
        jh_stm32g474_flash_access_begin
        jh_stm32g474_flash_access_end
        jh_stm32g474_flash_unlock
        jh_stm32g474_flash_program_doubleword
        jh_stm32g474_flash_erase_page
        jh_stm32g474_flash_lock)
    string(FIND "${_stm_text}" "${_stm_requirement}" _stm_at)
    if(_stm_at EQUAL -1)
        message(FATAL_ERROR
            "STM32 LittleFS backend is missing ${_stm_requirement}")
    endif()
endforeach()

foreach(_stm_access_guard IN ITEMS
        jh_stm32g474_flash_access_begin
        jh_stm32g474_flash_access_end)
    string(REGEX MATCHALL "${_stm_access_guard}" _stm_access_hits
        "${_stm_text}")
    list(LENGTH _stm_access_hits _stm_access_count)
    if(_stm_access_count LESS 2)
        message(FATAL_ERROR
            "STM32 LittleFS read/sync serialization is incomplete: ${_stm_access_guard}")
    endif()
endforeach()

foreach(_mock_requirement IN ITEMS
        jh_littlefs_provider_get
        jh_littlefs_mock_reset_facade
        hal_mock_littlefs_reset)
    string(FIND "${_mock_text}" "${_mock_requirement}" _mock_at)
    if(_mock_at EQUAL -1)
        message(FATAL_ERROR
            "Mock LittleFS backend is missing ${_mock_requirement}")
    endif()
endforeach()
foreach(_mock_state IN ITEMS hal_mutex_ s_mounted s_littlefs_mounted)
    string(FIND "${_mock_text}" "${_mock_state}" _mock_state_at)
    if(NOT _mock_state_at EQUAL -1)
        message(FATAL_ERROR
            "Mock LittleFS backend duplicates facade state: ${_mock_state}")
    endif()
endforeach()

foreach(_flash_lock_requirement IN ITEMS
        jh_hal_mutex_create_once
        hal_mutex_lock
        hal_mutex_unlock
        jh_stm32g474_flash_access_begin
        jh_stm32g474_flash_access_end)
    string(FIND "${_stm_flash_text}" "${_flash_lock_requirement}"
        _flash_lock_at)
    if(_flash_lock_at EQUAL -1)
        message(FATAL_ERROR
            "STM32 shared flash serialization is missing ${_flash_lock_requirement}")
    endif()
endforeach()
foreach(_eeprom_flash_requirement IN ITEMS
        jh_stm32g474_flash_unlock
        jh_stm32g474_flash_erase_page
        jh_stm32g474_flash_program_doubleword
        jh_stm32g474_flash_lock)
    string(FIND "${_stm_eeprom_text}" "${_eeprom_flash_requirement}"
        _eeprom_flash_at)
    if(_eeprom_flash_at EQUAL -1)
        message(FATAL_ERROR
            "STM32 EEPROM bypasses shared flash serialization: ${_eeprom_flash_requirement}")
    endif()
endforeach()

foreach(_rp_partition_guard IN ITEMS
        "kLittlefsSize % FLASH_SECTOR_SIZE"
        "kLittlefsSize >= (2u * FLASH_SECTOR_SIZE)"
        "kEepromSize + kLittlefsSize <= kFlashSize")
    string(FIND "${_rp_flash_storage_text}" "${_rp_partition_guard}"
        _rp_partition_guard_at)
    if(_rp_partition_guard_at EQUAL -1)
        message(FATAL_ERROR
            "RP LittleFS partition guard is missing: ${_rp_partition_guard}")
    endif()
endforeach()

foreach(_stm_partition_guard IN ITEMS
        "HAL_STM32_FLASH_LITTLEFS_SIZE >= (2 * HAL_STM32_FLASH_PAGE_SIZE)"
        "HAL_STM32_FLASH_LITTLEFS_SIZE % HAL_STM32_FLASH_PAGE_SIZE"
        "__hal_stm32_littlefs_flash_start"
        "__hal_stm32_littlefs_flash_end")
    string(FIND "${_stm_linker_text}" "${_stm_partition_guard}"
        _stm_partition_guard_at)
    if(_stm_partition_guard_at EQUAL -1)
        message(FATAL_ERROR
            "STM32 LittleFS partition guard is missing: ${_stm_partition_guard}")
    endif()
endforeach()

string(FIND "${_root_cmake_text}" "hal/storage/hal_littlefs.cpp"
    _root_facade_at)
if(_root_facade_at EQUAL -1)
    message(FATAL_ERROR "Host mock source inventory omits the LittleFS facade")
endif()
foreach(_inventory_text IN ITEMS
        "${_rp_sources_text}"
        "${_stm_sources_text}"
        "${_stm_firmware_sources_text}")
    if(NOT _inventory_text MATCHES
       "hal/\\*\\.cpp" OR
       NOT _inventory_text MATCHES
       "EXCLUDE REGEX \"/hal/impl/\"")
        message(FATAL_ERROR
            "Target source inventory no longer selects shared HAL sources")
    endif()
endforeach()
