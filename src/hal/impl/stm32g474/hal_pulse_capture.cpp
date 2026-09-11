#include "hal/core/hal_config.h"
#if HAL_TARGET_IS_STM32G474 && defined(HAL_ENABLE_PULSE_CAPTURE)
#include "hal/analog/jh_pulse_capture_backend.h"
#include "hal/core/hal_array.h"
#include "hal/system/hal_system.h"
#include "port/stm32g474_regs.h"

namespace {
/* TIM5 is independent of PWM/PCNT timers. DMA2 channel 8 is reserved here;
 * DMA macros use zero-based channel indices and DMAMUX indices are global.
 */
#ifdef JH_STM32G474_HW
constexpr uint32_t dma_channel = 7U;
volatile uint32_t ring[256];
uint32_t produced, consumed, last_pos, last_poll_us;
uint32_t saved_mode, saved_af;

static hal_status_t update_produced(void) {
  const uint32_t pos =
      (COUNTOF(ring) - DMA_CNDTR(DMA2_BASE, dma_channel)) % COUNTOF(ring);
  produced += (pos + COUNTOF(ring) - last_pos) % COUNTOF(ring);
  last_pos = pos;
  // Keep one slot free while a DMA write may still be in flight.
  return produced - consumed >= COUNTOF(ring) ? HAL_EOVERFLOW : HAL_OK;
}
#endif
uint32_t frequency;
bool running;
} // namespace

hal_status_t jh_pulse_capture_start(const hal_pulse_capture_config_t *config,
                                    uint32_t *clock_hz, uint16_t *stride) {
  if (config->pin != 0U)
    return HAL_EINVAL;
#ifdef JH_STM32G474_HW
  RCC_AHB1ENR |= RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_DMAMUX1EN;
  RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
  RCC_APB1ENR1 |= (1U << 3); /* TIM5EN */
  if ((TIM_CR1(TIM5_BASE) & TIM_CR1_CEN) != 0U ||
      (DMA_CCR(DMA2_BASE, dma_channel) & DMA_CCR_EN) != 0U ||
      (GPIO_MODER(0) & 3U) == GPIO_MODE_AF)
    return HAL_EBUSY;
  saved_mode = GPIO_MODER(0) & 3U;
  saved_af = GPIO_AFRL(0) & 15U;
  GPIO_MODER(0) = (GPIO_MODER(0) & ~3U) | GPIO_MODE_AF;
  GPIO_AFRL(0) = (GPIO_AFRL(0) & ~15U) | 2U; /* PA0 AF2 TIM5_CH1 */
  const uint32_t divider = JH_G474_TIMCLK1_HZ / 10000000U;
  frequency = JH_G474_TIMCLK1_HZ / divider;
  TIM_CR1(TIM5_BASE) = 0;
  TIM_SMCR(TIM5_BASE) = 0;
  TIM_PSC(TIM5_BASE) = divider - 1U;
  TIM_ARR(TIM5_BASE) = UINT32_MAX;
  TIM_EGR(TIM5_BASE) = TIM_EGR_UG;
  TIM_CNT(TIM5_BASE) = 0;
  /* Capture TI1 directly, every eighth selected edge, no digital filter. */
  TIM_CCMR1_REG(TIM5_BASE) = TIM_CCMR1_CC1S_TI1 | (3U << 2);
  TIM_CCER_REG(TIM5_BASE) =
      TIM_CCER_CC1E | (config->falling ? TIM_CCER_CC1P : 0U);
  TIM_SR(TIM5_BASE) = 0;
  DMA_CCR(DMA2_BASE, dma_channel) = 0;
  DMA_IFCR(DMA2_BASE) = DMA_IFCR_CLEAR_ALL(dma_channel);
  DMA_CPAR(DMA2_BASE, dma_channel) = (uint32_t)&TIM_CCR1(TIM5_BASE);
  DMA_CMAR(DMA2_BASE, dma_channel) = (uint32_t)ring;
  DMA_CNDTR(DMA2_BASE, dma_channel) = COUNTOF(ring);
  DMAMUX_CCR(15) = DMA_REQUEST_TIM5_CH1;
  DMA_CCR(DMA2_BASE, dma_channel) = DMA_CCR_MINC | DMA_CCR_CIRC |
                                    DMA_CCR_PSIZE_32 | DMA_CCR_MSIZE_32 |
                                    DMA_CCR_PL_HIGH | DMA_CCR_EN;
  TIM_DIER(TIM5_BASE) = TIM_DIER_CC1DE;
  TIM_CR1(TIM5_BASE) = TIM_CR1_CEN;
#else
  frequency = 10000000U;
#endif
#ifdef JH_STM32G474_HW
  produced = consumed = last_pos = 0;
  last_poll_us = hal_micros();
#endif
  running = true;
  *clock_hz = frequency;
  *stride = 8;
  return HAL_OK;
}

hal_status_t jh_pulse_capture_stop(void) {
  if (!running)
    return HAL_OK;
#ifdef JH_STM32G474_HW
  TIM_CR1(TIM5_BASE) = 0;
  TIM_DIER(TIM5_BASE) = 0;
  TIM_CCER_REG(TIM5_BASE) = 0;
  DMA_CCR(DMA2_BASE, dma_channel) = 0;
  DMA_IFCR(DMA2_BASE) = DMA_IFCR_CLEAR_ALL(dma_channel);
  DMAMUX_CCR(15) = 0;
  GPIO_MODER(0) = (GPIO_MODER(0) & ~3U) | saved_mode;
  GPIO_AFRL(0) = (GPIO_AFRL(0) & ~15U) | saved_af;
#endif
  running = false;
  return HAL_OK;
}

hal_status_t jh_pulse_capture_next(jh_pulse_capture_edge_t *edge) {
#ifdef JH_STM32G474_HW
  const uint32_t now = hal_micros();
  /* At <=100 kHz, 256 captures / 8 edges permit >20 ms before a lap.
   * Reject a 10 ms service gap rather than guessing the number of laps. */
  if (hal_elapsed_u32(now, last_poll_us, 10000U))
    return HAL_EOVERFLOW;
  last_poll_us = now;
  if ((TIM_SR(TIM5_BASE) & TIM_SR_CC1OF) != 0U)
    return HAL_EOVERFLOW;
  if ((DMA_ISR(DMA2_BASE) & DMA_FLAG_TEIF(dma_channel)) != 0U)
    return HAL_EHW;
  if (update_produced() != HAL_OK)
    return HAL_EOVERFLOW;
  if (produced == consumed)
    return HAL_EAGAIN;
  __asm volatile("dmb" ::: "memory");
  const uint32_t ticks = ring[consumed % COUNTOF(ring)];
  __asm volatile("dmb" ::: "memory");
  if (update_produced() != HAL_OK)
    return HAL_EOVERFLOW;
  ++consumed;
  const uint32_t wall_us = hal_micros();
  const uint32_t age_ticks = TIM_CNT(TIM5_BASE) - ticks;
  *edge = {ticks, wall_us -
                      (uint32_t)(((uint64_t)age_ticks * 1000000U) / frequency) -
                      1U};
  return HAL_OK;
#else
  (void)edge;
  return HAL_EAGAIN;
#endif
}
#endif
