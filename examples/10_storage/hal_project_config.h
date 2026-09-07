#pragma once

#define HAL_ENABLE_KV
#define HAL_ENABLE_LITTLEFS
#define HAL_ENABLE_SDLOGGER

#define HAL_EEPROM_TYPE EEPROM_TYPE_FLASH

/* Formatting erases the LittleFS partition. Enable it only when data loss is
 * acceptable. */
#ifndef EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT
#define EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT 0
#endif

#ifndef HAL_SDLOGGER_WRITE_INTERVAL_MS
#define HAL_SDLOGGER_WRITE_INTERVAL_MS 2000u
#endif

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif
