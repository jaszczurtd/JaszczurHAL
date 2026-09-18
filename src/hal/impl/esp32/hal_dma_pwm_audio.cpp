#include "hal/core/hal_compiler.h"
#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_DMA_PWM_AUDIO

#include "hal/analog/hal_adc.h"
#include "hal/audio/hal_dma_pwm_audio.h"
#include "hal/audio/hal_dma_pwm_audio_internal.h"
#include "hal/core/hal_assert.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "jh_esp32_gpio.h"
#include "jh_esp32_ledc.h"

#include <driver/gptimer.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <stddef.h>
#include <string.h>

/*
 * ESP32 has neither a DAC-grade sample DMA into LEDC nor a PWM compare
 * register a DMA channel can feed, so the sample pacing that RP and STM32
 * delegate to DMA runs here from a GPTimer alarm interrupt placed in IRAM.
 * The interrupt only pushes the next duty value; finished buffers are handed
 * to a service task that runs the application callback and refreshes the
 * optional ADC inputs, keeping the interrupt free of driver locks.
 */

#ifndef HAL_ESP32_AUDIO_SERVICE_STACK
/** @brief Service task stack in bytes; it runs the application callback. */
#define HAL_ESP32_AUDIO_SERVICE_STACK 4096u
#endif

#ifndef HAL_ESP32_AUDIO_SERVICE_PRIORITY
/** @brief Service task priority; buffers must be refilled before they play. */
#define HAL_ESP32_AUDIO_SERVICE_PRIORITY 18u
#endif

struct hal_dma_pwm_audio_impl_s {
  bool in_use;
  bool running;
  bool paused;
  bool streaming; /* Read by the alarm interrupt on every sample. */
  uint8_t pwm_pin;
  uint16_t block_size;
  uint16_t idle_value;
  uint32_t period_ticks;
  uint32_t sample_rate_hz;
  uint16_t *buffer_a;
  uint16_t *buffer_b;
  const uint8_t *adc_pins;
  uint8_t adc_count;
  volatile uint16_t *adc_buffer;
  hal_dma_pwm_audio_buffer_cb_t cb;
  void *user;
  jh_esp32_ledc_channel_t *ledc;
  gptimer_handle_t timer;
  TaskHandle_t service;
  bool service_stopped;
  uint16_t play_sample;
  uint8_t play_buffer;
};

namespace {

constexpr uint32_t kBufferDoneBit[2] = {UINT32_C(1) << 0, UINT32_C(1) << 1};
constexpr uint32_t kServiceStopBit = UINT32_C(1) << 31;
constexpr uint32_t kServiceStopTimeoutMs = 250u;

/* Highest alarm resolutions the APB-derived GPTimer expresses, most precise
 * first. A rate that divides one of them exactly keeps the pitch exact. */
constexpr uint32_t kAlarmResolutions[] = {
    UINT32_C(80000000), UINT32_C(40000000), UINT32_C(10000000),
    UINT32_C(1000000)};

hal_mutex_t s_mutex;
hal_dma_pwm_audio_impl_t s_pool[HAL_DMA_PWM_AUDIO_MAX_CHANNELS] = {};

hal_mutex_t pool_mutex(void) { return jh_hal_mutex_create_once(&s_mutex); }

bool audio_state_load(const bool *state) {
  return HAL_ATOMIC_LOAD(state, HAL_ATOMIC_ACQUIRE);
}

void audio_state_store(bool *state, bool value) {
  HAL_ATOMIC_STORE(state, value, HAL_ATOMIC_RELEASE);
}

bool IRAM_ATTR audio_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *,
                           void *user) {
  hal_dma_pwm_audio_impl_t *audio =
      static_cast<hal_dma_pwm_audio_impl_t *>(user);
  if (audio == nullptr ||
      !HAL_ATOMIC_LOAD(&audio->streaming, HAL_ATOMIC_ACQUIRE)) {
    return false;
  }

  const uint8_t index = audio->play_buffer;
  const uint16_t *buffer = index == 0u ? audio->buffer_a : audio->buffer_b;
  (void)jh_esp32_ledc_write_from_isr(audio->ledc, buffer[audio->play_sample]);

  BaseType_t woken = pdFALSE;
  ++audio->play_sample;
  if (audio->play_sample >= audio->block_size) {
    audio->play_sample = 0u;
    audio->play_buffer = (uint8_t)(1u - index);
    if (audio->service != nullptr) {
      (void)xTaskNotifyFromISR(audio->service, kBufferDoneBit[index], eSetBits,
                               &woken);
    }
  }
  return woken == pdTRUE;
}

void refresh_adc_inputs(hal_dma_pwm_audio_impl_t *audio) {
  for (uint8_t index = 0u; index < audio->adc_count; ++index) {
    const int value = hal_adc_read(audio->adc_pins[index]);
    if (value >= 0) {
      audio->adc_buffer[index] = (uint16_t)value;
    }
  }
}

void service_entry(void *user) {
  hal_dma_pwm_audio_impl_t *audio =
      static_cast<hal_dma_pwm_audio_impl_t *>(user);
  for (;;) {
    uint32_t bits = 0u;
    if (xTaskNotifyWait(0u, UINT32_MAX, &bits, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    if ((bits & kServiceStopBit) != 0u) {
      break;
    }
    if (!audio_state_load(&audio->running) ||
        audio_state_load(&audio->paused)) {
      continue;
    }
    refresh_adc_inputs(audio);
    if (audio->cb == nullptr) {
      continue;
    }
    if ((bits & kBufferDoneBit[0]) != 0u) {
      audio->cb(audio->user, audio->buffer_a, 0u);
    }
    if ((bits & kBufferDoneBit[1]) != 0u) {
      audio->cb(audio->user, audio->buffer_b, 1u);
    }
  }
  audio_state_store(&audio->service_stopped, true);
  vTaskDelete(nullptr);
}

/* Select the alarm resolution with the smallest rate error, preferring an
 * exact division. Returns false when no candidate yields at least two ticks. */
bool select_alarm_timing(uint32_t sample_rate_hz, uint32_t *out_resolution,
                         uint64_t *out_ticks) {
  bool found = false;
  uint32_t best_resolution = 0u;
  uint64_t best_ticks = 0u;
  for (size_t index = 0u; index < COUNTOF(kAlarmResolutions); ++index) {
    const uint32_t resolution = kAlarmResolutions[index];
    const uint64_t ticks =
        ((uint64_t)resolution + (sample_rate_hz / 2u)) / sample_rate_hz;
    if (ticks < 2u) {
      continue;
    }
    if (!found) {
      found = true;
      best_resolution = resolution;
      best_ticks = ticks;
    }
    if (resolution % sample_rate_hz == 0u) {
      best_resolution = resolution;
      best_ticks = ticks;
      break;
    }
  }
  if (!found) {
    return false;
  }
  *out_resolution = best_resolution;
  *out_ticks = best_ticks;
  return true;
}

hal_status_t create_alarm_timer(hal_dma_pwm_audio_impl_t *audio) {
  uint32_t resolution = 0u;
  uint64_t ticks = 0u;
  if (!select_alarm_timing(audio->sample_rate_hz, &resolution, &ticks)) {
    return HAL_EINVAL;
  }

  gptimer_config_t config = {};
  config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  config.direction = GPTIMER_COUNT_UP;
  config.resolution_hz = resolution;

  gptimer_handle_t timer = nullptr;
  esp_err_t result = gptimer_new_timer(&config, &timer);
  if (result == ESP_OK) {
    gptimer_event_callbacks_t callbacks = {};
    callbacks.on_alarm = audio_alarm;
    result = gptimer_register_event_callbacks(timer, &callbacks, audio);
  }
  if (result == ESP_OK) {
    result = gptimer_enable(timer);
  }
  if (result == ESP_OK) {
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = ticks;
    alarm.reload_count = 0u;
    alarm.flags.auto_reload_on_alarm = true;
    result = gptimer_set_alarm_action(timer, &alarm);
  }
  if (result != ESP_OK) {
    if (timer != nullptr) {
      (void)gptimer_disable(timer);
      (void)gptimer_del_timer(timer);
    }
    return HAL_EIO;
  }
  audio->timer = timer;
  return HAL_OK;
}

void destroy_alarm_timer(hal_dma_pwm_audio_impl_t *audio) {
  if (audio->timer == nullptr) {
    return;
  }
  (void)gptimer_stop(audio->timer);
  (void)gptimer_disable(audio->timer);
  (void)gptimer_del_timer(audio->timer);
  audio->timer = nullptr;
}

void destroy_service_task(hal_dma_pwm_audio_impl_t *audio) {
  TaskHandle_t service = audio->service;
  if (service == nullptr) {
    return;
  }
  audio_state_store(&audio->service_stopped, false);
  (void)xTaskNotify(service, kServiceStopBit, eSetBits);
  const uint32_t started = hal_millis();
  while (!audio_state_load(&audio->service_stopped) &&
         !hal_millis_deadline_expired(started, kServiceStopTimeoutMs)) {
    hal_delay_ms(1u);
  }
  const bool stopped = audio_state_load(&audio->service_stopped);
  if (!stopped) {
    /* A callback that never returns would otherwise keep reading the slot
     * this destroy is about to clear. Dropping the task bounds the damage,
     * and checked builds report the broken callback contract. */
    vTaskDelete(service);
  }
  audio->service = nullptr;
  HAL_ASSERT(stopped, "hal_dma_pwm_audio: buffer callback did not return");
}

void stop_output(hal_dma_pwm_audio_impl_t *audio, uint16_t idle_value) {
  audio_state_store(&audio->streaming, false);
  if (audio->timer != nullptr) {
    (void)gptimer_stop(audio->timer);
    (void)gptimer_set_raw_count(audio->timer, 0u);
  }
  (void)jh_esp32_ledc_write(audio->ledc, idle_value);
}

hal_status_t start_output(hal_dma_pwm_audio_impl_t *audio) {
  audio->play_sample = 0u;
  audio->play_buffer = 0u;
  audio_state_store(&audio->streaming, true);
  if (gptimer_set_raw_count(audio->timer, 0u) != ESP_OK ||
      gptimer_start(audio->timer) != ESP_OK) {
    audio_state_store(&audio->streaming, false);
    return HAL_EIO;
  }
  return HAL_OK;
}

void release_pool_slot(hal_dma_pwm_audio_impl_t *audio) {
  hal_mutex_t mutex = pool_mutex();
  if (mutex != nullptr) {
    hal_mutex_lock(mutex);
  }
  memset(audio, 0, sizeof(*audio));
  if (mutex != nullptr) {
    hal_mutex_unlock(mutex);
  }
}

} // namespace

hal_status_t hal_dma_pwm_audio_create_ex(const hal_dma_pwm_audio_config_t *cfg,
                                         hal_dma_pwm_audio_t *out_audio) {
  if (out_audio == nullptr) {
    return HAL_EINVAL;
  }
  *out_audio = nullptr;
  if (!jh_hal_dma_pwm_audio_config_is_valid(cfg)) {
    return HAL_EINVAL;
  }
  if (!jh_esp32_gpio_output_pin_valid(cfg->pwm_pin)) {
    return HAL_EINVAL;
  }
  /* LEDC needs at least one duty step below the period to express a sample. */
  if (cfg->period_ticks < 2u) {
    return HAL_EINVAL;
  }
  for (uint8_t index = 0u; index < cfg->adc_count; ++index) {
    if (cfg->adc_pins[index] == cfg->pwm_pin ||
        !hal_adc_is_pin_supported(cfg->adc_pins[index])) {
      return HAL_EINVAL;
    }
  }

  hal_mutex_t mutex = pool_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_dma_pwm_audio_impl_t *audio = nullptr;
  hal_mutex_lock(mutex);
  for (uint8_t index = 0u; index < HAL_DMA_PWM_AUDIO_MAX_CHANNELS; ++index) {
    if (!s_pool[index].in_use) {
      audio = &s_pool[index];
      memset(audio, 0, sizeof(*audio));
      audio->in_use = true;
      break;
    }
  }
  hal_mutex_unlock(mutex);
  if (audio == nullptr) {
    return HAL_ENOMEM;
  }

  audio->pwm_pin = cfg->pwm_pin;
  audio->sample_rate_hz = cfg->sample_rate_hz;
  audio->period_ticks = cfg->period_ticks;
  audio->buffer_a = cfg->buffer_a;
  audio->buffer_b = cfg->buffer_b;
  audio->block_size = cfg->block_size;
  audio->idle_value = cfg->idle_value;
  audio->adc_pins = cfg->adc_pins;
  audio->adc_count = cfg->adc_count;
  audio->adc_buffer = cfg->adc_buffer;
  audio->cb = cfg->buffer_done_cb;
  audio->user = cfg->user;

  /* The carrier runs at the sample rate, so one PWM period carries one
   * sample. The logical maximum matches the compare range the caller
   * declared; LEDC lowers its duty resolution when the rate needs it. */
  audio->ledc = jh_esp32_ledc_acquire(cfg->pwm_pin, cfg->sample_rate_hz,
                                      cfg->period_ticks - 1u);
  if (audio->ledc == nullptr) {
    release_pool_slot(audio);
    return HAL_EBUSY;
  }
  /* Configure the channel outside the interrupt so the alarm only updates
   * an already running output. */
  if (!jh_esp32_ledc_write(audio->ledc, cfg->idle_value)) {
    (void)jh_esp32_ledc_release(audio->ledc);
    release_pool_slot(audio);
    return HAL_EIO;
  }

  const hal_status_t timer_status = create_alarm_timer(audio);
  if (timer_status != HAL_OK) {
    (void)jh_esp32_ledc_release(audio->ledc);
    release_pool_slot(audio);
    return timer_status;
  }

  audio_state_store(&audio->service_stopped, false);
  if (xTaskCreatePinnedToCore(service_entry, "jh_audio",
                              HAL_ESP32_AUDIO_SERVICE_STACK, audio,
                              HAL_ESP32_AUDIO_SERVICE_PRIORITY, &audio->service,
                              (BaseType_t)xPortGetCoreID()) != pdPASS) {
    audio->service = nullptr;
    destroy_alarm_timer(audio);
    (void)jh_esp32_ledc_release(audio->ledc);
    release_pool_slot(audio);
    return HAL_ENOMEM;
  }

  *out_audio = audio;
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_start_ex(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (audio_state_load(&audio->running) || audio_state_load(&audio->paused)) {
    return HAL_ESTATE;
  }

  audio_state_store(&audio->running, true);
  audio_state_store(&audio->paused, false);
  const hal_status_t status = start_output(audio);
  if (status != HAL_OK) {
    audio_state_store(&audio->running, false);
  }
  return status;
}

bool hal_dma_pwm_audio_start(hal_dma_pwm_audio_t audio) {
  return hal_status_to_bool(hal_dma_pwm_audio_start_ex(audio));
}

hal_status_t hal_dma_pwm_audio_stop(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }

  audio_state_store(&audio->running, false);
  audio_state_store(&audio->paused, false);
  stop_output(audio, audio->idle_value);
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_pause(hal_dma_pwm_audio_t audio,
                                     uint16_t idle_value) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (idle_value >= audio->period_ticks) {
    return HAL_EINVAL;
  }
  if (!audio_state_load(&audio->running) || audio_state_load(&audio->paused)) {
    return HAL_ESTATE;
  }

  audio->idle_value = idle_value;
  audio_state_store(&audio->running, false);
  audio_state_store(&audio->paused, true);
  stop_output(audio, idle_value);
  return HAL_OK;
}

hal_status_t hal_dma_pwm_audio_resume(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }
  if (!audio_state_load(&audio->paused) || audio_state_load(&audio->running)) {
    return HAL_ESTATE;
  }

  audio_state_store(&audio->paused, false);
  audio_state_store(&audio->running, true);
  const hal_status_t status = start_output(audio);
  if (status != HAL_OK) {
    audio_state_store(&audio->running, false);
    audio_state_store(&audio->paused, true);
  }
  return status;
}

hal_status_t hal_dma_pwm_audio_destroy_ex(hal_dma_pwm_audio_t audio) {
  if (audio == nullptr || !audio->in_use) {
    return audio == nullptr ? HAL_EINVAL : HAL_ESTATE;
  }

  (void)hal_dma_pwm_audio_stop(audio);
  destroy_service_task(audio);
  destroy_alarm_timer(audio);
  (void)jh_esp32_ledc_release(audio->ledc);
  release_pool_slot(audio);
  return HAL_OK;
}

void hal_dma_pwm_audio_destroy(hal_dma_pwm_audio_t audio) {
  (void)hal_dma_pwm_audio_destroy_ex(audio);
}

bool hal_dma_pwm_audio_is_running(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use &&
         audio_state_load(&audio->running) && !audio_state_load(&audio->paused);
}

bool hal_dma_pwm_audio_is_paused(hal_dma_pwm_audio_t audio) {
  return audio != nullptr && audio->in_use && audio_state_load(&audio->paused);
}

#endif /* HAL_ENABLE_DMA_PWM_AUDIO */
#endif /* HAL_TARGET_IS_ESP32_FAMILY */
