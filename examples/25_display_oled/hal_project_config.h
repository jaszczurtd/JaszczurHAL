#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Entry point is selected by the build system:
 * RP and STM32 use the HAL-owned application entry point. */

/* SSD1306 monochrome OLED over I2C. Propagates HAL_ENABLE_DISPLAY + I2C. */
#define HAL_ENABLE_SSD1306
