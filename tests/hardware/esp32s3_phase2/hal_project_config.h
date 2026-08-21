#pragma once

/* Phase 2 exercises both ESP32-S3 application tasks and every optional bus
 * backend implemented in this phase. FreeRTOS itself is target-required. */
#define HAL_ENABLE_APP_TASK1 1
#define HAL_ENABLE_I2C 1
#define HAL_ENABLE_SPI 1
#define HAL_ENABLE_UART 1

/* Make the hardware affinity contract explicit and testable. */
#define HAL_FREERTOS_TASK0_CORE 0
#define HAL_FREERTOS_TASK1_CORE 1
#define HAL_FREERTOS_TASK0_STACK 4096u
#define HAL_FREERTOS_TASK1_STACK 4096u
