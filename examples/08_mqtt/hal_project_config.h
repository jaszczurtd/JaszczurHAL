#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Entry point is selected by the build system:
 * RP and STM32 use the HAL-owned application entry point. */

#ifndef HAL_ENABLE_MQTT
#define HAL_ENABLE_MQTT
#endif
