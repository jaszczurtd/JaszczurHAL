#pragma once

#include "freertos/FreeRTOS.h"

typedef struct fake_idf_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t function, const char *name,
                       uint32_t stack_depth, void *parameters,
                       UBaseType_t priority, TaskHandle_t *created_task);
void vTaskDelete(TaskHandle_t task);
