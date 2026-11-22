#pragma once
#include <FreeRTOS.h>
#define taskSCHEDULER_RUNNING 2
#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xTaskGetSchedulerState(void);
#ifdef __cplusplus
}
#endif
