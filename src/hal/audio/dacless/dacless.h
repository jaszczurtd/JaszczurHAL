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

#include "hal/audio/hal_dacless.h"
#include "hal/core/hal_compiler.h"
#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"

#if defined(HAL_ENABLE_DACLESS)

#include "hal/audio/hal_dma_pwm_audio.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/system/hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

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

  /**
   * @brief Start or restart audio while preserving the detailed HAL status.
   * @return HAL_OK on success, HAL_EINVAL for invalid DMA pin routing,
   *         HAL_EUNSUPPORTED when the DMA backend is not available, HAL_EBUSY
   *         when a hardware resource is already used, HAL_ENOMEM when
   *         synchronization or a backend slot is unavailable, or HAL_EIO
   *         when the output backend cannot be started.
   * @note Calls are serialized by the instance. On RP targets, all lifecycle
   *       and control calls for DMA instances must use one owner core.
   */
  hal_status_t beginEx();

  /**
   * @brief Stop output and release backend resources before destruction.
   * @return HAL_OK on success, HAL_ENOMEM when synchronization is
   *         unavailable, or an error returned by the DMA backend.
   * @note On RP targets, call this on the same core as @ref beginEx.
   */
  hal_status_t shutdownEx();
  void service();
  void mute();

  /**
   * @brief Pause output while preserving the detailed HAL status.
   * @return HAL_OK on success, HAL_ENOMEM when synchronization cannot be
   *         initialized, HAL_ESTATE for an invalid backend state, or HAL_EIO
   *         when the DMA backend cannot be paused.
   */
  hal_status_t muteEx();
  void unmute();

  /**
   * @brief Resume output while preserving the detailed HAL status.
   * @return HAL_OK on success, HAL_ENOMEM when synchronization cannot be
   *         initialized, HAL_EBUSY when ADC/PWM resources are in use,
   *         HAL_ESTATE for an invalid backend state, or HAL_EIO when the DMA
   *         backend cannot be resumed.
   */
  hal_status_t unmuteEx();

  void setSampleCallback(SampleCallback cb, void *userdata = nullptr);

  /**
   * @brief Install the sample callback while preserving the HAL status.
   * @param cb Callback to install, or NULL to clear it.
   * @param userdata Value passed to @p cb.
   * @return HAL_OK on success or HAL_ENOMEM when synchronization cannot be
   *         initialized.
   */
  hal_status_t setSampleCallbackEx(SampleCallback cb, void *userdata = nullptr);
  void setBlockCallback(BlockCallback cb, void *userdata = nullptr);

  /**
   * @brief Install the block callback while preserving the HAL status.
   * @param cb Callback to install, or NULL to clear it.
   * @param userdata Value passed to @p cb.
   * @return HAL_OK on success or HAL_ENOMEM when synchronization cannot be
   *         initialized.
   */
  hal_status_t setBlockCallbackEx(BlockCallback cb, void *userdata = nullptr);

  void setAudioSampleCallback(SampleCallback cb, void *userdata = nullptr) {
    setSampleCallback(cb, userdata);
  }

  uint16_t getADC(uint8_t channel) const;
  float getSampleRate() const { return sampleRate_; }
  const DAClessConfig &getConfig() const { return cfg_; }
  const volatile uint16_t *getOutBufPtr() const {
    return HAL_ATOMIC_LOAD(&outBufPtr_, HAL_ATOMIC_ACQUIRE);
  }
  const volatile uint16_t *getAdcBuffer() const { return adcBuf_; }

  /**
   * @brief Report whether the most recent start succeeded.
   * @return true after a successful begin; false before begin or after a
   *         failed restart.
   * @note Do not race this compatibility accessor with lifecycle operations.
   */
  bool isBegun() const { return begun_; }
  bool isMuted() const {
    return muted_ ||
           (dmaActive_ && dma_ != nullptr && hal_dma_pwm_audio_is_paused(dma_));
  }
  bool isRunning() const {
    return begun_ && !isMuted() &&
           (!dmaActive_ ||
            (dma_ != nullptr && hal_dma_pwm_audio_is_running(dma_)));
  }
  bool isDmaActive() const { return dmaActive_; }

  /**
   * @brief Report whether this object has a compatibility-registry slot.
   * @return true when this instance occupies the shared compatibility
   *         registry; false when that registry was already full at creation.
   * @note The result is fixed after construction. Do not call it concurrently
   *       with destruction.
   */
  bool isRegistered() const { return registryIndex_ >= 0; }

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
  void publishOutputBuffer(volatile uint16_t *buffer);

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
  hal_status_t startDmaUnlocked();
  hal_status_t stopDmaUnlocked();
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

#endif /* HAL_ENABLE_DACLESS */
