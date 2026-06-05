#pragma once

/* Entry point is selected by the build system:
 * RP2040 generates setup()/loop(); STM32 defines HAL_PROVIDE_APP_ENTRY. */

#define HAL_ENABLE_GPS
#define HAL_ENABLE_SWSERIAL   /* GPS transport on this RP2040 sketch (PA5/PA4) */
