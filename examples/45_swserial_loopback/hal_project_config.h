#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Entry point is selected by the build system:
 * RP2040 generates setup()/loop(); STM32 defines HAL_PROVIDE_APP_ENTRY. */

#define HAL_ENABLE_SWSERIAL
