#pragma once

/* Entry point is selected by the build system:
 * RP2040 generates setup()/loop(); STM32 defines HAL_PROVIDE_APP_ENTRY. */

/* SSD1306 monochrome OLED over I2C. Propagates HAL_ENABLE_DISPLAY + I2C. */
#define HAL_ENABLE_SSD1306
