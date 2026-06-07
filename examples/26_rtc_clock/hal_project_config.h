#pragma once

/**
 * @file hal_project_config.h
 * @brief RTC Clock example configuration.
 *
 * Enables PCF8563 RTC support with serial debugging.
 */

#define HAL_ENABLE_RTC
#define HAL_ENABLE_PCF8563
#define HAL_ENABLE_SERIAL

/* Optional: Use custom I2C address if your hardware differs from default 0x51 */
/* #define HAL_RTC_PCF8563_DEFAULT_I2C_ADDR 0x51 */

/* Optional: Configure I2C clock speed (default 100kHz via HAL) */
/* #define HAL_I2C_CLOCK_HZ 100000 */
