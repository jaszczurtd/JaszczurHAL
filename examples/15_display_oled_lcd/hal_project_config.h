#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* SSD1306 brings in the shared display/GFX and I2C layers. */
#define HAL_ENABLE_SSD1306
#define HAL_ENABLE_HD44780
