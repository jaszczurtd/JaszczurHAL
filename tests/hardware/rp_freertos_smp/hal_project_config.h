#pragma once

#ifndef HAL_ENABLE_FREERTOS
#define HAL_ENABLE_FREERTOS
#endif

#if !defined(JH_FREERTOS_SINGLE_CORE_PROBE) && !defined(HAL_ENABLE_APP_TASK1)
#define HAL_ENABLE_APP_TASK1
#endif
