/*
 * DACless PWM-audio driver for JaszczurHAL.
 *
 * Modeled after Brian Varren's DACless library by Brian Sullivan. The
 * buffering/callback/compatibility model is preserved while the implementation
 * uses JaszczurHAL services for PWM, ADC, timing and synchronization.
 *
 * Original project: https://github.com/brianvarren/DACless
 * Original license: MIT, Copyright (c) 2025 brian sullivan.
 */

#include "hal/hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/hal_config.h"
#ifdef HAL_ENABLE_DACLESS

#include "dacless.h"

#include "hal/hal_adc.h"
#include "hal/hal_system.h"
#include "hal/impl/shared/hal_mutex_once.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {
hal_mutex_t s_registry_mutex = nullptr;
DAClessAudio *s_instances[DACLESS_MAX_INSTANCES] = {};

bool registry_lock() {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_registry_mutex);
  if (mutex == nullptr) {
    return false;
  }
  hal_mutex_lock(mutex);
  return true;
}

void registry_unlock() {
  if (s_registry_mutex != nullptr) {
    hal_mutex_unlock(s_registry_mutex);
  }
}

uint16_t clamp_sample(uint16_t value, uint16_t max_value) {
  return (value > max_value) ? max_value : value;
}
} // namespace

float audio_rate = 0.0f;
volatile uint16_t *out_buf_ptr = nullptr;
const volatile uint16_t *adc_results_buf = nullptr;

DAClessAudio::DAClessAudio(const DAClessConfig &cfg)
    : cfg_(normalizeConfig(cfg)) {
  sampleRateInt_ = sampleRateIntForBits(cfg_.pwmBits);
  sampleRate_ =
      (float)sourceClockHz() / (float)periodTicksForBits(cfg_.pwmBits);
  samplePeriodQ16_ = samplePeriodQ16ForRate(sampleRateInt_);
  fillSilenceUnlocked();
  playBuf_ = bufferA();
  registerInstance();
}

DAClessAudio::~DAClessAudio() {
  if (lock()) {
    begun_ = false;
    muted_ = true;
    stopDmaUnlocked();
    if (pwm_ != nullptr) {
      hal_pwm_freq_stop(pwm_);
      hal_pwm_freq_destroy(pwm_);
      pwm_ = nullptr;
    }
    unlock();
  }

  unregisterInstance();

  if (mutex_ != nullptr) {
    hal_mutex_destroy(mutex_);
    mutex_ = nullptr;
  }
}

bool DAClessAudio::ensureMutex() const {
  return jh_hal_mutex_create_once(&mutex_) != nullptr;
}

bool DAClessAudio::lock() const {
  if (!ensureMutex()) {
    return false;
  }
  hal_mutex_lock(mutex_);
  return true;
}

void DAClessAudio::unlock() const {
  if (mutex_ != nullptr) {
    hal_mutex_unlock(mutex_);
  }
}

DAClessConfig DAClessAudio::normalizeConfig(const DAClessConfig &cfg) {
  DAClessConfig out = cfg;
  if (out.pwmBits < 1u) {
    out.pwmBits = 1u;
  } else if (out.pwmBits > 16u) {
    out.pwmBits = 16u;
  }

  if (out.blockSize == 0u) {
    out.blockSize = 1u;
  } else if (out.blockSize > DACLESS_MAX_BLOCK_SIZE) {
    out.blockSize = DACLESS_MAX_BLOCK_SIZE;
  }

  if (out.nAdcInputs > DACLESS_MAX_ADC_INPUTS) {
    out.nAdcInputs = DACLESS_MAX_ADC_INPUTS;
  }
  return out;
}

uint32_t DAClessAudio::sourceClockHz() {
#ifdef HAL_DACLESS_SOURCE_CLOCK_HZ
  return (uint32_t)HAL_DACLESS_SOURCE_CLOCK_HZ;
#elif HAL_TARGET_IS_RP2040
#ifdef F_CPU
  return (uint32_t)F_CPU;
#else
  return 125000000u;
#endif
#elif HAL_TARGET_IS_STM32G474
  return 16000000u;
#else
  return 125000000u;
#endif
}

uint16_t DAClessAudio::maxSampleValueForBits(uint16_t bits) {
  return (uint16_t)(periodTicksForBits(bits) - 1u);
}

uint16_t DAClessAudio::midpointForBits(uint16_t bits) {
  return (uint16_t)(periodTicksForBits(bits) / 2u);
}

uint32_t DAClessAudio::periodTicksForBits(uint16_t bits) {
  if (bits < 1u) {
    bits = 1u;
  } else if (bits > 16u) {
    bits = 16u;
  }
  return 1u << bits;
}

uint32_t DAClessAudio::sampleRateIntForBits(uint16_t bits) {
  const uint32_t ticks = periodTicksForBits(bits);
  return (sourceClockHz() + (ticks / 2u)) / ticks;
}

uint64_t DAClessAudio::samplePeriodQ16ForRate(uint32_t sample_rate_hz) {
  if (sample_rate_hz == 0u) {
    return 0u;
  }
  return (((uint64_t)1000000u << 16u) + (sample_rate_hz / 2u)) / sample_rate_hz;
}

void DAClessAudio::registerInstance() {
  if (!registry_lock()) {
    return;
  }

  for (uint8_t i = 0u; i < DACLESS_MAX_INSTANCES; ++i) {
    if (s_instances[i] == nullptr) {
      s_instances[i] = this;
      registryIndex_ = (int8_t)i;
      break;
    }
  }

  if (registryIndex_ == 0) {
    audio_rate = sampleRate_;
  } else if (registryIndex_ < 0) {
    HAL_ASSERT(false, "DAClessAudio: instance registry full");
  }

  registry_unlock();
}

void DAClessAudio::unregisterInstance() {
  if (!registry_lock()) {
    return;
  }

  if (registryIndex_ >= 0 && registryIndex_ < (int8_t)DACLESS_MAX_INSTANCES &&
      s_instances[(uint8_t)registryIndex_] == this) {
    s_instances[(uint8_t)registryIndex_] = nullptr;
  }

  if (registryIndex_ == 0) {
    audio_rate = 0.0f;
    out_buf_ptr = nullptr;
    adc_results_buf = nullptr;
  }
  registryIndex_ = -1;

  registry_unlock();
}

bool DAClessAudio::isCompatibilityOwnerUnlocked() const {
  return registryIndex_ == 0;
}

void DAClessAudio::updateCompatibilityGlobalsUnlocked() {
  if (!isCompatibilityOwnerUnlocked()) {
    return;
  }
  audio_rate = sampleRate_;
  out_buf_ptr = outBufPtr_;
  adc_results_buf = begun_ ? adcBuf_ : nullptr;
}

void DAClessAudio::fillSilenceUnlocked() {
  const uint16_t mid = midpointForBits(cfg_.pwmBits);
  for (uint16_t i = 0u; i < (DACLESS_MAX_BLOCK_SIZE * 2u); ++i) {
    pwmBuf_[i] = mid;
  }
}

void DAClessAudio::sampleAdcUnlocked() {
  if (cfg_.nAdcInputs == 0u) {
    return;
  }

  hal_adc_set_resolution(12u);
  for (uint8_t i = 0u; i < cfg_.nAdcInputs; ++i) {
    int value = hal_adc_read(cfg_.adcPins[i]);
    if (value < 0) {
      value = 0;
    } else if (value > 65535) {
      value = 65535;
    }
    adcBuf_[i] = (uint16_t)value;
  }
}

void DAClessAudio::begin() {
  if (!lock()) {
    return;
  }

  fillSilenceUnlocked();

  if (cfg_.useDma) {
    if (pwm_ != nullptr) {
      hal_pwm_freq_stop(pwm_);
      hal_pwm_freq_destroy(pwm_);
      pwm_ = nullptr;
    }
    if (!startDmaUnlocked()) {
      HAL_ASSERT(false, "DAClessAudio::begin: DMA audio create failed");
      unlock();
      return;
    }
  } else {
    stopDmaUnlocked();
    if (pwm_ == nullptr) {
      pwm_ = hal_pwm_freq_create(cfg_.pinPWM, sampleRateInt_,
                                 periodTicksForBits(cfg_.pwmBits));
      if (pwm_ == nullptr) {
        HAL_ASSERT(false, "DAClessAudio::begin: PWM channel create failed");
        unlock();
        return;
      }
    }
    sampleAdcUnlocked();
  }

  playBuf_ = bufferA();
  playIndex_ = 0u;
  outBufPtr_ = nullptr;
  bufReady_ = false;
  callbackInProgress_ = false;
  nextSampleDueQ16_ = hal_micros64() << 16u;
  begun_ = true;
  muted_ = false;
  updateCompatibilityGlobalsUnlocked();

  unlock();
}

void DAClessAudio::setSampleCallback(SampleCallback cb, void *userdata) {
  if (!lock()) {
    return;
  }
  sampleCb_ = cb;
  userPtr_ = userdata;
  unlock();
}

void DAClessAudio::setBlockCallback(BlockCallback cb, void *userdata) {
  if (!lock()) {
    return;
  }
  blockCb_ = cb;
  userPtr_ = userdata;
  unlock();
}

void DAClessAudio::mute() {
  if (!lock()) {
    return;
  }

  muted_ = true;
  if (dmaActive_ && dma_ != nullptr) {
    hal_dma_pwm_audio_pause(dma_, midpointForBits(cfg_.pwmBits));
  } else if (pwm_ != nullptr) {
    hal_pwm_freq_write(pwm_, midpointForBits(cfg_.pwmBits));
    hal_pwm_freq_stop(pwm_);
  }

  unlock();
}

void DAClessAudio::unmute() {
  if (!lock()) {
    return;
  }
  muted_ = false;
  if (dmaActive_ && dma_ != nullptr) {
    hal_dma_pwm_audio_resume(dma_);
  }
  nextSampleDueQ16_ = hal_micros64() << 16u;
  unlock();
}

uint16_t DAClessAudio::getADC(uint8_t channel) const {
  if (hal_in_isr()) {
    return (channel < cfg_.nAdcInputs) ? adcBuf_[channel] : 0u;
  }

  if (!lock()) {
    return 0u;
  }
  uint16_t value = 0u;
  if (channel < cfg_.nAdcInputs) {
    value = adcBuf_[channel];
  }
  unlock();
  return value;
}

void DAClessAudio::writeCurrentSampleUnlocked() {
  if (pwm_ == nullptr || playBuf_ == nullptr) {
    return;
  }

  const uint16_t max_value = maxSampleValueForBits(cfg_.pwmBits);
  const uint16_t sample = clamp_sample(playBuf_[playIndex_], max_value);
  hal_pwm_freq_write(pwm_, sample);
  playIndex_++;
}

void DAClessAudio::prepareFinishedBuffer(uint16_t *buffer) {
  outBufPtr_ = buffer;
  out_buf_ptr = outBufPtr_;
  bufReady_ = true;
  if (!dmaActive_) {
    sampleAdcUnlocked();
  }
  updateCompatibilityGlobalsUnlocked();
}

void DAClessAudio::fillBufferWithCallback(uint16_t *buffer,
                                          SampleCallback sample_cb,
                                          BlockCallback block_cb, void *user) {
  if (buffer == nullptr) {
    return;
  }

  if (block_cb != nullptr) {
    block_cb(user, buffer);
  } else if (sample_cb != nullptr) {
    for (uint16_t i = 0u; i < cfg_.blockSize; ++i) {
      buffer[i] = sample_cb(user);
    }
  } else {
    const uint16_t mid = midpointForBits(cfg_.pwmBits);
    for (uint16_t i = 0u; i < cfg_.blockSize; ++i) {
      buffer[i] = mid;
    }
  }
}

bool DAClessAudio::startDmaUnlocked() {
  if (!hal_dma_pwm_audio_supported()) {
    return false;
  }

  stopDmaUnlocked();

  hal_dma_pwm_audio_config_t dma_cfg = {};
  dma_cfg.pwm_pin = cfg_.pinPWM;
  dma_cfg.sample_rate_hz = sampleRateInt_;
  dma_cfg.period_ticks = periodTicksForBits(cfg_.pwmBits);
  dma_cfg.buffer_a = bufferA();
  dma_cfg.buffer_b = bufferB();
  dma_cfg.block_size = cfg_.blockSize;
  dma_cfg.idle_value = midpointForBits(cfg_.pwmBits);
  dma_cfg.adc_pins = cfg_.adcPins;
  dma_cfg.adc_count = cfg_.nAdcInputs;
  dma_cfg.adc_buffer = adcBuf_;
  dma_cfg.buffer_done_cb = dmaBufferDoneThunk;
  dma_cfg.user = this;

  dma_ = hal_dma_pwm_audio_create(&dma_cfg);
  if (dma_ == nullptr) {
    return false;
  }
  if (!hal_dma_pwm_audio_start(dma_)) {
    hal_dma_pwm_audio_destroy(dma_);
    dma_ = nullptr;
    return false;
  }

  dmaActive_ = true;
  return true;
}

void DAClessAudio::stopDmaUnlocked() {
  dmaActive_ = false;
  if (dma_ != nullptr) {
    hal_dma_pwm_audio_stop(dma_);
    hal_dma_pwm_audio_destroy(dma_);
    dma_ = nullptr;
  }
}

void DAClessAudio::dmaBufferDoneThunk(void *user, uint16_t *buffer,
                                      uint8_t buffer_index) {
  if (user == nullptr) {
    return;
  }
  static_cast<DAClessAudio *>(user)->onDmaBufferDone(buffer, buffer_index);
}

void DAClessAudio::onDmaBufferDone(uint16_t *buffer, uint8_t buffer_index) {
  (void)buffer_index;
  if (buffer == nullptr || muted_ || callbackInProgress_) {
    return;
  }

  outBufPtr_ = buffer;
  out_buf_ptr = outBufPtr_;
  bufReady_ = true;
  updateCompatibilityGlobalsUnlocked();

  SampleCallback sample_cb = sampleCb_;
  BlockCallback block_cb = blockCb_;
  void *user = userPtr_;
  callbackInProgress_ = true;
  fillBufferWithCallback(buffer, sample_cb, block_cb, user);
  callbackInProgress_ = false;
  bufReady_ = false;
}

void DAClessAudio::service() {
  if (!lock()) {
    return;
  }

  if (dmaActive_ || !begun_ || muted_ || pwm_ == nullptr ||
      callbackInProgress_) {
    unlock();
    return;
  }

  const uint64_t now_q16 = hal_micros64() << 16u;
  while (now_q16 >= nextSampleDueQ16_) {
    writeCurrentSampleUnlocked();
    nextSampleDueQ16_ += samplePeriodQ16_;

    if (playIndex_ < cfg_.blockSize) {
      continue;
    }

    uint16_t *finished = playBuf_;
    playBuf_ = (playBuf_ == bufferA()) ? bufferB() : bufferA();
    playIndex_ = 0u;
    prepareFinishedBuffer(finished);

    SampleCallback sample_cb = sampleCb_;
    BlockCallback block_cb = blockCb_;
    void *user = userPtr_;
    callbackInProgress_ = true;
    unlock();

    fillBufferWithCallback(finished, sample_cb, block_cb, user);

    if (!lock()) {
      return;
    }
    callbackInProgress_ = false;
    bufReady_ = false;

    if (!begun_ || muted_) {
      break;
    }
  }

  unlock();
}

uint16_t interpolate(uint16_t x, uint16_t y, uint16_t mu_scaled) {
  return hal_dma_interpolate0(x, y, mu_scaled);
}

uint16_t interpolate1(uint16_t x, uint16_t y, uint16_t mu_scaled) {
  return hal_dma_interpolate1(x, y, mu_scaled);
}

#endif /* HAL_ENABLE_DACLESS */
#endif /* supported target */
