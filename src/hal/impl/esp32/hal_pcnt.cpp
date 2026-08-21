#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_PCNT

#include "hal/analog/hal_pcnt.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"
#include "jh_esp32_gpio.h"
#include "jh_esp32_status.h"

#include <driver/pulse_cnt.h>

#include <limits.h>
#include <stdint.h>

namespace {

constexpr uint8_t kChannelCount = 4u;

struct counter_slot_t {
  pcnt_unit_handle_t unit;
  pcnt_channel_handle_t channel;
  uint32_t epoch;
  bool enabled;
  bool started;
};

hal_mutex_t s_mutex;
counter_slot_t s_counters[kChannelCount] = {};

hal_mutex_t counter_mutex(void) { return jh_hal_mutex_create_once(&s_mutex); }

bool edge_valid(hal_pcnt_edge_t edge) {
  return edge == HAL_PCNT_EDGE_RISING || edge == HAL_PCNT_EDGE_FALLING ||
         edge == HAL_PCNT_EDGE_BOTH;
}

esp_err_t release_counter(counter_slot_t &counter) {
  if (counter.unit != nullptr && counter.started) {
    const esp_err_t result = pcnt_unit_stop(counter.unit);
    if (result != ESP_OK) {
      return result;
    }
    counter.started = false;
  }
  if (counter.unit != nullptr && counter.enabled) {
    const esp_err_t result = pcnt_unit_disable(counter.unit);
    if (result != ESP_OK) {
      return result;
    }
    counter.enabled = false;
  }
  if (counter.channel != nullptr) {
    const esp_err_t result = pcnt_del_channel(counter.channel);
    if (result != ESP_OK) {
      return result;
    }
    counter.channel = nullptr;
  }
  if (counter.unit != nullptr) {
    const esp_err_t result = pcnt_del_unit(counter.unit);
    if (result != ESP_OK) {
      return result;
    }
    counter.unit = nullptr;
  }
  counter = {};
  return ESP_OK;
}

hal_status_t read_total(const counter_slot_t &counter, uint32_t *out_total) {
  int count = 0;
  const esp_err_t result = pcnt_unit_get_count(counter.unit, &count);
  if (result == ESP_OK) {
    *out_total = (uint32_t)count;
  }
  return jh_esp32_status_from_esp_err(result);
}

hal_status_t sample_counter(uint8_t channel, uint32_t *out_count,
                            bool reset_epoch) {
  if (out_count != nullptr) {
    *out_count = 0u;
  }
  hal_mutex_t mutex = counter_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  counter_slot_t &counter = s_counters[channel];
  if (counter.unit == nullptr || !counter.started) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  uint32_t total = 0u;
  const hal_status_t status = read_total(counter, &total);
  if (status == HAL_OK) {
    if (out_count != nullptr) {
      *out_count = total - counter.epoch;
    }
    if (reset_epoch) {
      counter.epoch = total;
    }
  }
  hal_mutex_unlock(mutex);
  return status;
}

} // namespace

bool hal_pcnt_is_supported(void) { return true; }

uint8_t hal_pcnt_channel_count(void) { return kChannelCount; }

hal_status_t hal_pcnt_init_ex(uint8_t logical_channel, uint8_t pin,
                              hal_pcnt_edge_t edge) {
  if (logical_channel >= kChannelCount || !jh_esp32_gpio_pin_valid(pin) ||
      !edge_valid(edge)) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = counter_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  counter_slot_t &counter = s_counters[logical_channel];
  esp_err_t result = release_counter(counter);
  if (result != ESP_OK) {
    hal_mutex_unlock(mutex);
    return jh_esp32_status_from_esp_err(result);
  }

  pcnt_unit_config_t unit_config = {};
  unit_config.low_limit = SHRT_MIN;
  unit_config.high_limit = SHRT_MAX;
  unit_config.flags.accum_count = 1u;
  result = pcnt_new_unit(&unit_config, &counter.unit);

  pcnt_chan_config_t channel_config = {};
  channel_config.edge_gpio_num = pin;
  channel_config.level_gpio_num = -1;
  channel_config.flags.virt_level_io_level = 1u;
  if (result == ESP_OK) {
    result = pcnt_new_channel(counter.unit, &channel_config, &counter.channel);
  }

  pcnt_channel_edge_action_t positive = PCNT_CHANNEL_EDGE_ACTION_HOLD;
  pcnt_channel_edge_action_t negative = PCNT_CHANNEL_EDGE_ACTION_HOLD;
  if (edge == HAL_PCNT_EDGE_RISING || edge == HAL_PCNT_EDGE_BOTH) {
    positive = PCNT_CHANNEL_EDGE_ACTION_INCREASE;
  }
  if (edge == HAL_PCNT_EDGE_FALLING || edge == HAL_PCNT_EDGE_BOTH) {
    negative = PCNT_CHANNEL_EDGE_ACTION_INCREASE;
  }
  if (result == ESP_OK) {
    result = pcnt_channel_set_edge_action(counter.channel, positive, negative);
  }
  if (result == ESP_OK) {
    result = pcnt_channel_set_level_action(counter.channel,
                                           PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                           PCNT_CHANNEL_LEVEL_ACTION_KEEP);
  }
  if (result == ESP_OK) {
    result = pcnt_unit_add_watch_point(counter.unit, SHRT_MIN);
  }
  if (result == ESP_OK) {
    result = pcnt_unit_add_watch_point(counter.unit, SHRT_MAX);
  }
  if (result == ESP_OK) {
    result = pcnt_unit_enable(counter.unit);
    counter.enabled = result == ESP_OK;
  }
  if (result == ESP_OK) {
    result = pcnt_unit_clear_count(counter.unit);
  }
  if (result == ESP_OK) {
    result = pcnt_unit_start(counter.unit);
    counter.started = result == ESP_OK;
  }
  if (result != ESP_OK) {
    const esp_err_t cleanup_result = release_counter(counter);
    if (cleanup_result != ESP_OK) {
      result = cleanup_result;
    }
  }
  hal_mutex_unlock(mutex);
  return jh_esp32_status_from_esp_err(result);
}

bool hal_pcnt_init(uint8_t channel, uint8_t pin, hal_pcnt_edge_t edge) {
  return hal_status_to_bool(hal_pcnt_init_ex(channel, pin, edge));
}

hal_status_t hal_pcnt_read_ex(uint8_t channel, uint32_t *out_count) {
  if (channel >= kChannelCount || out_count == nullptr) {
    return HAL_EINVAL;
  }
  return sample_counter(channel, out_count, false);
}

uint32_t hal_pcnt_read(uint8_t channel) {
  uint32_t count = 0u;
  (void)hal_pcnt_read_ex(channel, &count);
  return count;
}

hal_status_t hal_pcnt_reset(uint8_t channel) {
  if (channel >= kChannelCount) {
    return HAL_EINVAL;
  }
  return sample_counter(channel, nullptr, true);
}

hal_status_t hal_pcnt_read_and_reset_ex(uint8_t channel, uint32_t *out_count) {
  if (channel >= kChannelCount || out_count == nullptr) {
    return HAL_EINVAL;
  }
  return sample_counter(channel, out_count, true);
}

uint32_t hal_pcnt_read_and_reset(uint8_t channel) {
  uint32_t count = 0u;
  (void)hal_pcnt_read_and_reset_ex(channel, &count);
  return count;
}

#endif // HAL_ENABLE_PCNT
#endif // HAL_TARGET_IS_ESP32_FAMILY
