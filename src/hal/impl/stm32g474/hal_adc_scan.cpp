#include "hal/core/hal_compiler.h"
#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_ADC_SCAN
#include "hal/analog/jh_adc_scan_backend.h"
#include "hal/analog/jh_adc_scan_ring.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "stm32g474_adc_shared.h"

#ifdef JH_STM32G474_HW
#include "port/stm32g474_adc_channels.h"
#include "port/stm32g474_regs.h"
#endif

namespace {

// ADC1 converts the regular sequence continuously and DMA1 channel 3 writes
// the results circularly over both halves of the caller buffer: the
// half-transfer interrupt completes block 0, transfer-complete block 1.
// DACless keeps channels 1 and 2 with their own interrupt; both paths share
// the ADC1 ownership flag in stm32g474_adc_shared and exclude each other.
constexpr uint8_t kDmaChannel = 2u;             /* DMA1 Channel3 */
constexpr uint32_t kAdcClockHz = 42500000u;     /* HCLK / 4 */
constexpr uint32_t kConversionHalfCycles = 25u; /* 12.5 cycles */
constexpr uint32_t kMaxSequence = 8u;

// SMPR field codes and their sample times in half clock cycles.
constexpr uint32_t kSampleHalfCycles[8] = {5u,  13u,  25u,  49u,
                                           95u, 185u, 495u, 1281u};

struct scan_state_t {
  hal_adc_scan_config_t config;
  uint32_t samples_per_block;
  bool adc_owned;
  bool started;
  bool temperature;
  volatile uint32_t sequence;
  volatile uint8_t completed;
  volatile uint32_t completed_us;
  volatile uint32_t marker;
};

scan_state_t s = {};

uint16_t *half(uint8_t index) {
  return s.config.buffer + ((uint32_t)index * s.samples_per_block);
}

#ifdef JH_STM32G474_HW
bool channel_for_pin(uint8_t pin, uint32_t *channel) {
  if (pin == HAL_ADC_SCAN_PIN_TEMPERATURE) {
    *channel = ADC_CHANNEL_VSENSE;
    return true;
  }
  *channel = jh_stm32g474_adc1_channel_for_pin(pin);
  return *channel != 0u;
}

void stop_conversion(void) {
  if ((ADC1_CR & ADC_CR_ADSTART) != 0u) {
    ADC1_CR |= ADC_CR_ADSTP;
    uint32_t timeout = ADC_POLL_TIMEOUT;
    while ((ADC1_CR & ADC_CR_ADSTP) != 0u && timeout > 0u) {
      --timeout;
    }
  }
}

void set_sample_time(uint32_t channel, uint32_t code) {
  if (channel < 10u) {
    ADC1_SMPR1 =
        (ADC1_SMPR1 & ~(0x7u << (channel * 3u))) | (code << (channel * 3u));
  } else {
    const uint32_t s2 = channel - 10u;
    ADC1_SMPR2 = (ADC1_SMPR2 & ~(0x7u << (s2 * 3u))) | (code << (s2 * 3u));
  }
}

void complete_block(uint8_t index) {
  s.marker =
      s.config.marker != NULL ? s.config.marker(s.config.marker_user) : 0u;
  s.completed_us = hal_micros();
  s.completed = index;
  s.sequence = s.sequence + 1u;
}
#endif

bool scan_reader(uint8_t pin, uint16_t *raw) {
  for (uint8_t i = 0u; i < s.config.pin_count; ++i) {
    if (s.config.pins[i] == pin) {
      return jh_adc_scan_latest(i, raw) == HAL_OK;
    }
  }
  return false;
}

} // namespace

#ifdef JH_STM32G474_HW
extern "C" void DMA1_Channel3_IRQHandler(void) {
  const uint32_t status = DMA_ISR(DMA1_BASE);
  const uint32_t clear = status & DMA_IFCR_CLEAR_ALL(kDmaChannel);
  if (clear != 0u) {
    DMA_IFCR(DMA1_BASE) = clear;
  }
  if (!s.started) {
    return;
  }
  if ((status & DMA_FLAG_TEIF(kDmaChannel)) != 0u) {
    // A transfer error leaves the block sequence where it is; the consumer
    // sees no new blocks and the owner restarts the scan.
    DMA_CCR(DMA1_BASE, kDmaChannel) &= ~DMA_CCR_EN;
    return;
  }
  if ((status & DMA_FLAG_HTIF(kDmaChannel)) != 0u) {
    complete_block(0u);
  }
  if ((status & DMA_FLAG_TCIF(kDmaChannel)) != 0u) {
    complete_block(1u);
  }
}
#endif

hal_status_t jh_adc_scan_start(const hal_adc_scan_config_t *config,
                               uint8_t *positions, uint32_t *frame_period_ns) {
#ifndef JH_STM32G474_HW
  (void)config;
  (void)positions;
  (void)frame_period_ns;
  return HAL_EUNSUPPORTED;
#else
  if (s.started || s.adc_owned) {
    return HAL_EBUSY;
  }
  if (config->pin_count > kMaxSequence) {
    return HAL_EUNSUPPORTED;
  }
  uint32_t channels[HAL_ADC_SCAN_MAX_PINS] = {};
  bool temperature = false;
  for (uint8_t i = 0u; i < config->pin_count; ++i) {
    if (!channel_for_pin(config->pins[i], &channels[i])) {
      return HAL_EINVAL;
    }
    temperature =
        temperature || config->pins[i] == HAL_ADC_SCAN_PIN_TEMPERATURE;
    positions[i] = i; /* the regular sequence keeps the configured order */
  }
  // The sequence runs back to back, so the conversion period is the sample
  // time plus 12.5 cycles: pick the shortest sample time that is not faster
  // than requested. The temperature sensor needs its documented minimum.
  const uint64_t wanted_half_cycles =
      ((uint64_t)config->conversion_period_ns * kAdcClockHz * 2u +
       999999999ull) /
      1000000000ull;
  uint32_t code = 0u;
  while (code < 7u &&
         (uint64_t)kSampleHalfCycles[code] + kConversionHalfCycles <
             wanted_half_cycles) {
    ++code;
  }
  if ((uint64_t)kSampleHalfCycles[code] + kConversionHalfCycles <
      wanted_half_cycles) {
    return HAL_EUNSUPPORTED;
  }
  if (temperature && code < 6u) {
    code = 6u; /* 247.5 cycles: about 5.8 us for the internal sensor */
  }

  s.config = *config;
  s.samples_per_block = config->block_frames * (uint32_t)config->pin_count;
  s.temperature = temperature;
  s.sequence = 0u;
  s.completed = 0u;
  s.completed_us = 0u;
  s.marker = 0u;

  const hal_status_t owned = stm32g474_adc_acquire_dma();
  if (owned != HAL_OK) {
    return owned;
  }
  s.adc_owned = true;

  stop_conversion();
  uint32_t sqr1 = (uint32_t)(config->pin_count - 1u) & ADC_SQR1_L_MASK;
  uint32_t sqr2 = 0u;
  for (uint8_t i = 0u; i < config->pin_count; ++i) {
    set_sample_time(channels[i], code);
    if (i < 4u) {
      sqr1 |= channels[i] << (ADC_SQR1_SQ1_POS + (6u * i));
    } else {
      sqr2 |= channels[i] << (6u * (i - 4u));
    }
  }
  if (temperature) {
    ADC12_CCR |= ADC_CCR_VSENSESEL;
    hal_delay_us(ADC_INTERNAL_CHANNEL_STARTUP_US);
  }
  ADC1_SQR1 = sqr1;
  ADC1_SQR2 = sqr2;
  ADC1_CFGR = (ADC1_CFGR & ~ADC_CFGR_RES_MASK) | ADC_CFGR_DMAEN |
              ADC_CFGR_DMACFG | ADC_CFGR_OVRMOD | ADC_CFGR_CONT;

  RCC_AHB1ENR |= RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN;
  DMA_CCR(DMA1_BASE, kDmaChannel) &= ~DMA_CCR_EN;
  DMAMUX_CCR(kDmaChannel) = DMA_REQUEST_ADC1 & DMAMUX_CCR_DMAREQ_ID_MASK;
  DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(kDmaChannel);
  DMA_CPAR(DMA1_BASE, kDmaChannel) = (uint32_t)(uintptr_t)&ADC1_DR;
  DMA_CMAR(DMA1_BASE, kDmaChannel) = (uint32_t)(uintptr_t)config->buffer;
  DMA_CNDTR(DMA1_BASE, kDmaChannel) = s.samples_per_block * 2u;
  DMA_CCR(DMA1_BASE, kDmaChannel) =
      DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PSIZE_16 | DMA_CCR_MSIZE_16 |
      DMA_CCR_PL_HIGH | DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_TEIE;
  NVIC_IPR8(DMA1_Channel3_IRQn) = JH_NVIC_PRIO_TIMER;
  NVIC_ICPR(DMA1_Channel3_IRQn / 32u) = 1u << (DMA1_Channel3_IRQn % 32u);
  NVIC_ISER(DMA1_Channel3_IRQn / 32u) = 1u << (DMA1_Channel3_IRQn % 32u);
  DMA_CCR(DMA1_BASE, kDmaChannel) |= DMA_CCR_EN;

  stm32g474_adc_set_scan_reader(scan_reader);
  ADC1_ISR = ADC_ISR_EOC;
  ADC1_CR |= ADC_CR_ADSTART;
  s.started = true;

  const uint64_t frame_half_cycles =
      ((uint64_t)kSampleHalfCycles[code] + kConversionHalfCycles) *
      config->pin_count;
  *frame_period_ns = (uint32_t)((frame_half_cycles * 1000000000ull) /
                                ((uint64_t)kAdcClockHz * 2u));
  return HAL_OK;
#endif
}

hal_status_t jh_adc_scan_stop(void) {
#ifdef JH_STM32G474_HW
  if (s.started) {
    stop_conversion();
    stm32g474_adc_set_scan_reader(NULL);
  }
  NVIC_ICER(DMA1_Channel3_IRQn / 32u) = 1u << (DMA1_Channel3_IRQn % 32u);
  DMA_CCR(DMA1_BASE, kDmaChannel) &= ~DMA_CCR_EN;
  DMAMUX_CCR(kDmaChannel) = 0u;
  DMA_IFCR(DMA1_BASE) = DMA_IFCR_CLEAR_ALL(kDmaChannel);
  if (s.started) {
    ADC1_CFGR &=
        ~(ADC_CFGR_DMAEN | ADC_CFGR_DMACFG | ADC_CFGR_OVRMOD | ADC_CFGR_CONT);
    ADC1_ISR = ADC_ISR_EOC;
    if (s.temperature) {
      ADC12_CCR &= ~ADC_CCR_VSENSESEL;
    }
    s.started = false;
  }
  if (s.adc_owned) {
    stm32g474_adc_release_dma();
    s.adc_owned = false;
  }
#endif
  s.started = false;
  s.adc_owned = false;
  return HAL_OK;
}

bool jh_adc_scan_completed(hal_adc_scan_block_t *block) {
#ifndef JH_STM32G474_HW
  (void)block;
  return false;
#else
  hal_critical_section_enter();
  const uint32_t sequence = s.sequence;
  const uint8_t completed = s.completed;
  const uint32_t completed_us = s.completed_us;
  const uint32_t marker = s.marker;
  hal_critical_section_exit();
  if (!s.started || sequence == 0u) {
    return false;
  }
  jh_adc_scan_describe(block, half(completed), s.config.block_frames,
                       s.config.pin_count, sequence, completed_us, marker);
  return true;
#endif
}

hal_status_t jh_adc_scan_latest(uint8_t position, uint16_t *raw) {
#ifndef JH_STM32G474_HW
  (void)position;
  (void)raw;
  return HAL_EUNSUPPORTED;
#else
  if (!s.started || position >= s.config.pin_count) {
    return HAL_ESTATE;
  }
  // CNDTR counts down over both halves; what it has left says which half is
  // being written and how many complete frames it already holds. At a half
  // boundary the other half is the complete one even before its interrupt
  // has run, so the frame choice never leans on that bookkeeping.
  const uint32_t total = s.samples_per_block * 2u;
  const uint32_t remaining = DMA_CNDTR(DMA1_BASE, kDmaChannel);
  const uint32_t written = remaining <= total ? total - remaining : 0u;
  const uint8_t busy = written >= s.samples_per_block ? 1u : 0u;
  hal_critical_section_enter();
  const uint32_t sequence = s.sequence;
  const uint8_t completed = s.completed;
  hal_critical_section_exit();
  uint8_t index = 0u;
  uint32_t frame = 0u;
  if (!jh_adc_scan_latest_frame(busy, written % s.samples_per_block,
                                s.config.pin_count, s.config.block_frames,
                                sequence, completed, &index, &frame)) {
    return HAL_EAGAIN;
  }
  *raw = half(index)[(frame * s.config.pin_count) + position];
  return HAL_OK;
#endif
}

#endif // HAL_ENABLE_ADC_SCAN
#endif // HAL_TARGET_IS_STM32G474
