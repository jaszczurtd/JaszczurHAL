#include <stdint.h>

#include "hal/hal_system.h"

uint32_t hal_time_ms(void) { return hal_millis(); }

/* Stage 1 is serviced from JH's cooperative loop. The embedded BTstack run
 * loop must therefore never own IRQ state or put the MCU to sleep. Stage 3
 * replaces these no-op hooks with a JH-owned run-loop integration. */
void hal_cpu_disable_irqs(void) {}
void hal_cpu_enable_irqs(void) {}
void hal_cpu_enable_irqs_and_sleep(void) {}
