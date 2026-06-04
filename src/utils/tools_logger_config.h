#ifndef JASZCZURHAL_TOOLS_LOGGER_CONFIG_H
#define JASZCZURHAL_TOOLS_LOGGER_CONFIG_H

/**
 * @file tools_logger_config.h
 * @brief Legacy tools EEPROM-address compatibility aliases.
 *
 * SD-card logging moved to the HAL module `hal_sdlogger`.
 */

#include <hal/hal_config.h>
#ifdef HAL_ENABLE_SDLOGGER
#include <hal/hal_sdlogger.h>
#endif

/** @brief First EEPROM address available to tools utilities. */
#ifndef HAL_TOOLS_EEPROM_FIRST_ADDR
#ifdef HAL_ENABLE_SDLOGGER
#define HAL_TOOLS_EEPROM_FIRST_ADDR HAL_SDLOGGER_EEPROM_FIRST_ADDR
#else
#define HAL_TOOLS_EEPROM_FIRST_ADDR 0
#endif
#endif

/* Backward-compatible aliases used by legacy code. */
/** @brief Legacy alias for @ref HAL_TOOLS_EEPROM_FIRST_ADDR. */
#ifndef EEPROM_FIRST_ADDR
#define EEPROM_FIRST_ADDR HAL_TOOLS_EEPROM_FIRST_ADDR
#endif

#endif
