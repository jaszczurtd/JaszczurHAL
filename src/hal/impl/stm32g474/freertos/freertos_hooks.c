#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474 && defined(HAL_ENABLE_FREERTOS)

#include <FreeRTOS.h>
#include <task.h>

#include "hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.h"

void vApplicationMallocFailedHook(void) {
  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name) {
  (void)task;
  (void)task_name;

  jh_stack_overflow_reset();
}

#endif
