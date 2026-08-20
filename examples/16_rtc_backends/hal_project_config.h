#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Compile all runtime-selectable RTC providers in one firmware image. */
#define HAL_ENABLE_RTC
#define HAL_ENABLE_PCF8563
#define HAL_ENABLE_DS3231
#define HAL_ENABLE_INTERNAL_RTC
#define HAL_ENABLE_POWER_MANAGEMENT
