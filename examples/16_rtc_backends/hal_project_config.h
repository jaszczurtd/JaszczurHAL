#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Compile both runtime-selectable RTC providers in one firmware image. */
#define HAL_ENABLE_RTC
#define HAL_ENABLE_PCF8563
#define HAL_ENABLE_DS3231
