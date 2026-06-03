#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#ifdef HAL_ENABLE_DAC

#include "../../hal_dac.h"
#include "hal_mock.h"

#ifndef HAL_DAC_VREF_MV
#define HAL_DAC_VREF_MV 3300u
#endif

#define MOCK_DAC_CHANNELS   2
#define MOCK_DAC_RES_BITS   12u

static uint16_t s_value[MOCK_DAC_CHANNELS] = {};
static bool     s_init[MOCK_DAC_CHANNELS]  = {};

bool hal_dac_is_supported(void) {
    return true;
}

uint8_t hal_dac_resolution_bits(void) {
    return MOCK_DAC_RES_BITS;
}

uint16_t hal_dac_max_value(void) {
    return (uint16_t)((1u << MOCK_DAC_RES_BITS) - 1u);
}

bool hal_dac_init(uint8_t channel) {
    if (channel >= MOCK_DAC_CHANNELS) {
        return false;
    }
    s_init[channel] = true;
    s_value[channel] = 0u;
    return true;
}

void hal_dac_write(uint8_t channel, uint16_t value) {
    if (channel >= MOCK_DAC_CHANNELS || !s_init[channel]) {
        return;
    }
    const uint16_t max = hal_dac_max_value();
    s_value[channel] = (value > max) ? max : value;
}

void hal_dac_write_millivolts(uint8_t channel, uint16_t millivolts) {
    if (millivolts > HAL_DAC_VREF_MV) {
        millivolts = HAL_DAC_VREF_MV;
    }
    const uint32_t code = ((uint32_t)millivolts * hal_dac_max_value()) / HAL_DAC_VREF_MV;
    hal_dac_write(channel, (uint16_t)code);
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

uint16_t hal_mock_dac_get(uint8_t channel) {
    return (channel < MOCK_DAC_CHANNELS) ? s_value[channel] : 0u;
}

bool hal_mock_dac_is_initialized(uint8_t channel) {
    return (channel < MOCK_DAC_CHANNELS) ? s_init[channel] : false;
}

#endif  // HAL_ENABLE_DAC
#endif  // HAL_TARGET_IS_MOCK
