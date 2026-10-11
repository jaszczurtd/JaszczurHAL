/* Startup and FreeRTOS read the STM32G474 clock before any HAL header; the
 * assembly's clock tree must already apply there. */
#include "hal/impl/stm32g474/freertos/FreeRTOSConfig.h"

#if !defined(HAL_STM32G474_CLOCK_HSE_160MHZ) || !JH_G474_FDCAN_FROM_PLLQ
#error "the clock code does not see the clock tree of the assembly"
#endif

int main(void) { return configCPU_CLOCK_HZ == 160000000u ? 0 : 1; }
