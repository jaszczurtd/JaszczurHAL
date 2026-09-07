#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Enabling SSD1306 also enables the display, graphics, and I2C support it
 * needs. */
#define HAL_ENABLE_SSD1306
#define HAL_ENABLE_HD44780
