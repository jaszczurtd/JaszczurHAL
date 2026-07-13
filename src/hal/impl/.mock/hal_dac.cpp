#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#ifdef HAL_ENABLE_DAC

#include "../../hal_dac.h"
#include "hal_mock.h"

#ifndef HAL_DAC_VREF_MV
#define HAL_DAC_VREF_MV 3300u
#endif

#define MOCK_DAC_CHANNELS 2
#define MOCK_DAC_RES_BITS 12u

static uint16_t s_value[MOCK_DAC_CHANNELS] = {};
static bool s_init[MOCK_DAC_CHANNELS] = {};

bool hal_dac_is_supported(void) { return true; }

uint8_t hal_dac_resolution_bits(void) { return MOCK_DAC_RES_BITS; }

uint16_t hal_dac_max_value(void) {
  return (uint16_t)((1u << MOCK_DAC_RES_BITS) - 1u);
}

hal_status_t hal_dac_init_ex(uint8_t channel) {
  if (channel >= MOCK_DAC_CHANNELS) {
    return HAL_EINVAL;
  }
  s_init[channel] = true;
  s_value[channel] = 0u;
  return HAL_OK;
}

bool hal_dac_init(uint8_t channel) {
  return hal_status_to_bool(hal_dac_init_ex(channel));
}

hal_status_t hal_dac_write(uint8_t channel, uint16_t value) {
  if (channel >= MOCK_DAC_CHANNELS) {
    return HAL_EINVAL;
  }
  if (!s_init[channel]) {
    return HAL_EUNINIT;
  }
  const uint16_t max = hal_dac_max_value();
  s_value[channel] = (value > max) ? max : value;
  return HAL_OK;
}

hal_status_t hal_dac_write_millivolts(uint8_t channel, uint16_t millivolts) {
  if (millivolts > HAL_DAC_VREF_MV) {
    millivolts = HAL_DAC_VREF_MV;
  }
  const uint32_t code =
      ((uint32_t)millivolts * hal_dac_max_value()) / HAL_DAC_VREF_MV;
  return hal_dac_write(channel, (uint16_t)code);
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

uint16_t hal_mock_dac_get(uint8_t channel) {
  return (channel < MOCK_DAC_CHANNELS) ? s_value[channel] : 0u;
}

bool hal_mock_dac_is_initialized(uint8_t channel) {
  return (channel < MOCK_DAC_CHANNELS) ? s_init[channel] : false;
}

void hal_mock_dac_reset(void) {
  for (uint8_t i = 0; i < MOCK_DAC_CHANNELS; ++i) {
    s_init[i] = false;
    s_value[i] = 0u;
  }
}

#endif // HAL_ENABLE_DAC
#endif // HAL_TARGET_IS_MOCK
