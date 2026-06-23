/**
 * @file startup_stm32g474.c
 * @brief Minimal C startup + interrupt vector table for STM32G474.
 *
 * Replaces ST's assembler startup with a small, readable C equivalent:
 *   1. hardware loads the initial MSP from vector[0] and jumps to
 * Reset_Handler,
 *   2. Reset_Handler copies .data from flash to RAM, zeroes .bss,
 *   3. calls SystemInit() (clock/SysTick/FPU/fault enables), then main().
 *
 * Core exceptions are populated directly. Peripheral IRQ vectors are added as
 * drivers need them; EXTI lines are used by hal_gpio and TIM6 by hal_timer.
 */

#include <stdint.h>

/* Symbols provided by the linker script. */
extern uint32_t _sidata; /* .data init values in flash      */
extern uint32_t _sdata;  /* .data start in RAM              */
extern uint32_t _edata;  /* .data end in RAM                */
extern uint32_t _sbss;   /* .bss start                      */
extern uint32_t _ebss;   /* .bss end                        */
extern uint32_t _estack; /* top of stack                    */

extern int main(void);
extern void SystemInit(void);

/* Fault / system handlers (defined elsewhere; weak fallbacks below). */
void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI9_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI15_10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_EV_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_EV_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM6_DACUNDER_IRQHandler(void)
    __attribute__((weak, alias("Default_Handler")));

#ifdef HAL_ENABLE_FREERTOS
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
#else
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
#endif

/* STM32G474 IRQ number per RM0440/CMSIS device headers. */
#define STM32_IRQ_EXTI0 6u
#define STM32_IRQ_EXTI1 7u
#define STM32_IRQ_EXTI2 8u
#define STM32_IRQ_EXTI3 9u
#define STM32_IRQ_EXTI4 10u
#define STM32_IRQ_EXTI9_5 23u
#define STM32_IRQ_I2C1_EV 31u
#define STM32_IRQ_I2C1_ER 32u
#define STM32_IRQ_I2C2_EV 33u
#define STM32_IRQ_I2C2_ER 34u
#define STM32_IRQ_EXTI15_10 40u
#define STM32_IRQ_TIM6_DACUNDER 54u

/* Vector table: initial SP + 15 system exceptions + populated peripheral IRQs.
 * Unlisted peripheral entries stay zero; only enabled IRQs must be present. */
__attribute__((section(".isr_vector"),
               used)) void (*const g_vector_table[])(void) = {
    [0] = (void (*)(void))(&_estack), /* Initial stack pointer */
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16u + STM32_IRQ_EXTI0] = EXTI0_IRQHandler,
    [16u + STM32_IRQ_EXTI1] = EXTI1_IRQHandler,
    [16u + STM32_IRQ_EXTI2] = EXTI2_IRQHandler,
    [16u + STM32_IRQ_EXTI3] = EXTI3_IRQHandler,
    [16u + STM32_IRQ_EXTI4] = EXTI4_IRQHandler,
    [16u + STM32_IRQ_EXTI9_5] = EXTI9_5_IRQHandler,
    [16u + STM32_IRQ_I2C1_EV] = I2C1_EV_IRQHandler,
    [16u + STM32_IRQ_I2C1_ER] = I2C1_ER_IRQHandler,
    [16u + STM32_IRQ_I2C2_EV] = I2C2_EV_IRQHandler,
    [16u + STM32_IRQ_I2C2_ER] = I2C2_ER_IRQHandler,
    [16u + STM32_IRQ_EXTI15_10] = EXTI15_10_IRQHandler,
    [16u + STM32_IRQ_TIM6_DACUNDER] = TIM6_DACUNDER_IRQHandler,
};

void Reset_Handler(void) {
  /* Copy initialised data from flash to RAM. */
  uint32_t *src = &_sidata;
  uint32_t *dst = &_sdata;
  while (dst < &_edata) {
    *dst++ = *src++;
  }

  /* Zero the .bss segment. */
  for (dst = &_sbss; dst < &_ebss;) {
    *dst++ = 0u;
  }

  SystemInit();
  (void)main();

  /* main() should not return; trap if it does. */
  for (;;) {
  }
}

void Default_Handler(void) {
  for (;;) {
  }
}
