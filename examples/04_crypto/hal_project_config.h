#pragma once

/* Entry point is selected by the build system:
 * RP2040 generates setup()/loop(); STM32 defines HAL_PROVIDE_APP_ENTRY. */
#define HAL_ENABLE_CRYPTO
