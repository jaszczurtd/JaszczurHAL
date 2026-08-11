if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_facade "${JH_ROOT}/src/hal/storage/hal_eeprom.cpp")
set(_provider_header "${JH_ROOT}/src/hal/storage/jh_eeprom_provider.h")
set(_flash_provider
    "${JH_ROOT}/src/hal/storage/jh_eeprom_flash_provider.cpp")
set(_at24_provider
    "${JH_ROOT}/src/hal/storage/at24c256/jh_at24c256_provider.cpp")
set(_mock_provider "${JH_ROOT}/src/hal/impl/.mock/hal_eeprom.cpp")
set(_rp_provider "${JH_ROOT}/src/hal/impl/rp2040/hal_eeprom.cpp")
set(_stm_provider "${JH_ROOT}/src/hal/impl/stm32g474/hal_eeprom.cpp")

foreach(_required IN ITEMS
        "${_facade}"
        "${_provider_header}"
        "${_flash_provider}"
        "${_at24_provider}"
        "${_mock_provider}"
        "${_rp_provider}"
        "${_stm_provider}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "EEPROM facade/provider source is missing: ${_required}")
    endif()
endforeach()

file(READ "${_facade}" _facade_contents)
if(NOT _facade_contents MATCHES "jh_eeprom_provider_get_ops" OR
   NOT _facade_contents MATCHES "hal_mutex_lock" OR
   _facade_contents MATCHES "hal_i2c_|jh_rp_flash_|jh_stm32g474_flash_|HAL_TARGET_IS_")
    message(FATAL_ERROR
        "EEPROM facade lost provider dispatch/locking or gained target coupling")
endif()

foreach(_provider IN ITEMS
        "${_mock_provider}"
        "${_rp_provider}"
        "${_stm_provider}"
        "${_at24_provider}"
        "${_flash_provider}")
    file(READ "${_provider}" _contents)
    if(_contents MATCHES
       "hal_status_t[ \t\r\n]+hal_eeprom_[a-z_]+[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "EEPROM provider exports a public facade operation: ${_provider}")
    endif()
endforeach()

file(READ "${_at24_provider}" _at24_contents)
if(NOT _at24_contents MATCHES "hal_i2c_" OR
   _at24_contents MATCHES
   "hardware/i2c|pico/stdlib|stm32g4|stm32g474|HAL_TARGET_IS_")
    message(FATAL_ERROR "AT24C256 provider is not transport-portable")
endif()

file(READ "${_mock_provider}" _mock_contents)
if(_mock_contents MATCHES
   "hal_status_t[ \t\r\n]+hal_eeprom_[a-z_]+[ \t\r\n]*\\(")
    message(FATAL_ERROR "Mock EEPROM public facade returned")
endif()
