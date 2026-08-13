#include <FreeRTOS.h>
#include <task.h>

#include <hardware/sync.h>

#include "hal/impl/rp2040/drivers/rp2040/rp2040_fault.h"

void jh_rp_freertos_assert_fail(const char *file, int line) {
  (void)file;
  (void)line;
  (void)save_and_disable_interrupts();
  for (;;) {
    __asm volatile("" ::: "memory");
  }
}

void vApplicationMallocFailedHook(void) {
  jh_rp_freertos_assert_fail("malloc", 0);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name) {
  (void)task;
  (void)task_name;
  jh_stack_overflow_reset();
}
