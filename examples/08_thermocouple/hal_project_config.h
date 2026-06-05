#pragma once

/* Entry point is selected by the build system:
 * RP2040 generates setup()/loop(); STM32 defines HAL_PROVIDE_APP_ENTRY. */

#define HAL_ENABLE_I2C
#define HAL_ENABLE_MCP9600
#define HAL_ENABLE_MAX6675
