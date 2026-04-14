#ifndef JASZCZURHAL_TOOLS_LOGGER_CONFIG_H
#define JASZCZURHAL_TOOLS_LOGGER_CONFIG_H

/**
 * @file tools_logger_config.h
 * @brief Logger-related utility defaults and compatibility aliases.
 */

#ifdef SD_LOGGER
/** @brief Default periodic flush interval for SD logger (milliseconds). */
#ifndef HAL_TOOLS_WRITE_INTERVAL_MS
#define HAL_TOOLS_WRITE_INTERVAL_MS 2000
#endif
/** @brief EEPROM address storing the current SD log file index. */
#ifndef HAL_TOOLS_EEPROM_LOGGER_ADDR
#define HAL_TOOLS_EEPROM_LOGGER_ADDR 0
#endif
/** @brief EEPROM address storing the current crash log file index. */
#ifndef HAL_TOOLS_EEPROM_CRASH_ADDR
#define HAL_TOOLS_EEPROM_CRASH_ADDR 4
#endif
/** @brief First free EEPROM address after logger metadata region. */
#ifndef HAL_TOOLS_EEPROM_FIRST_ADDR
#define HAL_TOOLS_EEPROM_FIRST_ADDR 8
#endif
#else
/** @brief First EEPROM address available when SD logger is disabled. */
#ifndef HAL_TOOLS_EEPROM_FIRST_ADDR
#define HAL_TOOLS_EEPROM_FIRST_ADDR 0
#endif
#endif

/* Backward-compatible aliases used by legacy code. */
#ifdef SD_LOGGER
/** @brief Legacy alias for @ref HAL_TOOLS_WRITE_INTERVAL_MS. */
#ifndef WRITE_INTERVAL
#define WRITE_INTERVAL HAL_TOOLS_WRITE_INTERVAL_MS
#endif
/** @brief Legacy alias for @ref HAL_TOOLS_EEPROM_LOGGER_ADDR. */
#ifndef EEPROM_LOGGER_ADDR
#define EEPROM_LOGGER_ADDR HAL_TOOLS_EEPROM_LOGGER_ADDR
#endif
/** @brief Legacy alias for @ref HAL_TOOLS_EEPROM_CRASH_ADDR. */
#ifndef EEPROM_CRASH_ADDR
#define EEPROM_CRASH_ADDR HAL_TOOLS_EEPROM_CRASH_ADDR
#endif
#endif
/** @brief Legacy alias for @ref HAL_TOOLS_EEPROM_FIRST_ADDR. */
#ifndef EEPROM_FIRST_ADDR
#define EEPROM_FIRST_ADDR HAL_TOOLS_EEPROM_FIRST_ADDR
#endif

#endif
