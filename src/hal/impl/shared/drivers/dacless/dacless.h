#pragma once

/*
 * DACless PWM-audio driver for JaszczurHAL.
 *
 * This shared implementation is modeled after Brian Varren's DACless library
 * by Brian Sullivan. It preserves the public engine/configuration model,
 * double-buffered block flow, sample and block callbacks, ADC result buffer,
 * compatibility globals and RP2040 interpolator helper semantics while
 * routing timing, PWM, ADC and synchronization through JaszczurHAL.
 *
 * Original project: https://github.com/brianvarren/DACless
 * Original license: MIT, Copyright (c) 2025 brian sullivan.
 */

#include "hal/hal_config.h"
#include "hal/hal_target.h"

#if (HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK) &&     \
    defined(HAL_ENABLE_DACLESS)

#include "hal/hal_dma_pwm_audio.h"
#include "hal/hal_pwm_freq.h"
#include "hal/hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef DACLESS_MAX_BLOCK_SIZE
#define DACLESS_MAX_BLOCK_SIZE 512u
#endif

#ifndef DACLESS_MAX_ADC_INPUTS
#define DACLESS_MAX_ADC_INPUTS 4u
#endif

#ifndef DACLESS_MAX_INSTANCES
#define DACLESS_MAX_INSTANCES 4u
#endif

#ifndef DACLESS_MAX_POLLING_CATCHUP_SAMPLES
#define DACLESS_MAX_POLLING_CATCHUP_SAMPLES 64u
#endif

#ifndef DACLESS_DEFAULT_PWM_PIN
#define DACLESS_DEFAULT_PWM_PIN 6u
#endif

#if HAL_TARGET_IS_STM32G474
#define DACLESS_DEFAULT_ADC0_PIN 0u
#define DACLESS_DEFAULT_ADC1_PIN 1u
#define DACLESS_DEFAULT_ADC2_PIN 2u
#define DACLESS_DEFAULT_ADC3_PIN 3u
#else
#define DACLESS_DEFAULT_ADC0_PIN 26u
#define DACLESS_DEFAULT_ADC1_PIN 27u
#define DACLESS_DEFAULT_ADC2_PIN 28u
#define DACLESS_DEFAULT_ADC3_PIN 29u
#endif

struct DAClessConfig {
  uint8_t pinPWM = DACLESS_DEFAULT_PWM_PIN;
  uint16_t pwmBits = 12u;
  uint16_t blockSize = 128u;
  uint8_t nAdcInputs = DACLESS_MAX_ADC_INPUTS;
  bool useDma = true;
  uint8_t adcPins[DACLESS_MAX_ADC_INPUTS] = {
      DACLESS_DEFAULT_ADC0_PIN, DACLESS_DEFAULT_ADC1_PIN,
      DACLESS_DEFAULT_ADC2_PIN, DACLESS_DEFAULT_ADC3_PIN};
};

class DAClessAudio {
public:
  using SampleCallback = uint16_t (*)(void *);
  using BlockCallback = void (*)(void *, uint16_t *);

  explicit DAClessAudio(const DAClessConfig &cfg = DAClessConfig());
  ~DAClessAudio();

  DAClessAudio(const DAClessAudio &) = delete;
  DAClessAudio &operator=(const DAClessAudio &) = delete;

  bool begin();
  void service();
  void mute();
  void unmute();

  void setSampleCallback(SampleCallback cb, void *userdata = nullptr);
  void setBlockCallback(BlockCallback cb, void *userdata = nullptr);

  void setAudioSampleCallback(SampleCallback cb, void *userdata = nullptr) {
    setSampleCallback(cb, userdata);
  }

  uint16_t getADC(uint8_t channel) const;
  float getSampleRate() const { return sampleRate_; }
  const DAClessConfig &getConfig() const { return cfg_; }
  const volatile uint16_t *getOutBufPtr() const { return outBufPtr_; }
  const volatile uint16_t *getAdcBuffer() const { return adcBuf_; }
  bool isMuted() const { return muted_; }
  bool isRunning() const { return begun_ && !muted_; }
  bool isDmaActive() const { return dmaActive_; }

#if HAL_TARGET_IS_MOCK
  hal_pwm_freq_channel_t getPwmChannelForTest() const { return pwm_; }
  hal_dma_pwm_audio_t getDmaAudioForTest() const { return dma_; }
#endif

private:
  struct CallbackSnapshot {
    SampleCallback sample_cb;
    BlockCallback block_cb;
    void *user;
  };

  bool ensureMutex() const;
  bool lock() const;
  void unlock() const;

  static DAClessConfig normalizeConfig(const DAClessConfig &cfg);
  static uint32_t sourceClockHz(uint8_t pin);
  static uint16_t maxSampleValueForBits(uint16_t bits);
  static uint16_t midpointForBits(uint16_t bits);
  static uint32_t periodTicksForBits(uint16_t bits);
  static uint32_t sampleRateIntForBits(uint16_t bits, uint8_t pin);
  static uint64_t samplePeriodQ16ForRate(uint32_t sample_rate_hz);
  static uint64_t clampPollingCatchupQ16(uint64_t now_q16,
                                         uint64_t next_due_q16,
                                         uint64_t sample_period_q16);

  void registerInstance();
  void unregisterInstance();
  void updateCompatibilityGlobalsUnlocked();
  bool isCompatibilityOwnerUnlocked() const;

  void fillSilenceUnlocked();
  void markBeginFailedUnlocked();
  void sampleAdcUnlocked();
  void prepareFinishedBuffer(uint16_t *buffer);
  bool claimDmaBufferUnlocked(uint16_t *buffer, CallbackSnapshot &snapshot);
  void finishCallbackUnlocked();
  CallbackSnapshot callbackSnapshotUnlocked() const;
  void writeCurrentSampleUnlocked();
  void fillBufferWithCallback(uint16_t *buffer, SampleCallback sample_cb,
                              BlockCallback block_cb, void *user);
  bool startDmaUnlocked();
  void stopDmaUnlocked();
  static void dmaBufferDoneThunk(void *user, uint16_t *buffer,
                                 uint8_t buffer_index);
  void onDmaBufferDone(uint16_t *buffer, uint8_t buffer_index);
  uint16_t *bufferA() { return pwmBuf_; }
  uint16_t *bufferB() { return pwmBuf_ + cfg_.blockSize; }
  const uint16_t *bufferA() const { return pwmBuf_; }
  const uint16_t *bufferB() const { return pwmBuf_ + cfg_.blockSize; }

  mutable hal_mutex_t mutex_ = nullptr;
  DAClessConfig cfg_;

  hal_pwm_freq_channel_t pwm_ = nullptr;
  hal_dma_pwm_audio_t dma_ = nullptr;
  float sampleRate_ = 0.0f;
  uint32_t sampleRateInt_ = 0u;
  uint64_t samplePeriodQ16_ = 0u;
  uint64_t nextSampleDueQ16_ = 0u;

  uint16_t adcBuf_[DACLESS_MAX_ADC_INPUTS] = {};
  uint16_t pwmBuf_[DACLESS_MAX_BLOCK_SIZE * 2u] = {};
  uint16_t *playBuf_ = nullptr;
  uint16_t playIndex_ = 0u;

  volatile uint16_t *outBufPtr_ = nullptr;
  volatile bool bufReady_ = false;
  bool begun_ = false;
  bool muted_ = false;
  bool dmaActive_ = false;
  bool callbackInProgress_ = false;
  int8_t registryIndex_ = -1;

  SampleCallback sampleCb_ = nullptr;
  BlockCallback blockCb_ = nullptr;
  void *userPtr_ = nullptr;
};

uint16_t interpolate(uint16_t x, uint16_t y, uint16_t mu_scaled);

extern float audio_rate;
extern volatile uint16_t *out_buf_ptr;
extern const volatile uint16_t *adc_results_buf;

#endif /* supported target && HAL_ENABLE_DACLESS */
