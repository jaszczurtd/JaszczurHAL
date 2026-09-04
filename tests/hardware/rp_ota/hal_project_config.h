#pragma once

#ifndef HAL_ENABLE_OTA
#define HAL_ENABLE_OTA
#endif

#ifndef HAL_ENABLE_KV
#define HAL_ENABLE_KV
#endif

#ifndef HAL_ENABLE_LITTLEFS
#define HAL_ENABLE_LITTLEFS
#endif

#if defined(HAL_ENABLE_FREERTOS) && !defined(HAL_FREERTOS_TASK0_STACK)
#define HAL_FREERTOS_TASK0_STACK 2048u
#endif

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif
