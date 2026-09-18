#pragma once

#include "freertos/FreeRTOS.h"

typedef struct fake_idf_semaphore *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore,
                                 BaseType_t *higher_priority_task_woken);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
