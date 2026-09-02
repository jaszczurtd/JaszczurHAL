#include "stm32g474_adc_shared.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_target.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#if HAL_TARGET_IS_STM32G474

#ifdef JH_STM32G474_HW
#include "port/stm32g474_adc_channels.h"
#include "port/stm32g474_regs.h"
#endif

static uint8_t s_resolution = 12u;
static hal_mutex_t s_adc_mutex = nullptr;

#ifndef JH_STM32G474_HW
/* Host-sanity build: no registers to touch - keep a benign zero-filled store.
 */
static int s_adc_values[128] = {};
#endif

static void adc_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_adc_mutex);
}

#ifdef JH_STM32G474_HW
/* ── Real ADC1 backend: single-ended, polled, one regular conversion ───────
 * No init entry point exists in the public API, so the first read lazily
 * brings ADC1 up (clock, regulator, calibration, enable) and routes the
 * requested pin/channel on demand. */

static bool s_adc_ready = false;

/* RES field for a requested bit count; anything unsupported falls back to 12.
 */
static uint32_t adc_res_field(uint8_t bits) {
  switch (bits) {
  case 10u:
    return 1u;
  case 8u:
    return 2u;
  case 6u:
    return 3u;
  case 12u:
  default:
    return 0u;
  }
}

static void adc_apply_resolution(void) {
  ADC1_CFGR = (ADC1_CFGR & ~ADC_CFGR_RES_MASK) |
              (adc_res_field(s_resolution) << ADC_CFGR_RES_POS);
}

static void adc1_hw_init(void) {
  if (s_adc_ready) {
    return;
  }
  /* Kernel clock = HCLK/4 (42.5 MHz); peripheral bus clock via RCC. */
  RCC_AHB2ENR |= RCC_AHB2ENR_ADC12EN;
  ADC12_CCR = (ADC12_CCR & ~ADC_CCR_CKMODE_MASK) | ADC_CCR_CKMODE_HCLK_DIV4;

  /* Leave deep-power-down and wait the documented regulator startup time. */
  ADC1_CR &= ~ADC_CR_DEEPPWD;
  ADC1_CR |= ADC_CR_ADVREGEN;
  hal_delay_us(20u);

  /* Single-ended calibration - ADEN must be 0 here (it is after reset). */
  ADC1_CR &= ~ADC_CR_ADCALDIF;
  ADC1_CR |= ADC_CR_ADCAL;
  while (ADC1_CR & ADC_CR_ADCAL) {
  }

  adc_apply_resolution();

  /* Enable the ADC and wait until it reports ready. */
  ADC1_ISR = ADC_ISR_ADRDY; /* clear ADRDY by writing 1 */
  ADC1_CR |= ADC_CR_ADEN;
  while (!(ADC1_ISR & ADC_ISR_ADRDY)) {
  }

  s_adc_ready = true;
}

/* Sample-time write + one-shot regular conversion on @p channel. Shared by
 * the external GPIO path and the internal VSENSE/VREFINT path below. */
static uint16_t adc1_hw_convert(uint32_t channel) {
  if (channel <= 9u) {
    ADC1_SMPR1 = (ADC1_SMPR1 & ~(0x7u << (channel * 3u))) |
                 (ADC_SMP_247CYCLES << (channel * 3u));
  } else {
    const uint32_t s = channel - 10u;
    ADC1_SMPR2 =
        (ADC1_SMPR2 & ~(0x7u << (s * 3u))) | (ADC_SMP_247CYCLES << (s * 3u));
  }

  ADC1_SQR1 = (channel << ADC_SQR1_SQ1_POS);

  ADC1_ISR = ADC_ISR_EOC; /* clear any stale EOC */
  ADC1_CR |= ADC_CR_ADSTART;
  uint32_t to = ADC_POLL_TIMEOUT;
  while (!(ADC1_ISR & ADC_ISR_EOC) && to) {
    --to;
  }
  if (to == 0u) {
    return 0u;
  }
  return (uint16_t)(ADC1_DR & 0xFFFFu); /* reading DR clears EOC */
}

/* Route @p pin to analog mode and run one polled conversion on its channel. */
static int adc1_hw_read_gpio(uint8_t pin) {
  const uint32_t ch = jh_stm32g474_adc1_channel_for_pin(pin);
  if (ch == 0u) {
    return 0;
  }

  const uint32_t port = (uint32_t)(pin >> 4);
  const uint32_t n = (uint32_t)(pin & 0x0Fu);
  if (port <= 6u) {
    RCC_AHB2ENR |= (1u << port);                        /* GPIOAEN..GPIOGEN */
    GPIO_MODER(port) |= (GPIO_MODE_ANALOG << (n * 2u)); /* analog (11) */
  }

  return (int)adc1_hw_convert(ch);
}

/* Enable the internal path bit in the common CCR, wait the documented
 * buffer startup time, run one conversion forced to 12-bit (matching the
 * factory calibration bytes), then disable the path again and restore
 * whatever resolution the caller had configured. */
static uint16_t adc1_hw_read_internal(uint32_t channel,
                                      uint32_t ccr_enable_bit) {
  const uint8_t saved_resolution = s_resolution;
  s_resolution = 12u;
  adc_apply_resolution();

  ADC12_CCR |= ccr_enable_bit;
  hal_delay_us(ADC_INTERNAL_CHANNEL_STARTUP_US);

  const uint16_t raw = adc1_hw_convert(channel);

  ADC12_CCR &= ~ccr_enable_bit;

  s_resolution = saved_resolution;
  adc_apply_resolution();

  return raw;
}
#endif // JH_STM32G474_HW

void stm32g474_adc_set_resolution(uint8_t bits) {
  adc_ensure_mutex();
  hal_mutex_lock(s_adc_mutex);
  s_resolution = bits;
#ifdef JH_STM32G474_HW
  if (s_adc_ready) {
    adc_apply_resolution();
  }
#else
  (void)s_resolution;
#endif
  hal_mutex_unlock(s_adc_mutex);
}

int stm32g474_adc_read_gpio(uint8_t pin) {
  adc_ensure_mutex();
  hal_mutex_lock(s_adc_mutex);
  int val;
#ifdef JH_STM32G474_HW
  adc1_hw_init();
  val = adc1_hw_read_gpio(pin);
#else
  val = (pin < 128u) ? s_adc_values[pin] : 0;
#endif
  hal_mutex_unlock(s_adc_mutex);
  return val;
}

uint16_t stm32g474_adc_read_temp_sensor_raw(void) {
  adc_ensure_mutex();
  hal_mutex_lock(s_adc_mutex);
  uint16_t raw;
#ifdef JH_STM32G474_HW
  adc1_hw_init();
  raw = adc1_hw_read_internal(ADC_CHANNEL_VSENSE, ADC_CCR_VSENSESEL);
#else
  raw = 0u;
#endif
  hal_mutex_unlock(s_adc_mutex);
  return raw;
}

uint16_t stm32g474_adc_read_vrefint_raw(void) {
  adc_ensure_mutex();
  hal_mutex_lock(s_adc_mutex);
  uint16_t raw;
#ifdef JH_STM32G474_HW
  adc1_hw_init();
  raw = adc1_hw_read_internal(ADC_CHANNEL_VREFINT, ADC_CCR_VREFEN);
#else
  raw = 0u;
#endif
  hal_mutex_unlock(s_adc_mutex);
  return raw;
}

#endif // HAL_TARGET_IS_STM32G474
