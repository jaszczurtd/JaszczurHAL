#include "hal/core/hal_compiler.h"
#include "hal/core/hal_config.h"
#if HAL_TARGET_IS_ESP32_S3 && defined(HAL_ENABLE_PULSE_CAPTURE)
#include "hal/analog/jh_pulse_capture_backend.h"
#include "hal/core/hal_array.h"
#include "hal/system/hal_system.h"
#include "jh_esp32_gpio.h"
#include "jh_esp32_status.h"
#include <driver/mcpwm_cap.h>
#include <esp_attr.h>

#if !CONFIG_MCPWM_ISR_CACHE_SAFE
#error "HAL pulse capture requires CONFIG_MCPWM_ISR_CACHE_SAFE=y"
#endif

namespace {
mcpwm_cap_timer_handle_t timer;
mcpwm_cap_channel_handle_t input, reference;
bool timer_enabled, timer_started, input_enabled, reference_enabled;
DRAM_ATTR uint32_t ring[128];
DRAM_ATTR uint32_t produced, consumed;
DRAM_ATTR bool overflow;
uint32_t frequency, last_poll_us;

bool IRAM_ATTR captured(mcpwm_cap_channel_handle_t,
                        const mcpwm_capture_event_data_t *event, void *) {
  const uint32_t write = HAL_ATOMIC_LOAD(&produced, HAL_ATOMIC_RELAXED);
  if (write - HAL_ATOMIC_LOAD(&consumed, HAL_ATOMIC_ACQUIRE) >= COUNTOF(ring)) {
    HAL_ATOMIC_STORE(&overflow, true, HAL_ATOMIC_RELEASE);
    return false;
  }
  ring[write % COUNTOF(ring)] = event->cap_value;
  HAL_ATOMIC_STORE(&produced, write + 1U, HAL_ATOMIC_RELEASE);
  return false;
}

esp_err_t setup(const hal_pulse_capture_config_t *config) {
  mcpwm_capture_timer_config_t tc = {};
  tc.clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT;
  esp_err_t error = ESP_ERR_NOT_FOUND;
  for (tc.group_id = 0; tc.group_id < 2; ++tc.group_id) {
    error = mcpwm_new_capture_timer(&tc, &timer);
    if (error != ESP_ERR_NOT_FOUND)
      break;
  }
  if (error != ESP_OK)
    return error;
  mcpwm_capture_channel_config_t cc = {};
  cc.gpio_num = config->pin;
  cc.prescale = 32;
  cc.intr_priority = 3;
  cc.flags.pos_edge = true;
  cc.flags.invert_cap_signal = config->falling;
  error = mcpwm_new_capture_channel(timer, &cc, &input);
  if (error != ESP_OK)
    return error;
  /* A separate software-only channel samples the same timer for sample age.
   * No callback is registered on this channel. */
  cc.gpio_num = -1;
  cc.prescale = 1;
  cc.flags.invert_cap_signal = false;
  error = mcpwm_new_capture_channel(timer, &cc, &reference);
  if (error != ESP_OK)
    return error;
  mcpwm_capture_event_callbacks_t callbacks = {};
  callbacks.on_cap = captured;
  error = mcpwm_capture_channel_register_event_callbacks(input, &callbacks,
                                                         nullptr);
  if (error != ESP_OK)
    return error;
  error = mcpwm_capture_timer_get_resolution(timer, &frequency);
  if (error != ESP_OK)
    return error;
  error = mcpwm_capture_timer_enable(timer);
  if (error != ESP_OK)
    return error;
  timer_enabled = true;
  error = mcpwm_capture_channel_enable(reference);
  if (error != ESP_OK)
    return error;
  reference_enabled = true;
  error = mcpwm_capture_channel_enable(input);
  if (error != ESP_OK)
    return error;
  input_enabled = true;
  error = mcpwm_capture_timer_start(timer);
  if (error == ESP_OK)
    timer_started = true;
  return error;
}

esp_err_t release(void) {
  esp_err_t error;
  if (input_enabled) {
    error = mcpwm_capture_channel_disable(input);
    if (error != ESP_OK)
      return error;
    input_enabled = false;
  }
  if (reference_enabled) {
    error = mcpwm_capture_channel_disable(reference);
    if (error != ESP_OK)
      return error;
    reference_enabled = false;
  }
  if (timer_started) {
    error = mcpwm_capture_timer_stop(timer);
    if (error != ESP_OK)
      return error;
    timer_started = false;
  }
  if (timer_enabled) {
    error = mcpwm_capture_timer_disable(timer);
    if (error != ESP_OK)
      return error;
    timer_enabled = false;
  }
  mcpwm_cap_channel_handle_t *channels[] = {&input, &reference};
  for (auto channel : channels) {
    if (*channel == nullptr)
      continue;
    error = mcpwm_del_capture_channel(*channel);
    if (error != ESP_OK)
      return error;
    *channel = nullptr;
  }
  if (timer != nullptr) {
    error = mcpwm_del_capture_timer(timer);
    if (error != ESP_OK)
      return error;
    timer = nullptr;
  }
  return ESP_OK;
}
} // namespace

hal_status_t jh_pulse_capture_start(const hal_pulse_capture_config_t *config,
                                    uint32_t *clock_hz, uint16_t *stride) {
  if (!jh_esp32_gpio_pin_valid(config->pin))
    return HAL_EINVAL;
  /* A failed setup may leave resources if SDK cleanup also failed.
   * Retry that cleanup before allocating another timer. */
  if (timer != nullptr) {
    const esp_err_t cleanup = release();
    if (cleanup != ESP_OK)
      return jh_esp32_status_from_esp_err(cleanup);
  }
  produced = consumed = 0;
  overflow = false;
  const esp_err_t error = setup(config);
  if (error != ESP_OK) {
    const esp_err_t cleanup = release();
    if (cleanup != ESP_OK)
      return jh_esp32_status_from_esp_err(cleanup);
    return error == ESP_ERR_NOT_FOUND ? HAL_EBUSY
                                      : jh_esp32_status_from_esp_err(error);
  }
  last_poll_us = hal_micros();
  *clock_hz = frequency;
  *stride = 32;
  return HAL_OK;
}

hal_status_t jh_pulse_capture_stop(void) {
  return jh_esp32_status_from_esp_err(release());
}

hal_status_t jh_pulse_capture_next(jh_pulse_capture_edge_t *edge) {
  const uint32_t polled_us = hal_micros();
  if (hal_elapsed_u32(polled_us, last_poll_us, 10000U))
    return HAL_EOVERFLOW;
  last_poll_us = polled_us;
  if (HAL_ATOMIC_LOAD(&overflow, HAL_ATOMIC_ACQUIRE))
    return HAL_EOVERFLOW;
  const uint32_t read = HAL_ATOMIC_LOAD(&consumed, HAL_ATOMIC_RELAXED);
  if (read == HAL_ATOMIC_LOAD(&produced, HAL_ATOMIC_ACQUIRE))
    return HAL_EAGAIN;
  const uint32_t ticks = ring[read % COUNTOF(ring)];
  uint32_t current;
  const uint32_t wall_us = hal_micros();
  esp_err_t error = mcpwm_capture_channel_trigger_soft_catch(reference);
  if (error == ESP_OK)
    error = mcpwm_capture_get_latched_value(reference, &current);
  if (error != ESP_OK)
    return jh_esp32_status_from_esp_err(error);
  HAL_ATOMIC_STORE(&consumed, read + 1U, HAL_ATOMIC_RELEASE);
  const uint32_t age_us =
      (uint32_t)(((uint64_t)(current - ticks) * 1000000U) / frequency);
  *edge = {ticks, wall_us - age_us - 1U};
  return HAL_OK;
}
#endif
