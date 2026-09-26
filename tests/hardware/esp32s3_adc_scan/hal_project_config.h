#pragma once

/* The continuous ADC scan on the exact esp32s3 target, with the board's
 * WS2812 as the status light and a flash write during the scan. */
#define HAL_ENABLE_ADC_SCAN 1
#define HAL_ENABLE_RGB_LED 1
#define HAL_ENABLE_STACK_GUARD 1

#define HAL_FREERTOS_TASK0_CORE 0
#define HAL_FREERTOS_TASK0_STACK 8192u
