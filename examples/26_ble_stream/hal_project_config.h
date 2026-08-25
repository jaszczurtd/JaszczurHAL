#pragma once

#if defined(HAL_ENABLE_FREERTOS) && !defined(HAL_FREERTOS_TASK0_STACK)
#define HAL_FREERTOS_TASK0_STACK 1024u
#endif

#define HAL_ENABLE_BLE_STREAM
