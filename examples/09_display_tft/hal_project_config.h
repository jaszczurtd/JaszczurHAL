#pragma once

/* Entry point is selected by the build system:
 * RP2040 generates setup()/loop(); STM32 defines HAL_PROVIDE_APP_ENTRY. */

#define HAL_ENABLE_ILI9341
#define HAL_DISPLAY_ILI9341
