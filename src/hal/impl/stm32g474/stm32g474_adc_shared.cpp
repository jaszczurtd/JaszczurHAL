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
static bool s_dma_owned = false;

#ifndef JH_STM32G474_HW
/* Host-sanity build: no registers to touch - keep a benign zero-filled store.
 */
static int s_adc_values[128] = {};
#endif

static stm32g474_adc_scan_reader_fn s_scan_reader = NULL;

/* Scanned samples are 12-bit codes; present them at the configured
 * resolution like a polled conversion. */
static int scale_scanned(uint16_t raw) {
  const uint32_t max_in = (1u << 12u) - 1u;
  const uint32_t max_out = (1u << s_resolution) - 1u;
  return (int)(((uint32_t)raw * max_out + (max_in / 2u)) / max_in);
}

static bool adc_ensure_mutex(void) {
  return jh_hal_mutex_try_create_once(&s_adc_mutex) != nullptr;
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

  /* A boot stage may already have enabled ADC1. In that case do not clear
   * ADRDY and wait for an edge which cannot occur while ADEN remains set. */
  if ((ADC1_CR & ADC_CR_ADEN) != 0u) {
    adc_apply_resolution();
    s_adc_ready = true;
    return;
  }

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
  if (!adc_ensure_mutex()) {
    return;
  }
  hal_mutex_lock(s_adc_mutex);
  s_resolution = bits;
#ifdef JH_STM32G474_HW
  if (s_adc_ready && !s_dma_owned) {
    adc_apply_resolution();
  }
#else
  (void)s_resolution;
#endif
  hal_mutex_unlock(s_adc_mutex);
}

int stm32g474_adc_read_gpio(uint8_t pin) {
  if (!adc_ensure_mutex()) {
    return 0;
  }
  hal_mutex_lock(s_adc_mutex);
  int val;
  if (s_dma_owned) {
    // A running scan owns the converter; a pin it does not carry has no
    // sample to offer, and a one-shot conversion would break the scan.
    uint16_t raw = 0u;
    const bool scanned = s_scan_reader != NULL && s_scan_reader(pin, &raw);
    hal_mutex_unlock(s_adc_mutex);
    return scanned ? scale_scanned(raw) : -1;
  }
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
  if (!adc_ensure_mutex()) {
    return 0u;
  }
  hal_mutex_lock(s_adc_mutex);
  uint16_t raw;
  if (s_dma_owned) {
    hal_mutex_unlock(s_adc_mutex);
    return 0u;
  }
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
  if (!adc_ensure_mutex()) {
    return 0u;
  }
  hal_mutex_lock(s_adc_mutex);
  uint16_t raw;
  if (s_dma_owned) {
    hal_mutex_unlock(s_adc_mutex);
    return 0u;
  }
#ifdef JH_STM32G474_HW
  adc1_hw_init();
  raw = adc1_hw_read_internal(ADC_CHANNEL_VREFINT, ADC_CCR_VREFEN);
#else
  raw = 0u;
#endif
  hal_mutex_unlock(s_adc_mutex);
  return raw;
}

hal_status_t stm32g474_adc_read_internal_pair(uint16_t *out_temp_raw,
                                              uint16_t *out_vref_raw) {
  if (out_temp_raw == nullptr || out_vref_raw == nullptr) {
    return HAL_EINVAL;
  }
  if (!adc_ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_adc_mutex);
  if (s_dma_owned) {
    hal_mutex_unlock(s_adc_mutex);
    return HAL_EBUSY;
  }
#ifdef JH_STM32G474_HW
  adc1_hw_init();
  const uint16_t temp_raw =
      adc1_hw_read_internal(ADC_CHANNEL_VSENSE, ADC_CCR_VSENSESEL);
  const uint16_t vref_raw =
      adc1_hw_read_internal(ADC_CHANNEL_VREFINT, ADC_CCR_VREFEN);
#else
  const uint16_t temp_raw = 0u;
  const uint16_t vref_raw = 0u;
#endif
  hal_mutex_unlock(s_adc_mutex);
  *out_temp_raw = temp_raw;
  *out_vref_raw = vref_raw;
  return HAL_OK;
}

hal_status_t stm32g474_adc_acquire_dma(void) {
  if (!adc_ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_adc_mutex);
  if (s_dma_owned) {
    hal_mutex_unlock(s_adc_mutex);
    return HAL_EBUSY;
  }
#ifdef JH_STM32G474_HW
  adc1_hw_init();
#endif
  s_dma_owned = true;
  hal_mutex_unlock(s_adc_mutex);
  return HAL_OK;
}

void stm32g474_adc_release_dma(void) {
  if (!adc_ensure_mutex()) {
    return;
  }
  hal_mutex_lock(s_adc_mutex);
  if (s_dma_owned) {
#ifdef JH_STM32G474_HW
    if (s_adc_ready) {
      adc_apply_resolution();
    }
#endif
    s_dma_owned = false;
  }
  s_scan_reader = NULL;
  hal_mutex_unlock(s_adc_mutex);
}

void stm32g474_adc_set_scan_reader(stm32g474_adc_scan_reader_fn reader) {
  if (!adc_ensure_mutex()) {
    return;
  }
  hal_mutex_lock(s_adc_mutex);
  s_scan_reader = reader;
  hal_mutex_unlock(s_adc_mutex);
}

#endif // HAL_TARGET_IS_STM32G474
