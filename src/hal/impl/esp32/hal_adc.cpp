#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_S3

#include "hal/core/hal_config.h"

#include "hal/analog/hal_adc.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"

#include <esp_adc/adc_oneshot.h>

#include <stddef.h>
#include <stdint.h>

namespace {

constexpr uint8_t kEsp32AdcHardwareBits = SOC_ADC_RTC_MAX_BITWIDTH;
constexpr uint8_t kDefaultResolutionBits = 12u;
constexpr uint8_t kMinimumResolutionBits = 1u;
constexpr uint8_t kMaximumResolutionBits = 16u;

hal_mutex_t s_adc_mutex = nullptr;
adc_oneshot_unit_handle_t s_units[SOC_ADC_PERIPH_NUM] = {};
uint32_t s_configured_channels[SOC_ADC_PERIPH_NUM] = {};
uint8_t s_resolution_bits = kDefaultResolutionBits;

bool pin_in_mask(uint8_t pin, uint64_t mask) {
  return pin < 64u && (mask & (UINT64_C(1) << pin)) != 0u;
}

bool adc_pin_available(uint8_t pin) {
  const bool board_accessible =
      pin_in_mask(pin, HAL_BOARD_GPIO_EXPOSED_MASK) ||
      pin_in_mask(pin, HAL_BOARD_GPIO_SOFT_RESERVED_MASK);
  return pin_in_mask(pin, HAL_TARGET_GPIO_VALID_MASK) &&
         pin_in_mask(pin, HAL_TARGET_GPIO_ADC_MASK) && board_accessible &&
         !pin_in_mask(pin, HAL_BOARD_GPIO_HARD_RESERVED_MASK);
}

uint8_t clamp_resolution(uint8_t bits) {
  if (bits < kMinimumResolutionBits) {
    return kMinimumResolutionBits;
  }
  if (bits > kMaximumResolutionBits) {
    return kMaximumResolutionBits;
  }
  return bits;
}

hal_mutex_t adc_mutex() { return jh_hal_mutex_create_once(&s_adc_mutex); }

bool adc_ensure_unit_locked(adc_unit_t unit) {
  const size_t index = static_cast<size_t>(unit);
  if (index >= SOC_ADC_PERIPH_NUM) {
    return false;
  }
  if (s_units[index] != nullptr) {
    return true;
  }

  adc_oneshot_unit_init_cfg_t config = {};
  config.unit_id = unit;
  config.clk_src = ADC_RTC_CLK_SRC_DEFAULT;
  config.ulp_mode = ADC_ULP_MODE_DISABLE;
  return adc_oneshot_new_unit(&config, &s_units[index]) == ESP_OK;
}

bool adc_ensure_channel_locked(adc_unit_t unit, adc_channel_t channel) {
  const size_t unit_index = static_cast<size_t>(unit);
  const uint32_t channel_bit = UINT32_C(1) << static_cast<uint32_t>(channel);
  if ((s_configured_channels[unit_index] & channel_bit) != 0u) {
    return true;
  }

  adc_oneshot_chan_cfg_t config = {};
  config.atten = ADC_ATTEN_DB_12;
  config.bitwidth = ADC_BITWIDTH_DEFAULT;
  if (adc_oneshot_config_channel(s_units[unit_index], channel, &config) !=
      ESP_OK) {
    return false;
  }
  s_configured_channels[unit_index] |= channel_bit;
  return true;
}

int scale_adc_result(int raw, uint8_t output_bits) {
  if (raw <= 0) {
    return 0;
  }
  const uint32_t input_max =
      (UINT32_C(1) << kEsp32AdcHardwareBits) - UINT32_C(1);
  const uint32_t output_max = (UINT32_C(1) << output_bits) - UINT32_C(1);
  const uint32_t bounded_raw = static_cast<uint32_t>(raw) < input_max
                                   ? static_cast<uint32_t>(raw)
                                   : input_max;
  return static_cast<int>((bounded_raw * output_max + input_max / 2u) /
                          input_max);
}

} // namespace

void hal_adc_set_resolution(uint8_t bits) {
  hal_mutex_t mutex = adc_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  s_resolution_bits = clamp_resolution(bits);
  hal_mutex_unlock(mutex);
}

int hal_adc_read(uint8_t pin) {
  if (!adc_pin_available(pin)) {
    return 0;
  }

  adc_unit_t unit = ADC_UNIT_1;
  adc_channel_t channel = ADC_CHANNEL_0;
  if (adc_oneshot_io_to_channel((int)pin, &unit, &channel) != ESP_OK) {
    return 0;
  }

  hal_mutex_t mutex = adc_mutex();
  if (mutex == nullptr) {
    return 0;
  }
  hal_mutex_lock(mutex);
  if (!adc_ensure_unit_locked(unit) ||
      !adc_ensure_channel_locked(unit, channel)) {
    hal_mutex_unlock(mutex);
    return 0;
  }

  int raw = 0;
  const esp_err_t error =
      adc_oneshot_read(s_units[static_cast<size_t>(unit)], channel, &raw);
  const int result =
      error == ESP_OK ? scale_adc_result(raw, s_resolution_bits) : 0;
  hal_mutex_unlock(mutex);
  return result;
}

#endif /* HAL_TARGET_IS_ESP32_S3 */
