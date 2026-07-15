#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_external_adc.h"
#include "hal_mock.h"

static float s_adc_range = 0.1875f;
static int16_t s_raw[4] = {};
static float s_scaled[4] = {};
static bool s_initialized = false;

hal_status_t hal_ext_adc_init(uint8_t address, float adc_range) {
  return hal_ext_adc_init_bus(0, address, adc_range);
}

hal_status_t hal_ext_adc_init_bus(uint8_t i2c_bus, uint8_t address,
                                  float adc_range) {
  (void)i2c_bus;
  if (address > 0x7Fu || adc_range <= 0.0f) {
    return HAL_EINVAL;
  }
  s_adc_range = adc_range;
  s_initialized = true;
  return HAL_OK;
}

hal_status_t hal_ext_adc_read_ex(uint8_t channel, int16_t *out) {
  if (!out || channel >= 4) {
    return HAL_EINVAL;
  }
  *out = 0;
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  *out = s_raw[channel];
  return HAL_OK;
}

int16_t hal_ext_adc_read(uint8_t channel) {
  int16_t value = 0;
  (void)hal_ext_adc_read_ex(channel, &value);
  return value;
}

hal_status_t hal_ext_adc_read_scaled_ex(uint8_t channel, float *out) {
  if (!out || channel >= 4) {
    return HAL_EINVAL;
  }
  *out = 0.0f;
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  *out = s_scaled[channel];
  return HAL_OK;
}

float hal_ext_adc_read_scaled(uint8_t channel) {
  float value = 0.0f;
  (void)hal_ext_adc_read_scaled_ex(channel, &value);
  return value;
}

/* ── Mock helpers ─────────────────────────────────────────────────────── */

void hal_mock_ext_adc_inject_raw(uint8_t channel, int16_t value) {
  if (channel < 4)
    s_raw[channel] = value;
}

void hal_mock_ext_adc_inject_scaled(uint8_t channel, float value) {
  if (channel < 4)
    s_scaled[channel] = value;
}

float hal_mock_ext_adc_get_range(void) { return s_adc_range; }
#endif // HAL_TARGET_IS_MOCK
