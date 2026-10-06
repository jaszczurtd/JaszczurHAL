#pragma once

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(EEPROM_16K, "16 KiB EEPROM/KV reservation",                                \
    HAL_STM32_FLASH_EEPROM_SIZE = 16384u)

#define HAL_ENABLE_EEPROM
#define HAL_ENABLE_KV
#define HAL_ENABLE_ADC_SCAN
#undef HAL_EEPROM_TYPE
#define HAL_EEPROM_TYPE EEPROM_TYPE_FLASH

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif
