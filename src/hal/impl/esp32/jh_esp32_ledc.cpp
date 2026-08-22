#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"
#include "jh_esp32_gpio.h"
#include "jh_esp32_ledc.h"

#include <driver/ledc.h>
#include <esp_clk_tree.h>
#include <soc/soc.h>
#include <soc/soc_caps.h>

#include <stddef.h>
#include <stdint.h>

static_assert(SOC_LEDC_TIMER_NUM > 0, "ESP32 LEDC requires a timer");
static_assert(SOC_LEDC_CHANNEL_NUM > 0, "ESP32 LEDC requires a channel");
static_assert(SOC_LEDC_CHANNEL_NUM <= UINT8_MAX,
              "LEDC reference count must fit in uint8_t");

struct jh_esp32_ledc_channel_s {
  bool in_use;
  bool configured;
  bool full_on;
  uint8_t pin;
  uint8_t timer;
  uint8_t channel;
  uint8_t duty_bits;
  uint32_t logical_max;
};

namespace {

struct timer_slot_t {
  bool in_use;
  uint8_t references;
  uint8_t duty_bits;
  uint32_t frequency_hz;
};

hal_mutex_t s_mutex;
timer_slot_t s_timers[SOC_LEDC_TIMER_NUM] = {};
jh_esp32_ledc_channel_t s_channels[SOC_LEDC_CHANNEL_NUM] = {};

hal_mutex_t ledc_mutex(void) { return jh_hal_mutex_create_once(&s_mutex); }

uint8_t bits_for_logical_max(uint32_t logical_max) {
  uint8_t bits = 1u;
  while (bits < 31u && ((UINT32_C(1) << bits) - 1u) < logical_max) {
    ++bits;
  }
  return bits;
}

uint32_t source_clock_hz(void) {
  uint32_t frequency = 0u;
  if (esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_APB,
                                   ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                   &frequency) != ESP_OK) {
    frequency = APB_CLK_FREQ;
  }
  return frequency;
}

int find_free_channel_locked(uint8_t pin) {
  for (size_t index = 0u; index < SOC_LEDC_CHANNEL_NUM; ++index) {
    if (s_channels[index].in_use && s_channels[index].pin == pin) {
      return -1;
    }
  }
  for (size_t index = 0u; index < SOC_LEDC_CHANNEL_NUM; ++index) {
    if (!s_channels[index].in_use) {
      return (int)index;
    }
  }
  return -1;
}

int find_timer_locked(uint32_t frequency_hz, uint8_t duty_bits) {
  for (size_t index = 0u; index < SOC_LEDC_TIMER_NUM; ++index) {
    const timer_slot_t &slot = s_timers[index];
    if (slot.in_use && slot.frequency_hz == frequency_hz &&
        slot.duty_bits == duty_bits) {
      return (int)index;
    }
  }
  for (size_t index = 0u; index < SOC_LEDC_TIMER_NUM; ++index) {
    if (!s_timers[index].in_use) {
      return (int)index;
    }
  }
  return -1;
}

bool valid_channel_locked(const jh_esp32_ledc_channel_t *channel) {
  const uintptr_t address = (uintptr_t)channel;
  return channel != nullptr && address >= (uintptr_t)&s_channels[0] &&
         address < (uintptr_t)&s_channels[SOC_LEDC_CHANNEL_NUM] &&
         channel->in_use;
}

uint32_t scale_duty(const jh_esp32_ledc_channel_t &channel,
                    uint32_t logical_value) {
  if (logical_value > channel.logical_max) {
    logical_value = channel.logical_max;
  }
  const uint32_t hardware_max =
      (UINT32_C(1) << channel.duty_bits) - UINT32_C(1);
  return (uint32_t)(((uint64_t)logical_value * hardware_max +
                     channel.logical_max / 2u) /
                    channel.logical_max);
}

} // namespace

jh_esp32_ledc_channel_t *jh_esp32_ledc_acquire(uint8_t pin,
                                               uint32_t frequency_hz,
                                               uint32_t logical_max) {
  if (!jh_esp32_gpio_output_pin_valid(pin) || frequency_hz == 0u ||
      logical_max == 0u) {
    return nullptr;
  }

  const uint32_t clock_hz = source_clock_hz();
  uint32_t maximum_bits =
      ledc_find_suitable_duty_resolution(clock_hz, frequency_hz);
  if (maximum_bits > SOC_LEDC_TIMER_BIT_WIDTH) {
    maximum_bits = SOC_LEDC_TIMER_BIT_WIDTH;
  }
  if (maximum_bits == 0u) {
    return nullptr;
  }
  uint8_t duty_bits = bits_for_logical_max(logical_max);
  if (duty_bits > maximum_bits) {
    duty_bits = (uint8_t)maximum_bits;
  }

  hal_mutex_t mutex = ledc_mutex();
  if (mutex == nullptr) {
    return nullptr;
  }
  hal_mutex_lock(mutex);
  const int channel_index = find_free_channel_locked(pin);
  const int timer_index = find_timer_locked(frequency_hz, duty_bits);
  if (channel_index < 0 || timer_index < 0) {
    hal_mutex_unlock(mutex);
    return nullptr;
  }

  timer_slot_t &timer = s_timers[timer_index];
  if (!timer.in_use) {
    ledc_timer_config_t config = {};
    config.speed_mode = LEDC_LOW_SPEED_MODE;
    config.duty_resolution = (ledc_timer_bit_t)duty_bits;
    config.timer_num = (ledc_timer_t)timer_index;
    config.freq_hz = frequency_hz;
    config.clk_cfg = LEDC_USE_APB_CLK;
    if (ledc_timer_config(&config) != ESP_OK) {
      hal_mutex_unlock(mutex);
      return nullptr;
    }
    timer.in_use = true;
    timer.references = 0u;
    timer.duty_bits = duty_bits;
    timer.frequency_hz = frequency_hz;
  }

  jh_esp32_ledc_channel_t &channel = s_channels[channel_index];
  channel.in_use = true;
  channel.configured = false;
  channel.full_on = false;
  channel.pin = pin;
  channel.timer = (uint8_t)timer_index;
  channel.channel = (uint8_t)channel_index;
  channel.duty_bits = duty_bits;
  channel.logical_max = logical_max;
  ++timer.references;
  hal_mutex_unlock(mutex);
  return &channel;
}

bool jh_esp32_ledc_write(jh_esp32_ledc_channel_t *channel,
                         uint32_t logical_value) {
  hal_mutex_t mutex = ledc_mutex();
  if (mutex == nullptr) {
    return false;
  }
  hal_mutex_lock(mutex);
  if (!valid_channel_locked(channel)) {
    hal_mutex_unlock(mutex);
    return false;
  }

  const bool full_on = logical_value >= channel->logical_max;
  const uint32_t duty = full_on ? 0u : scale_duty(*channel, logical_value);
  esp_err_t result = ESP_OK;
  bool newly_configured = false;
  if (!channel->configured) {
    ledc_channel_config_t config = {};
    config.gpio_num = channel->pin;
    config.speed_mode = LEDC_LOW_SPEED_MODE;
    config.channel = (ledc_channel_t)channel->channel;
    config.timer_sel = (ledc_timer_t)channel->timer;
    config.duty = duty;
    config.hpoint = 0;
    config.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
    result = ledc_channel_config(&config);
    channel->configured = result == ESP_OK;
    newly_configured = result == ESP_OK;
  }
  if (result == ESP_OK && full_on && !channel->full_on) {
    /* ESP32-S3 cannot reliably express 100% duty through LEDC when the timer
     * uses its maximum resolution. The driver's idle-high stop is the exact,
     * hardware-supported full-on state and a later duty update re-enables the
     * waveform output. */
    result =
        ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel->channel, 1u);
  } else if (result == ESP_OK && !full_on && !newly_configured) {
    result = ledc_set_duty_and_update(
        LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel->channel, duty, 0u);
  }
  if (result == ESP_OK) {
    channel->full_on = full_on;
  }
  hal_mutex_unlock(mutex);
  return result == ESP_OK;
}

void jh_esp32_ledc_stop(jh_esp32_ledc_channel_t *channel) {
  hal_mutex_t mutex = ledc_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (valid_channel_locked(channel) && channel->configured) {
    if (ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel->channel, 0u) ==
        ESP_OK) {
      channel->full_on = false;
    }
  }
  hal_mutex_unlock(mutex);
}

bool jh_esp32_ledc_release(jh_esp32_ledc_channel_t *channel) {
  hal_mutex_t mutex = ledc_mutex();
  if (mutex == nullptr) {
    return false;
  }
  hal_mutex_lock(mutex);
  if (!valid_channel_locked(channel)) {
    hal_mutex_unlock(mutex);
    return false;
  }

  if (channel->configured) {
    const esp_err_t stop_result =
        ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel->channel, 0u);
    if (stop_result != ESP_OK) {
      hal_mutex_unlock(mutex);
      return false;
    }
    channel->full_on = false;
    ledc_channel_config_t config = {};
    config.speed_mode = LEDC_LOW_SPEED_MODE;
    config.channel = (ledc_channel_t)channel->channel;
    config.deconfigure = true;
    if (ledc_channel_config(&config) != ESP_OK) {
      /* The stopped channel remains owned and can be retried or restarted. */
      hal_mutex_unlock(mutex);
      return false;
    }
  }
  timer_slot_t &timer = s_timers[channel->timer];
  if (timer.references > 0u) {
    --timer.references;
  }
  if (timer.references == 0u) {
    timer = {};
  }
  *channel = {};
  hal_mutex_unlock(mutex);
  return true;
}

uint32_t jh_esp32_ledc_source_clock_hz(void) { return source_clock_hz(); }

#endif // HAL_TARGET_IS_ESP32_FAMILY
