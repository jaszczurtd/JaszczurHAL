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

#define JH_REG8(addr)                                                          \
  (*(volatile uint8_t *)(addr)) /* NOLINT(performance-no-int-to-ptr) */
#define JH_REG32(addr)                                                         \
  (*(volatile uint32_t *)(addr)) /* NOLINT(performance-no-int-to-ptr) */

/* ── RCC (Reset & Clock Control) ─────────────────────────────────────────── */
#define RCC_BASE 0x40021000u
#define RCC_AHB1ENR JH_REG32(RCC_BASE + 0x48u)  /* DMA / DMAMUX clocks   */
#define RCC_AHB2ENR JH_REG32(RCC_BASE + 0x4Cu)  /* GPIO port clocks      */
#define RCC_APB1ENR1 JH_REG32(RCC_BASE + 0x58u) /* TIM2.. / USART2 / SPI2 */
#define RCC_APB2ENR JH_REG32(RCC_BASE + 0x60u)  /* TIM15.. / USART1 / SPI1 */
#define RCC_CCIPR JH_REG32(RCC_BASE + 0x88u)    /* peripheral clock muxes  */
#define RCC_CSR JH_REG32(RCC_BASE + 0x94u)      /* reset flags / clear    */
#define RCC_CRRCR JH_REG32(RCC_BASE + 0x98u)    /* HSI48 control/status    */

#define RCC_AHB1ENR_DMA1EN (1u << 0)
#define RCC_AHB1ENR_DMA2EN (1u << 1)
#define RCC_AHB1ENR_DMAMUX1EN (1u << 2)

#define RCC_AHB2ENR_GPIOAEN (1u << 0)
#define RCC_AHB2ENR_GPIOBEN (1u << 1)
#define RCC_AHB2ENR_GPIOCEN (1u << 2)
#define RCC_AHB2ENR_GPIODEN (1u << 3)
#define RCC_AHB2ENR_GPIOEEN (1u << 4)
#define RCC_AHB2ENR_GPIOFEN (1u << 5)
#define RCC_AHB2ENR_GPIOGEN (1u << 6)
#define RCC_AHB2ENR_RNGEN (1u << 26)

#define RCC_APB1ENR1_TIM2EN (1u << 0)
#define RCC_APB1ENR1_TIM3EN (1u << 1)
#define RCC_APB1ENR1_TIM4EN (1u << 2)
#define RCC_APB1ENR1_TIM6EN (1u << 4)
#define RCC_APB1ENR1_TIM7EN (1u << 5)
#define RCC_APB1ENR1_USART2EN (1u << 17)
#define RCC_APB1ENR1_SPI2EN (1u << 14)
#define RCC_APB1ENR1_SPI3EN (1u << 15)
#define RCC_APB1ENR1_I2C1EN (1u << 21)
#define RCC_APB1ENR1_I2C2EN (1u << 22)
#define RCC_APB1ENR1_FDCANEN (1u << 25)

#define RCC_APB2ENR_SPI1EN (1u << 12)
#define RCC_APB2ENR_TIM15EN (1u << 16)
#define RCC_APB2ENR_TIM16EN (1u << 17)
#define RCC_APB2ENR_TIM17EN (1u << 18)
#define RCC_APB2ENR_SYSCFGEN (1u << 0)

#define RCC_AHB2ENR_DAC1EN (1u << 16)
#define RCC_AHB2ENR_ADC12EN (1u << 13)

#define RCC_CCIPR_CLK48SEL_MASK (0x3u << 26)
#define RCC_CCIPR_CLK48SEL_HSI48 (0x0u << 26)
#define RCC_CRRCR_HSI48ON (1u << 0)
#define RCC_CRRCR_HSI48RDY (1u << 1)

/* RCC_CSR reset flags (RM0440 / CMSIS stm32g474xx.h). */
#define RCC_CSR_RMVF (1u << 23)
#define RCC_CSR_OBLRSTF (1u << 25)
#define RCC_CSR_PINRSTF (1u << 26)
#define RCC_CSR_BORRSTF (1u << 27)
#define RCC_CSR_SFTRSTF (1u << 28)
#define RCC_CSR_IWDGRSTF (1u << 29)
#define RCC_CSR_WWDGRSTF (1u << 30)
#define RCC_CSR_LPWRRSTF (1u << 31)

/* ── FLASH controller (main flash erase/program) ────────────────────────── */
#define FLASH_BASE 0x40022000u
#define FLASH_ACR JH_REG32(FLASH_BASE + 0x00u)
#define FLASH_KEYR JH_REG32(FLASH_BASE + 0x08u)
#define FLASH_SR JH_REG32(FLASH_BASE + 0x10u)
#define FLASH_CR JH_REG32(FLASH_BASE + 0x14u)

#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu

#define FLASH_SR_EOP (1u << 0)
#define FLASH_SR_OPERR (1u << 1)
#define FLASH_SR_PROGERR (1u << 3)
#define FLASH_SR_WRPERR (1u << 4)
#define FLASH_SR_PGAERR (1u << 5)
#define FLASH_SR_SIZERR (1u << 6)
#define FLASH_SR_PGSERR (1u << 7)
#define FLASH_SR_MISERR (1u << 8)
#define FLASH_SR_FASTERR (1u << 9)
#define FLASH_SR_RDERR (1u << 14)
#define FLASH_SR_OPTVERR (1u << 15)
#define FLASH_SR_BSY (1u << 16)
#define FLASH_SR_ERRORS                                                        \
  (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |     \
   FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR |    \
   FLASH_SR_RDERR | FLASH_SR_OPTVERR)

#define FLASH_CR_PG (1u << 0)
#define FLASH_CR_PER (1u << 1)
#define FLASH_CR_PNB_POS 3u
#define FLASH_CR_PNB_MASK (0xFFu << FLASH_CR_PNB_POS)
#define FLASH_CR_BKER (1u << 11)
#define FLASH_CR_STRT (1u << 16)
#define FLASH_CR_LOCK (1u << 31)

/* ── ADC1 + ADC12 common (single-channel polled regular conversions) ───────
 * ADC1 inputs are single-ended; the ADC kernel clock is taken from HCLK/1
 * (CKMODE=01) so the HSI16 bring-up clock yields a 16 MHz ADC clock, in spec.
 * Register layout / bit positions per RM0440; pending on-silicon validation
 * (see examples/g474_adc_read). */
#define ADC1_BASE 0x50000000u
#define ADC12_COMMON_BASE 0x50000300u

#define ADC1_ISR JH_REG32(ADC1_BASE + 0x00u)
#define ADC1_CR JH_REG32(ADC1_BASE + 0x08u)
#define ADC1_CFGR JH_REG32(ADC1_BASE + 0x0Cu)
#define ADC1_SMPR1 JH_REG32(ADC1_BASE + 0x14u) /* sample time, ch 0..9   */
#define ADC1_SMPR2 JH_REG32(ADC1_BASE + 0x18u) /* sample time, ch 10..18 */
#define ADC1_SQR1 JH_REG32(ADC1_BASE + 0x30u)  /* regular sequence       */
#define ADC1_DR                                                                \
  JH_REG32(ADC1_BASE + 0x40u) /* regular data (read clears EOC)                \
                               */
#define ADC12_CCR JH_REG32(ADC12_COMMON_BASE + 0x08u)

#define ADC_ISR_ADRDY (1u << 0)
#define ADC_ISR_EOC (1u << 2)

#define ADC_CR_ADEN (1u << 0)
#define ADC_CR_ADDIS (1u << 1)
#define ADC_CR_ADSTART (1u << 2)
#define ADC_CR_ADSTP (1u << 4)
#define ADC_CR_ADVREGEN (1u << 28)
#define ADC_CR_DEEPPWD (1u << 29)
#define ADC_CR_ADCALDIF (1u << 30)
#define ADC_CR_ADCAL (1u << 31)

/* CFGR RES field [4:3]: 00 = 12-bit, 01 = 10-bit, 10 = 8-bit, 11 = 6-bit. */
#define ADC_CFGR_DMAEN (1u << 0)
#define ADC_CFGR_DMACFG (1u << 1)
#define ADC_CFGR_RES_POS 3u
#define ADC_CFGR_RES_MASK (0x3u << ADC_CFGR_RES_POS)
#define ADC_CFGR_OVRMOD (1u << 12)
#define ADC_CFGR_CONT (1u << 13)

/* SQR1: L (sequence length-1) [3:0], then SQ1..SQ4. */
#define ADC_SQR1_L_MASK 0xFu
#define ADC_SQR1_SQ1_POS 6u
#define ADC_SQR1_SQ2_POS 12u
#define ADC_SQR1_SQ3_POS 18u
#define ADC_SQR1_SQ4_POS 24u

/* Common CCR CKMODE [17:16]: 01 = synchronous HCLK/1. */
#define ADC_CCR_CKMODE_MASK (0x3u << 16)
#define ADC_CCR_CKMODE_HCLK_DIV1 (0x1u << 16)

/* SMPR 3-bit sample-time code: 0b110 = 247.5 ADC clock cycles (safe for
 * higher-impedance sources during bring-up). */
#define ADC_SMP_247CYCLES 0x6u

/* Busy-poll bound for one conversion (matches the I2C backend's style). */
#define ADC_POLL_TIMEOUT 200000u

/* ── RNG (true-random generator, clocked from HSI48) ────────────────────── */
#define RNG_BASE 0x50060800u
#define RNG_CR JH_REG32(RNG_BASE + 0x00u)
#define RNG_SR JH_REG32(RNG_BASE + 0x04u)
#define RNG_DR JH_REG32(RNG_BASE + 0x08u)

#define RNG_CR_RNGEN (1u << 2)
#define RNG_SR_DRDY (1u << 0)
#define RNG_SR_CECS (1u << 1)
#define RNG_SR_SECS (1u << 2)
#define RNG_SR_CEIS (1u << 5)
#define RNG_SR_SEIS (1u << 6)
#define RNG_SR_ERRORS (RNG_SR_CECS | RNG_SR_SECS | RNG_SR_CEIS | RNG_SR_SEIS)
#define RNG_POLL_TIMEOUT 200000u

/* ── DMA1 + DMAMUX1 (minimal Channel1 memory-to-peripheral support) ─────── */
#define DMA1_BASE 0x40020000u
#define DMAMUX1_BASE 0x40020800u

#define DMA_ISR(base) JH_REG32((base) + 0x00u)
#define DMA_IFCR(base) JH_REG32((base) + 0x04u)
#define DMA_CCR(base, ch) JH_REG32((base) + 0x08u + ((uint32_t)(ch) * 0x14u))
#define DMA_CNDTR(base, ch) JH_REG32((base) + 0x0Cu + ((uint32_t)(ch) * 0x14u))
#define DMA_CPAR(base, ch) JH_REG32((base) + 0x10u + ((uint32_t)(ch) * 0x14u))
#define DMA_CMAR(base, ch) JH_REG32((base) + 0x14u + ((uint32_t)(ch) * 0x14u))
#define DMAMUX_CCR(ch) JH_REG32(DMAMUX1_BASE + ((uint32_t)(ch) * 4u))

#define DMA_CCR_EN (1u << 0)
#define DMA_CCR_TCIE (1u << 1)
#define DMA_CCR_HTIE (1u << 2)
#define DMA_CCR_TEIE (1u << 3)
#define DMA_CCR_DIR (1u << 4)
#define DMA_CCR_CIRC (1u << 5)
#define DMA_CCR_PINC (1u << 6)
#define DMA_CCR_MINC (1u << 7)
#define DMA_CCR_PSIZE_16 (1u << 8)
#define DMA_CCR_MSIZE_16 (1u << 10)
#define DMA_CCR_PL_HIGH (2u << 12)

#define DMA_FLAG_GIF(ch) (1u << ((uint32_t)(ch) * 4u))
#define DMA_FLAG_TCIF(ch) (1u << (((uint32_t)(ch) * 4u) + 1u))
#define DMA_FLAG_HTIF(ch) (1u << (((uint32_t)(ch) * 4u) + 2u))
#define DMA_FLAG_TEIF(ch) (1u << (((uint32_t)(ch) * 4u) + 3u))
#define DMA_IFCR_CLEAR_ALL(ch)                                                 \
  (DMA_FLAG_GIF(ch) | DMA_FLAG_TCIF(ch) | DMA_FLAG_HTIF(ch) | DMA_FLAG_TEIF(ch))

#define DMAMUX_CCR_DMAREQ_ID_MASK 0x7Fu

/* Values from ST stm32g4xx_hal_dma.h for STM32G4 DMAMUX1 timer update
 * requests plus ADC1. */
#define DMA_REQUEST_ADC1 5u
#define DMA_REQUEST_TIM2_UP 60u
#define DMA_REQUEST_TIM3_UP 65u
#define DMA_REQUEST_TIM4_UP 71u
#define DMA_REQUEST_TIM15_UP 79u
#define DMA_REQUEST_TIM16_UP 83u
#define DMA_REQUEST_TIM17_UP 85u

/* ── DAC1 (DAC1_OUT1 = PA4, DAC1_OUT2 = PA5) ─────────────────────────────── */
#define DAC1_BASE 0x50000800u
#define DAC1_CR JH_REG32(DAC1_BASE + 0x00u)
#define DAC1_DHR12R1                                                           \
  JH_REG32(DAC1_BASE + 0x08u) /* ch1, 12-bit right-aligned                     \
                               */
#define DAC1_DHR12R2                                                           \
  JH_REG32(DAC1_BASE + 0x14u) /* ch2, 12-bit right-aligned                     \
                               */
#define DAC_CR_EN1 (1u << 0)
#define DAC_CR_EN2 (1u << 16)

/* ── TIM2 (32-bit GP timer; used as a pulse counter on TIM2_CH1 = PA0/AF1) ──
 */
#define TIM2_BASE 0x40000000u
#define TIM3_BASE 0x40000400u
#define TIM4_BASE 0x40000800u
#define TIM6_BASE 0x40001000u
#define TIM7_BASE 0x40001400u
#define TIM15_BASE 0x40014000u
#define TIM16_BASE 0x40014400u
#define TIM17_BASE 0x40014800u

#define TIM_CR1(base) JH_REG32((base) + 0x00u)
#define TIM_SMCR(base) JH_REG32((base) + 0x08u)
#define TIM_DIER(base) JH_REG32((base) + 0x0Cu)
#define TIM_SR(base) JH_REG32((base) + 0x10u)
#define TIM_EGR(base) JH_REG32((base) + 0x14u)
#define TIM_CCMR1_REG(base) JH_REG32((base) + 0x18u)
#define TIM_CCMR2_REG(base) JH_REG32((base) + 0x1Cu)
#define TIM_CCER_REG(base) JH_REG32((base) + 0x20u)
#define TIM_CNT(base) JH_REG32((base) + 0x24u)
#define TIM_PSC(base) JH_REG32((base) + 0x28u)
#define TIM_ARR(base) JH_REG32((base) + 0x2Cu)
#define TIM_CCR1(base) JH_REG32((base) + 0x34u)
#define TIM_CCR2(base) JH_REG32((base) + 0x38u)
#define TIM_CCR3(base) JH_REG32((base) + 0x3Cu)
#define TIM_CCR4(base) JH_REG32((base) + 0x40u)
#define TIM_BDTR(base) JH_REG32((base) + 0x44u)

#define TIM2_CR1 JH_REG32(TIM2_BASE + 0x00u)
#define TIM2_SMCR JH_REG32(TIM2_BASE + 0x08u) /* slave-mode / ext clock */
#define TIM2_CCMR1 JH_REG32(TIM2_BASE + 0x18u)
#define TIM2_CCER JH_REG32(TIM2_BASE + 0x20u)
#define TIM2_CNT JH_REG32(TIM2_BASE + 0x24u)
#define TIM2_ARR JH_REG32(TIM2_BASE + 0x2Cu)
#define TIM_CR1_CEN (1u << 0)
#define TIM_CR1_OPM (1u << 3)
#define TIM_CR1_ARPE (1u << 7)
#define TIM_DIER_UIE (1u << 0)
#define TIM_DIER_UDE (1u << 8)
#define TIM_SR_UIF (1u << 0)
#define TIM_EGR_UG (1u << 0)

#define TIM_CCMR1_CC1S_MASK (0x3u << 0)
#define TIM_CCMR1_OC1PE (1u << 3)
#define TIM_CCMR1_OC1M_MASK ((0x7u << 4) | (1u << 16))
#define TIM_CCMR1_OC1M_PWM1 (0x6u << 4)
#define TIM_CCMR1_CC2S_MASK (0x3u << 8)
#define TIM_CCMR1_OC2PE (1u << 11)
#define TIM_CCMR1_OC2M_MASK ((0x7u << 12) | (1u << 24))
#define TIM_CCMR1_OC2M_PWM1 (0x6u << 12)

#define TIM_CCMR2_CC3S_MASK (0x3u << 0)
#define TIM_CCMR2_OC3PE (1u << 3)
#define TIM_CCMR2_OC3M_MASK ((0x7u << 4) | (1u << 16))
#define TIM_CCMR2_OC3M_PWM1 (0x6u << 4)
#define TIM_CCMR2_CC4S_MASK (0x3u << 8)
#define TIM_CCMR2_OC4PE (1u << 11)
#define TIM_CCMR2_OC4M_MASK ((0x7u << 12) | (1u << 24))
#define TIM_CCMR2_OC4M_PWM1 (0x6u << 12)

/* CCMR1: CC1S = 01 -> IC1 mapped on TI1. */
#define TIM_CCMR1_CC1S_TI1 (0x1u << 0)
/* CCER capture-input polarity: 00 rising, 10 falling, 11 both. */
#define TIM_CCER_CC1E (1u << 0)
#define TIM_CCER_CC1P (1u << 1)
#define TIM_CCER_CC1NP (1u << 3)
#define TIM_CCER_CC2E (1u << 4)
#define TIM_CCER_CC3E (1u << 8)
#define TIM_CCER_CC4E (1u << 12)
#define TIM_BDTR_MOE (1u << 15)
/* SMCR: SMS=111 external clock mode 1; TS=101 selects TI1FP1. */
#define TIM_SMCR_SMS_EXT1 (0x7u << 0)
#define TIM_SMCR_TS_TI1FP1 (0x5u << 4)

/* ── I2C1 (I2C v2 peripheral; SCL=PB8, SDA=PB9, AF4 on Nucleo-G474RE) ───────
 */
#define I2C1_BASE 0x40005400u
#define I2C2_BASE 0x40005800u

#define I2C_CR1_REG(base) JH_REG32((base) + 0x00u)
#define I2C_CR2_REG(base) JH_REG32((base) + 0x04u)
#define I2C_TIMINGR_REG(base) JH_REG32((base) + 0x10u)
#define I2C_ISR_REG(base) JH_REG32((base) + 0x18u)
#define I2C_ICR_REG(base) JH_REG32((base) + 0x1Cu)
#define I2C_RXDR_REG(base) JH_REG32((base) + 0x24u)
#define I2C_TXDR_REG(base) JH_REG32((base) + 0x28u)

#define I2C1_CR1 JH_REG32(I2C1_BASE + 0x00u)
#define I2C1_CR2 JH_REG32(I2C1_BASE + 0x04u)
#define I2C1_TIMINGR JH_REG32(I2C1_BASE + 0x10u)
#define I2C1_ISR JH_REG32(I2C1_BASE + 0x18u)
#define I2C1_ICR JH_REG32(I2C1_BASE + 0x1Cu)
#define I2C1_RXDR JH_REG32(I2C1_BASE + 0x24u)
#define I2C1_TXDR JH_REG32(I2C1_BASE + 0x28u)

#define I2C2_CR1 JH_REG32(I2C2_BASE + 0x00u)
#define I2C2_CR2 JH_REG32(I2C2_BASE + 0x04u)
#define I2C2_TIMINGR JH_REG32(I2C2_BASE + 0x10u)
#define I2C2_ISR JH_REG32(I2C2_BASE + 0x18u)
#define I2C2_ICR JH_REG32(I2C2_BASE + 0x1Cu)
#define I2C2_RXDR JH_REG32(I2C2_BASE + 0x24u)
#define I2C2_TXDR JH_REG32(I2C2_BASE + 0x28u)

#define I2C_CR1_PE (1u << 0)
#define I2C_CR1_TXIE (1u << 1)
#define I2C_CR1_RXIE (1u << 2)
#define I2C_CR1_ADDRIE (1u << 3)
#define I2C_CR1_NACKIE (1u << 4)
#define I2C_CR1_STOPIE (1u << 5)
#define I2C_CR1_ERRIE (1u << 7)
#define I2C_OAR1_REG(base) JH_REG32((base) + 0x08u)
#define I2C_OAR1_OA1EN (1u << 15)
#define I2C_CR2_RD_WRN (1u << 10)
#define I2C_CR2_START (1u << 13)
#define I2C_CR2_STOP (1u << 14)
#define I2C_CR2_AUTOEND (1u << 25)
#define I2C_ISR_TXE (1u << 0)
#define I2C_ISR_TXIS (1u << 1)
#define I2C_ISR_RXNE (1u << 2)
#define I2C_ISR_ADDR (1u << 3)
#define I2C_ISR_NACKF (1u << 4)
#define I2C_ISR_STOPF (1u << 5)
#define I2C_ISR_TC (1u << 6)
#define I2C_ISR_BERR (1u << 8)
#define I2C_ISR_ARLO (1u << 9)
#define I2C_ISR_OVR (1u << 10)
#define I2C_ISR_BUSY (1u << 15)
#define I2C_ISR_DIR (1u << 16)
#define I2C_ICR_ADDRCF (1u << 3)
#define I2C_ICR_NACKCF (1u << 4)
#define I2C_ICR_STOPCF (1u << 5)
#define I2C_ICR_BERRCF (1u << 8)
#define I2C_ICR_ARLOCF (1u << 9)
#define I2C_ICR_OVRCF (1u << 10)

/* TIMINGR for I2CCLK = 16 MHz (HSI16 / PCLK1 default), standard mode 100 kHz.
 * Value per STM32CubeMX / RM0440 timing tables. If the core clock is later
 * raised (PLL), this must be recomputed for the new PCLK1. */
#define I2C_TIMINGR_100K_16MHZ 0x30420F13u

/* Conservative bring-up presets for I2CCLK = 16 MHz.
 * These keep the first STM32 backend dependency-free while still honoring
 * HAL_I2C_CLOCK_{STANDARD,FAST,FAST_PLUS}_HZ requests. */
#define I2C_TIMINGR_400K_16MHZ 0x1010060Cu
#define I2C_TIMINGR_1M_16MHZ 0x00100509u

/* ── SPI master (SPI1/SPI2; 8-bit full-duplex, software NSS) ─────────────── */
#define SPI1_BASE 0x40013000u
#define SPI2_BASE 0x40003800u
#define SPI3_BASE 0x40003C00u

#define SPI_CR1(base) JH_REG32((base) + 0x00u)
#define SPI_CR2(base) JH_REG32((base) + 0x04u)
#define SPI_SR(base) JH_REG32((base) + 0x08u)
#define SPI_DR(base) JH_REG32((base) + 0x0Cu)
#define SPI_DR8(base) JH_REG8((base) + 0x0Cu)

#define SPI_CR1_CPHA (1u << 0)
#define SPI_CR1_CPOL (1u << 1)
#define SPI_CR1_MSTR (1u << 2)
#define SPI_CR1_BR_POS 3u
#define SPI_CR1_BR_MASK (0x7u << SPI_CR1_BR_POS)
#define SPI_CR1_SPE (1u << 6)
#define SPI_CR1_LSBFIRST (1u << 7)
#define SPI_CR1_SSI (1u << 8)
#define SPI_CR1_SSM (1u << 9)

#define SPI_CR2_DS_POS 8u
#define SPI_CR2_DS_8BIT (0x7u << SPI_CR2_DS_POS)
#define SPI_CR2_FRXTH (1u << 12)

#define SPI_SR_RXNE (1u << 0)
#define SPI_SR_TXE (1u << 1)
#define SPI_SR_OVR (1u << 6)
#define SPI_SR_BSY (1u << 7)

#define SPI_POLL_TIMEOUT 200000u

/* ── GPIO ─────────────────────────────────────────────────────────────────
 * 7 ports A..G, each spaced 0x400 apart starting at 0x48000000.
 */
#define GPIO_PORT_BASE(idx) (0x48000000u + (uint32_t)(idx) * 0x400u)

#define GPIO_MODER(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x00u)
#define GPIO_OTYPER(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x04u)
#define GPIO_OSPEEDR(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x08u)
#define GPIO_PUPDR(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x0Cu)
#define GPIO_IDR(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x10u)
#define GPIO_ODR(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x14u)
#define GPIO_BSRR(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x18u)
#define GPIO_AFRL(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x20u)
#define GPIO_AFRH(idx) JH_REG32(GPIO_PORT_BASE(idx) + 0x24u)

/* MODER 2-bit fields: 00 input, 01 output, 10 alternate-function, 11 analog. */
#define GPIO_MODE_INPUT 0x0u
#define GPIO_MODE_OUTPUT 0x1u
#define GPIO_MODE_AF 0x2u
#define GPIO_MODE_ANALOG 0x3u

/* PUPDR 2-bit fields: 00 none, 01 pull-up, 10 pull-down. */
#define GPIO_PUPD_NONE 0x0u
#define GPIO_PUPD_UP 0x1u
#define GPIO_PUPD_DOWN 0x2u

/* ── FDCAN1 (STM32G474 native Bosch M_CAN-derived controller) ───────────── */
#define FDCAN1_BASE 0x40006400u
#define FDCAN_SRAM_BASE 0x4000AC00u

#define FDCAN_REG(base, off) JH_REG32((base) + (off))
#define FDCAN_DBTP(base) FDCAN_REG((base), 0x00Cu)
#define FDCAN_TEST(base) FDCAN_REG((base), 0x010u)
#define FDCAN_CCCR(base) FDCAN_REG((base), 0x018u)
#define FDCAN_NBTP(base) FDCAN_REG((base), 0x01Cu)
#define FDCAN_ECR(base) FDCAN_REG((base), 0x040u)
#define FDCAN_PSR(base) FDCAN_REG((base), 0x044u)
#define FDCAN_TDCR(base) FDCAN_REG((base), 0x048u)
#define FDCAN_IR(base) FDCAN_REG((base), 0x050u)
#define FDCAN_IE(base) FDCAN_REG((base), 0x054u)
#define FDCAN_RXGFC(base) FDCAN_REG((base), 0x080u)
#define FDCAN_SIDFC(base) FDCAN_REG((base), 0x084u)
#define FDCAN_XIDFC(base) FDCAN_REG((base), 0x088u)
#define FDCAN_XIDAM(base) FDCAN_REG((base), 0x090u)
#define FDCAN_RXF0C(base) FDCAN_REG((base), 0x0A0u)
#define FDCAN_RXF0S(base) FDCAN_REG((base), 0x0A4u)
#define FDCAN_RXF0A(base) FDCAN_REG((base), 0x0A8u)
#define FDCAN_RXF1C(base) FDCAN_REG((base), 0x0B0u)
#define FDCAN_RXESC(base) FDCAN_REG((base), 0x0BCu)
#define FDCAN_TXBC(base) FDCAN_REG((base), 0x0C0u)
#define FDCAN_TXFQS(base) FDCAN_REG((base), 0x0C4u)
#define FDCAN_TXESC(base) FDCAN_REG((base), 0x0C8u)
#define FDCAN_TXBRP(base) FDCAN_REG((base), 0x0CCu)
#define FDCAN_TXBAR(base) FDCAN_REG((base), 0x0D0u)
#define FDCAN_TXBCR(base) FDCAN_REG((base), 0x0D4u)
#define FDCAN_TXBTO(base) FDCAN_REG((base), 0x0D8u)
#define FDCAN_TXBCF(base) FDCAN_REG((base), 0x0DCu)
#define FDCAN_TXEFC(base) FDCAN_REG((base), 0x0F0u)

#define FDCAN_CCCR_INIT (1u << 0)
#define FDCAN_CCCR_CCE (1u << 1)
#define FDCAN_CCCR_ASM (1u << 2)
#define FDCAN_CCCR_CSR (1u << 4)
#define FDCAN_CCCR_MON (1u << 5)
#define FDCAN_CCCR_DAR (1u << 6)
#define FDCAN_CCCR_TEST (1u << 7)
#define FDCAN_CCCR_FDOE (1u << 8)
#define FDCAN_CCCR_BRSE (1u << 9)

#define FDCAN_TEST_LBCK (1u << 4)
#define FDCAN_IR_ALL 0xFFFFFFFFu
#define FDCAN_PSR_EP (1u << 5)
#define FDCAN_PSR_EW (1u << 6)
#define FDCAN_PSR_BO (1u << 7)
#define FDCAN_ECR_TEC_MASK 0xFFu
#define FDCAN_ECR_REC_MASK (0x7Fu << 8)

#define FDCAN_TXBC_TFQM (1u << 30)
#define FDCAN_ELEM_SIZE_64 7u
#define FDCAN_RXF0S_F0FL_MASK 0x0Fu
#define FDCAN_RXF0S_F0GI_MASK (0x3u << 8)
#define FDCAN_RXF0S_F0GI_POS 8u
#define FDCAN_RXF0A_F0AI_MASK 0x7u

#define FDCAN_MRAM_STD_FILTER_WORDS 28u
#define FDCAN_MRAM_EXT_FILTER_WORDS (8u * 2u)
#define FDCAN_MRAM_RX_FIFO0_ELEMS 3u
#define FDCAN_MRAM_RX_FIFO1_ELEMS 3u
#define FDCAN_MRAM_TX_EVENT_ELEMS 3u
#define FDCAN_MRAM_TX_BUF_ELEMS 3u
#define FDCAN_MRAM_ELEM_WORDS_64 18u

#define FDCAN_MRAM_STD_FILTER_WORD 0u
#define FDCAN_MRAM_EXT_FILTER_WORD                                             \
  (FDCAN_MRAM_STD_FILTER_WORD + FDCAN_MRAM_STD_FILTER_WORDS)
#define FDCAN_MRAM_RX0_WORD                                                    \
  (FDCAN_MRAM_EXT_FILTER_WORD + FDCAN_MRAM_EXT_FILTER_WORDS)
#define FDCAN_MRAM_RX1_WORD                                                    \
  (FDCAN_MRAM_RX0_WORD + (FDCAN_MRAM_RX_FIFO0_ELEMS * FDCAN_MRAM_ELEM_WORDS_64))
#define FDCAN_MRAM_TX_EVENT_WORD                                               \
  (FDCAN_MRAM_RX1_WORD + (FDCAN_MRAM_RX_FIFO1_ELEMS * FDCAN_MRAM_ELEM_WORDS_64))
#define FDCAN_MRAM_TX_BUF_WORD                                                 \
  (FDCAN_MRAM_TX_EVENT_WORD + (FDCAN_MRAM_TX_EVENT_ELEMS * 2u))

#define FDCAN_POLL_TIMEOUT 200000u

/* ── SYSCFG / EXTI (external interrupt routing) ─────────────────────────── */
#define SYSCFG_BASE 0x40010000u
#define SYSCFG_EXTICR(idx)                                                     \
  JH_REG32(SYSCFG_BASE + 0x08u + ((uint32_t)(idx) * 4u))

#define EXTI_BASE 0x40010400u
/* STM32G4 uses a combined pending register at 0x14. Do not substitute the
 * split rising/falling pending layout used by newer STM32 families. */
#define EXTI_IMR1 JH_REG32(EXTI_BASE + 0x00u)
#define EXTI_RTSR1 JH_REG32(EXTI_BASE + 0x08u)
#define EXTI_FTSR1 JH_REG32(EXTI_BASE + 0x0Cu)
#define EXTI_SWIER1 JH_REG32(EXTI_BASE + 0x10u)
#define EXTI_PR1 JH_REG32(EXTI_BASE + 0x14u)

/* ── Generic USART (v2) register accessors by peripheral base ─────────────
 * Used by the hal_uart backend for USART1 (PORT_1) and USART2 (PORT_2). The
 * debug console keeps its own dedicated USART2_* accessors below. */
#define USART_CR1(base) JH_REG32((base) + 0x00u)
#define USART_BRR(base) JH_REG32((base) + 0x0Cu)
#define USART_ISR(base) JH_REG32((base) + 0x1Cu)
#define USART_ICR(base) JH_REG32((base) + 0x20u)
#define USART_RDR(base) JH_REG32((base) + 0x24u)
#define USART_TDR(base) JH_REG32((base) + 0x28u)

#define USART_CR1_RE_BIT (1u << 2)
#define USART_CR1_TE_BIT (1u << 3)
#define USART_ISR_PE_F (1u << 0)
#define USART_ISR_FE_F (1u << 1)
#define USART_ISR_NE_F (1u << 2)
#define USART_ISR_ORE_F (1u << 3)
#define USART_ISR_RXNE_F (1u << 5)
#define USART_ISR_LBDF_F (1u << 8)
#define USART_ISR_TXE_F (1u << 7)
#define USART_ICR_PECF_F (1u << 0)
#define USART_ICR_FECF_F (1u << 1)
#define USART_ICR_NECF_F (1u << 2)
#define USART_ICR_ORECF_F (1u << 3)
#define USART_ICR_LBDCF_F (1u << 8)

#define USART1_BASE 0x40013800u
#define RCC_APB2ENR_USART1EN (1u << 14)

/* ── USART2 (APB1, used as the debug console / ST-Link VCP on Nucleo-G474RE) */
#define USART2_BASE 0x40004400u
#define USART2_CR1 JH_REG32(USART2_BASE + 0x00u)
#define USART2_BRR JH_REG32(USART2_BASE + 0x0Cu)
#define USART2_ISR JH_REG32(USART2_BASE + 0x1Cu)
#define USART2_TDR JH_REG32(USART2_BASE + 0x28u)

#define USART_CR1_UE (1u << 0)
#define USART_CR1_RE (1u << 2)
#define USART_CR1_TE (1u << 3)
#define USART_ISR_TXE (1u << 7) /* TX data register / FIFO not full */
#define USART_ISR_TC (1u << 6)  /* transmission complete            */

/* ── SysTick (Cortex-M system timer) ─────────────────────────────────────── */
#define SYSTICK_BASE 0xE000E010u
#define SYSTICK_CTRL JH_REG32(SYSTICK_BASE + 0x00u)
#define SYSTICK_LOAD JH_REG32(SYSTICK_BASE + 0x04u)
#define SYSTICK_VAL JH_REG32(SYSTICK_BASE + 0x08u)

#define SYSTICK_CTRL_ENABLE (1u << 0)
#define SYSTICK_CTRL_TICKINT (1u << 1)
#define SYSTICK_CTRL_CLKSOURCE (1u << 2) /* processor clock */

/* ── NVIC (Cortex-M interrupt controller) ───────────────────────────────── */
#define NVIC_ISER(n) JH_REG32(0xE000E100u + ((uint32_t)(n) * 4u))
#define NVIC_ICPR(n) JH_REG32(0xE000E280u + ((uint32_t)(n) * 4u))
#define NVIC_IPR8(irqn) JH_REG8(0xE000E400u + (uint32_t)(irqn))

#define TIM6_DACUNDER_IRQn 54u
#define DMA1_Channel1_IRQn 11u
#define I2C1_EV_IRQn 31u
#define I2C1_ER_IRQn 32u
#define I2C2_EV_IRQn 33u
#define I2C2_ER_IRQn 34u
#define EXTI0_IRQn 6u
#define EXTI1_IRQn 7u
#define EXTI2_IRQn 8u
#define EXTI3_IRQn 9u
#define EXTI4_IRQn 10u
#define EXTI9_5_IRQn 23u
#define EXTI15_10_IRQn 40u
#define JH_NVIC_PRIO_TIMER 0x80u

/* ── DWT cycle counter (Cortex-M debug block) ────────────────────────────── */
#define DWT_BASE 0xE0001000u
#define DWT_CTRL JH_REG32(DWT_BASE + 0x00u)
#define DWT_CYCCNT JH_REG32(DWT_BASE + 0x04u)

#define DWT_CTRL_CYCCNTENA (1u << 0)

/* ── CoreDebug (for enabling DWT CYCCNT) ─────────────────────────────────── */
#define COREDEBUG_BASE 0xE000EDF0u
#define COREDEBUG_DEMCR JH_REG32(COREDEBUG_BASE + 0x0Cu)

#define COREDEBUG_DEMCR_TRCENA (1u << 24)

/* ── SCB (System Control Block) ──────────────────────────────────────────── */
#define SCB_BASE 0xE000ED00u
#define SCB_VTOR JH_REG32(SCB_BASE + 0x08u)
#define SCB_AIRCR JH_REG32(SCB_BASE + 0x0Cu)
#define SCB_SHCSR JH_REG32(SCB_BASE + 0x24u)
#define SCB_CFSR JH_REG32(SCB_BASE + 0x28u) /* configurable fault status */
#define SCB_CFSR_MMARVALID (1u << 7)
#define SCB_HFSR JH_REG32(SCB_BASE + 0x2Cu)  /* hard fault status         */
#define SCB_MMFAR JH_REG32(SCB_BASE + 0x34u) /* mem-manage fault address  */
#define SCB_BFAR JH_REG32(SCB_BASE + 0x38u)  /* bus fault address         */
#define SCB_CPACR JH_REG32(SCB_BASE + 0x88u) /* coprocessor (FPU) access  */

#define SCB_SHCSR_MEMFAULTENA (1u << 16)
#define SCB_SHCSR_BUSFAULTENA (1u << 17)
#define SCB_SHCSR_USGFAULTENA (1u << 18)

/* AIRCR: write VECTKEY in the top half-word, SYSRESETREQ to reboot. */
#define SCB_AIRCR_VECTKEY (0x05FAu << 16)
#define SCB_AIRCR_SYSRESETREQ (1u << 2)

/* CPACR: full access to CP10 & CP11 (the FPU). */
#define SCB_CPACR_FPU_FULL (0xFu << 20)

/* ── MPU (Cortex-M4 memory protection unit) ─────────────────────────────── */
#define MPU_BASE 0xE000ED90u
#define MPU_TYPE JH_REG32(MPU_BASE + 0x00u)
#define MPU_CTRL JH_REG32(MPU_BASE + 0x04u)
#define MPU_RNR JH_REG32(MPU_BASE + 0x08u)
#define MPU_RBAR JH_REG32(MPU_BASE + 0x0Cu)
#define MPU_RASR JH_REG32(MPU_BASE + 0x10u)

#define MPU_CTRL_ENABLE (1u << 0)
#define MPU_CTRL_PRIVDEFENA (1u << 2)
#define MPU_RASR_ENABLE (1u << 0)
#define MPU_RASR_SIZE_32B (4u << 1)
#define MPU_RASR_XN (1u << 28)
#define MPU_RBAR_ADDR_MASK 0xFFFFFFE0u

/* ── Device electronic signature ─────────────────────────────────────────── */
#define STM32_UID_BASE 0x1FFF7590u /* 96-bit unique device ID (3 words) */

/* ── Core clock after reset ──────────────────────────────────────────────── */
/* STM32G4 boots on HSI16 (16 MHz). The first bring-up keeps this default
 * clock instead of configuring the PLL: correctness over speed. */
#define JH_G474_CORE_CLOCK_HZ 16000000u

/* ── Peripheral (APB) kernel clocks ──────────────────────────────────────── */
/* SPI/UART baud is derived from the APB clock (PCLK), which need NOT equal the
 * core clock once the PLL and APB prescalers are configured. With the current
 * HSI16 bring-up (no PLL, no APB prescaler) both PCLKs equal the core clock.
 * SPI1 is on APB2 (PCLK2), SPI2 on APB1 (PCLK1). STM32 TIM kernels are a
 * special case: when the APB prescaler is not 1, timer clock is 2 * PCLKx.
 * Current PWM code assumes APB prescaler == 1 and uses these PCLK values as
 * timer clocks; update both contracts alongside any clock-tree change. */
#define JH_G474_PCLK1_HZ JH_G474_CORE_CLOCK_HZ /* APB1 -> SPI2 */
#define JH_G474_PCLK2_HZ JH_G474_CORE_CLOCK_HZ /* APB2 -> SPI1 */
