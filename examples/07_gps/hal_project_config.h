#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Entry point is selected by the build system:
 * RP and STM32 use the HAL-owned application entry point. */

#define HAL_ENABLE_GPS
#define HAL_ENABLE_SWSERIAL /* GPS transport on this RP2040 sketch (PA5/PA4)   \
                             */
