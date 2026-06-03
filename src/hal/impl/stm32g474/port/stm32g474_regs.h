#pragma once

/**
 * @file stm32g474_regs.h
 * @brief Minimal, self-contained register map for the STM32G474 bring-up.
 *
 * This is a deliberately tiny "CMSIS-light" header. It defines ONLY the
 * peripherals the first real backend step needs (RCC, GPIO, USART2, SysTick,
 * SCB). It exists so the first hardware bring-up has zero dependency on the
 * full STM32CubeG4 package; once the backend grows past the blink milestone
 * the recommended path is to pull CubeG4 LL drivers and replace this header.
 *
 * Addresses/bit positions are per RM0440 (STM32G4 reference manual) and the
 * Arm Cortex-M4 architecture (system control space).
 *
 * Only compiled meaningfully on the ARM target.
 */

#include <stdint.h>

#define JH_REG32(addr) (*(volatile uint32_t *)(addr))

/* ── RCC (Reset & Clock Control) ─────────────────────────────────────────── */
#define RCC_BASE        0x40021000u
#define RCC_AHB2ENR     JH_REG32(RCC_BASE + 0x4Cu)   /* GPIO port clocks      */
#define RCC_APB1ENR1    JH_REG32(RCC_BASE + 0x58u)   /* USART2 clock          */

#define RCC_AHB2ENR_GPIOAEN (1u << 0)
#define RCC_AHB2ENR_GPIOBEN (1u << 1)
#define RCC_AHB2ENR_GPIOCEN (1u << 2)
#define RCC_AHB2ENR_GPIODEN (1u << 3)
#define RCC_AHB2ENR_GPIOEEN (1u << 4)
#define RCC_AHB2ENR_GPIOFEN (1u << 5)
#define RCC_AHB2ENR_GPIOGEN (1u << 6)

#define RCC_APB1ENR1_USART2EN (1u << 17)

/* ── GPIO ─────────────────────────────────────────────────────────────────
 * 7 ports A..G, each spaced 0x400 apart starting at 0x48000000.
 */
#define GPIO_PORT_BASE(idx) (0x48000000u + (uint32_t)(idx) * 0x400u)

#define GPIO_MODER(idx)  JH_REG32(GPIO_PORT_BASE(idx) + 0x00u)
#define GPIO_OTYPER(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x04u)
#define GPIO_OSPEEDR(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x08u)
#define GPIO_PUPDR(idx)  JH_REG32(GPIO_PORT_BASE(idx) + 0x0Cu)
#define GPIO_IDR(idx)    JH_REG32(GPIO_PORT_BASE(idx) + 0x10u)
#define GPIO_ODR(idx)    JH_REG32(GPIO_PORT_BASE(idx) + 0x14u)
#define GPIO_BSRR(idx)   JH_REG32(GPIO_PORT_BASE(idx) + 0x18u)
#define GPIO_AFRL(idx)   JH_REG32(GPIO_PORT_BASE(idx) + 0x20u)
#define GPIO_AFRH(idx)   JH_REG32(GPIO_PORT_BASE(idx) + 0x24u)

/* MODER 2-bit fields: 00 input, 01 output, 10 alternate-function, 11 analog. */
#define GPIO_MODE_INPUT  0x0u
#define GPIO_MODE_OUTPUT 0x1u
#define GPIO_MODE_AF     0x2u
#define GPIO_MODE_ANALOG 0x3u

/* PUPDR 2-bit fields: 00 none, 01 pull-up, 10 pull-down. */
#define GPIO_PUPD_NONE   0x0u
#define GPIO_PUPD_UP     0x1u
#define GPIO_PUPD_DOWN   0x2u

/* ── USART2 (APB1, used as the debug console / ST-Link VCP on Nucleo-G474RE)  */
#define USART2_BASE     0x40004400u
#define USART2_CR1      JH_REG32(USART2_BASE + 0x00u)
#define USART2_BRR      JH_REG32(USART2_BASE + 0x0Cu)
#define USART2_ISR      JH_REG32(USART2_BASE + 0x1Cu)
#define USART2_TDR      JH_REG32(USART2_BASE + 0x28u)

#define USART_CR1_UE    (1u << 0)
#define USART_CR1_RE    (1u << 2)
#define USART_CR1_TE    (1u << 3)
#define USART_ISR_TXE   (1u << 7)   /* TX data register / FIFO not full */
#define USART_ISR_TC    (1u << 6)   /* transmission complete            */

/* ── SysTick (Cortex-M system timer) ─────────────────────────────────────── */
#define SYSTICK_BASE    0xE000E010u
#define SYSTICK_CTRL    JH_REG32(SYSTICK_BASE + 0x00u)
#define SYSTICK_LOAD    JH_REG32(SYSTICK_BASE + 0x04u)
#define SYSTICK_VAL     JH_REG32(SYSTICK_BASE + 0x08u)

#define SYSTICK_CTRL_ENABLE    (1u << 0)
#define SYSTICK_CTRL_TICKINT   (1u << 1)
#define SYSTICK_CTRL_CLKSOURCE (1u << 2)   /* processor clock */

/* ── SCB (System Control Block) ──────────────────────────────────────────── */
#define SCB_BASE        0xE000ED00u
#define SCB_VTOR        JH_REG32(SCB_BASE + 0x08u)
#define SCB_AIRCR       JH_REG32(SCB_BASE + 0x0Cu)
#define SCB_SHCSR       JH_REG32(SCB_BASE + 0x24u)
#define SCB_CFSR        JH_REG32(SCB_BASE + 0x28u)   /* configurable fault status */
#define SCB_HFSR        JH_REG32(SCB_BASE + 0x2Cu)   /* hard fault status         */
#define SCB_MMFAR       JH_REG32(SCB_BASE + 0x34u)   /* mem-manage fault address  */
#define SCB_BFAR        JH_REG32(SCB_BASE + 0x38u)   /* bus fault address         */
#define SCB_CPACR       JH_REG32(SCB_BASE + 0x88u)   /* coprocessor (FPU) access  */

#define SCB_SHCSR_MEMFAULTENA (1u << 16)
#define SCB_SHCSR_BUSFAULTENA (1u << 17)
#define SCB_SHCSR_USGFAULTENA (1u << 18)

/* AIRCR: write VECTKEY in the top half-word, SYSRESETREQ to reboot. */
#define SCB_AIRCR_VECTKEY     (0x05FAu << 16)
#define SCB_AIRCR_SYSRESETREQ (1u << 2)

/* CPACR: full access to CP10 & CP11 (the FPU). */
#define SCB_CPACR_FPU_FULL    (0xFu << 20)

/* ── Device electronic signature ─────────────────────────────────────────── */
#define STM32_UID_BASE  0x1FFF7590u   /* 96-bit unique device ID (3 words) */

/* ── Core clock after reset ──────────────────────────────────────────────── */
/* STM32G4 boots on HSI16 (16 MHz). The first bring-up keeps this default
 * clock instead of configuring the PLL: correctness over speed. */
#define JH_G474_CORE_CLOCK_HZ 16000000u
