/**
 * @file startup_stm32g474.c
 * @brief Minimal C startup + interrupt vector table for STM32G474.
 *
 * Replaces ST's assembler startup with a small, readable C equivalent:
 *   1. hardware loads the initial MSP from vector[0] and jumps to Reset_Handler,
 *   2. Reset_Handler copies .data from flash to RAM, zeroes .bss,
 *   3. calls SystemInit() (clock/SysTick/FPU/fault enables), then main().
 *
 * Only the core exceptions up to SysTick (#15) are populated; the first
 * bring-up uses no peripheral IRQs, so the table is intentionally short.
 * Peripheral IRQ vectors are added as drivers that need them land.
 */

#include <stdint.h>

/* Symbols provided by the linker script. */
extern uint32_t _sidata;   /* .data init values in flash      */
extern uint32_t _sdata;    /* .data start in RAM              */
extern uint32_t _edata;    /* .data end in RAM                */
extern uint32_t _sbss;     /* .bss start                      */
extern uint32_t _ebss;     /* .bss end                        */
extern uint32_t _estack;   /* top of stack                    */

extern int  main(void);
extern void SystemInit(void);

/* Fault / system handlers (defined elsewhere; weak fallbacks below). */
void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));

/* Vector table: initial SP + 15 system exceptions. */
__attribute__((section(".isr_vector"), used))
void (*const g_vector_table[])(void) = {
    (void (*)(void))(&_estack), /* 0  Initial stack pointer        */
    Reset_Handler,              /* 1  Reset                        */
    NMI_Handler,                /* 2  NMI                          */
    HardFault_Handler,          /* 3  HardFault                    */
    MemManage_Handler,          /* 4  MemManage                    */
    BusFault_Handler,           /* 5  BusFault                     */
    UsageFault_Handler,         /* 6  UsageFault                   */
    0, 0, 0, 0,                 /* 7-10 reserved                   */
    SVC_Handler,                /* 11 SVCall                       */
    DebugMon_Handler,           /* 12 Debug monitor                */
    0,                          /* 13 reserved                     */
    PendSV_Handler,             /* 14 PendSV                       */
    SysTick_Handler,            /* 15 SysTick                      */
};

void Reset_Handler(void)
{
    /* Copy initialised data from flash to RAM. */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* Zero the .bss segment. */
    for (dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0u;
    }

    SystemInit();
    (void)main();

    /* main() should not return; trap if it does. */
    for (;;) {
    }
}

void Default_Handler(void)
{
    for (;;) {
    }
}
