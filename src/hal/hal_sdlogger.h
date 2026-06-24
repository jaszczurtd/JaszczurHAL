#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_SDLOGGER

/**
 * @file hal_sdlogger.h
 * @brief SD-card logger and crash-report logger.
 *
 * This module keeps the public logger API in HAL while shared FatFs file
 * handling provides the SD-over-SPI storage backend for RP2040 and STM32.
 * Public headers do not expose FatFs or target-specific file types.
 */

#include <stdbool.h>
#include <stdint.h>

#ifndef HAL_SDLOGGER_WRITE_INTERVAL_MS
#define HAL_SDLOGGER_WRITE_INTERVAL_MS 2000u
#endif

#ifndef HAL_SDLOGGER_EEPROM_LOGGER_ADDR
#define HAL_SDLOGGER_EEPROM_LOGGER_ADDR 0u
#endif

#ifndef HAL_SDLOGGER_EEPROM_CRASH_ADDR
#define HAL_SDLOGGER_EEPROM_CRASH_ADDR 4u
#endif

#ifndef HAL_SDLOGGER_EEPROM_FIRST_ADDR
#define HAL_SDLOGGER_EEPROM_FIRST_ADDR 8u
#endif

#ifndef HAL_SDLOGGER_LOG_BUFFER_SIZE
#define HAL_SDLOGGER_LOG_BUFFER_SIZE 2048u
#endif

#ifndef HAL_SDLOGGER_NAME_BUFFER_SIZE
#define HAL_SDLOGGER_NAME_BUFFER_SIZE 128u
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return the next SD log file number stored in EEPROM. */
int hal_sdlogger_get_log_number(void);

/** @brief Return the next crash-report file number stored in EEPROM. */
int hal_sdlogger_get_crash_number(void);

/**
 * @brief Initialise the periodic SD logger.
 * @param cs SD card chip-select pin.
 * @return true when the SD card and log file are ready.
 */
bool hal_sdlogger_init(int cs);

/**
 * @brief Initialise the crash-report logger.
 * @param add_to_name Optional crash tag written into the crash-report file.
 * @param cs SD card chip-select pin.
 * @return true when the SD card and crash-report file are ready.
 */
bool hal_sdlogger_crash_init(const char *add_to_name, int cs);

/** @brief Return true when the periodic SD logger is ready. */
bool hal_sdlogger_is_initialized(void);

/** @brief Return true when the crash-report logger is ready. */
bool hal_sdlogger_crash_is_initialized(void);

/** @brief Append one line to the periodic SD logger buffer. */
void hal_sdlogger_append(const char *data);

/** @brief Append one line immediately to the crash-report file. */
void hal_sdlogger_crash_append(const char *data);

/** @brief Flush buffered log data and close the periodic SD logger file. */
void hal_sdlogger_close(void);

/** @brief Flush and close the crash-report file. */
void hal_sdlogger_crash_close(void);

/** @brief Append a formatted crash-report entry. */
void hal_sdlogger_crash_report(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_SDLOGGER */
