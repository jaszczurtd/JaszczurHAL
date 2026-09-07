#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* The build selects the HAL entry point for RP and STM32 applications. */

#ifndef HAL_ENABLE_MQTT
#define HAL_ENABLE_MQTT
#endif
