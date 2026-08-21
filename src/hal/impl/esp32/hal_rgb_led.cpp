#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_RGB_LED

#include "hal/gpio/hal_rgb_led_internal.h"
#include "jh_esp32_gpio.h"
#include "jh_esp32_status.h"

#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>

#include <stdint.h>

namespace {

constexpr uint32_t kResolutionHz = UINT32_C(10000000);
constexpr uint32_t kResetTimeUs = 60u;

rmt_channel_handle_t s_channel;
rmt_encoder_handle_t s_encoder;
uint8_t s_pin = UINT8_MAX;

hal_status_t release_transport(void) {
  if (s_channel != nullptr) {
    const esp_err_t disable_result = rmt_disable(s_channel);
    if (disable_result != ESP_OK && disable_result != ESP_ERR_INVALID_STATE) {
      return jh_esp32_status_from_esp_err(disable_result);
    }
    /* ESP_ERR_INVALID_STATE also means that a partially-created channel is
     * still in its initial (already disabled) state. rmt_del_channel() is the
     * authoritative check: on any other state it fails without freeing the
     * handle, which must remain available for a later teardown retry. */
    const esp_err_t delete_result = rmt_del_channel(s_channel);
    if (delete_result != ESP_OK) {
      return jh_esp32_status_from_esp_err(delete_result);
    }
    s_channel = nullptr;
  }
  if (s_encoder != nullptr) {
    const esp_err_t delete_result = rmt_del_encoder(s_encoder);
    if (delete_result != ESP_OK) {
      return jh_esp32_status_from_esp_err(delete_result);
    }
    s_encoder = nullptr;
  }
  s_pin = UINT8_MAX;
  return HAL_OK;
}

} // namespace

bool jh_hal_rgb_led_pin_valid(uint8_t pin) {
  return jh_esp32_gpio_output_pin_valid(pin);
}

void jh_hal_rgb_led_release_transport(void) { (void)release_transport(); }

hal_status_t jh_hal_rgb_led_prepare_transport(uint8_t pin, bool is800khz) {
  if (!jh_hal_rgb_led_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (!is800khz) {
    return HAL_EUNSUPPORTED;
  }
  const hal_status_t release_status = release_transport();
  if (hal_status_is_error(release_status)) {
    return release_status;
  }

  rmt_tx_channel_config_t tx_config = {};
  tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_config.gpio_num = (gpio_num_t)pin;
  tx_config.mem_block_symbols = 64u;
  tx_config.resolution_hz = kResolutionHz;
  tx_config.trans_queue_depth = 1u;
  esp_err_t result = rmt_new_tx_channel(&tx_config, &s_channel);
  if (result != ESP_OK) {
    (void)release_transport();
    return jh_esp32_status_from_esp_err(result);
  }

  rmt_bytes_encoder_config_t encoder_config = {};
  encoder_config.bit0.level0 = 1u;
  encoder_config.bit0.duration0 = 3u;
  encoder_config.bit0.level1 = 0u;
  encoder_config.bit0.duration1 = 9u;
  encoder_config.bit1.level0 = 1u;
  encoder_config.bit1.duration0 = 9u;
  encoder_config.bit1.level1 = 0u;
  encoder_config.bit1.duration1 = 3u;
  encoder_config.flags.msb_first = 1u;
  result = rmt_new_bytes_encoder(&encoder_config, &s_encoder);
  if (result == ESP_OK) {
    result = rmt_enable(s_channel);
  }
  if (result != ESP_OK) {
    (void)release_transport();
    return jh_esp32_status_from_esp_err(result);
  }
  s_pin = pin;
  return HAL_OK;
}

bool jh_hal_rgb_led_write_pixels(const uint8_t *pixels, uint32_t num_bytes,
                                 bool is800khz, uint8_t pin, void *user) {
  (void)user;
  if (pixels == nullptr || num_bytes == 0u) {
    return true;
  }
  if (!is800khz || pin != s_pin || s_channel == nullptr ||
      s_encoder == nullptr) {
    return false;
  }

  rmt_transmit_config_t config = {};
  config.loop_count = 0;
  esp_err_t result =
      rmt_transmit(s_channel, s_encoder, pixels, num_bytes, &config);
  if (result == ESP_OK) {
    result = rmt_tx_wait_all_done(s_channel, portMAX_DELAY);
  }
  if (result == ESP_OK) {
    esp_rom_delay_us(kResetTimeUs);
  }
  return result == ESP_OK;
}

#endif // HAL_ENABLE_RGB_LED
#endif // HAL_TARGET_IS_ESP32_FAMILY
