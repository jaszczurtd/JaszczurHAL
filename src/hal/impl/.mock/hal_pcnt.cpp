#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#ifdef HAL_ENABLE_PCNT

#include "../../hal_pcnt.h"
#include "hal_mock.h"

#define MOCK_PCNT_CHANNELS 4

static uint32_t        s_count[MOCK_PCNT_CHANNELS] = {};
static bool            s_init[MOCK_PCNT_CHANNELS]  = {};
static hal_pcnt_edge_t s_edge[MOCK_PCNT_CHANNELS]  = {};
static uint8_t         s_pin[MOCK_PCNT_CHANNELS]   = {};

bool hal_pcnt_is_supported(void) {
    return true;
}

uint8_t hal_pcnt_channel_count(void) {
    return MOCK_PCNT_CHANNELS;
}

bool hal_pcnt_init(uint8_t channel, uint8_t pin, hal_pcnt_edge_t edge) {
    if (channel >= MOCK_PCNT_CHANNELS) {
        return false;
    }
    s_init[channel]  = true;
    s_edge[channel]  = edge;
    s_pin[channel]   = pin;
    s_count[channel] = 0u;
    return true;
}

uint32_t hal_pcnt_read(uint8_t channel) {
    return (channel < MOCK_PCNT_CHANNELS && s_init[channel]) ? s_count[channel] : 0u;
}

void hal_pcnt_reset(uint8_t channel) {
    if (channel < MOCK_PCNT_CHANNELS) {
        s_count[channel] = 0u;
    }
}

uint32_t hal_pcnt_read_and_reset(uint8_t channel) {
    const uint32_t v = hal_pcnt_read(channel);
    hal_pcnt_reset(channel);
    return v;
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

/** Simulate @p pulses edges on a channel. */
void hal_mock_pcnt_inject(uint8_t channel, uint32_t pulses) {
    if (channel < MOCK_PCNT_CHANNELS && s_init[channel]) {
        s_count[channel] += pulses;
    }
}

hal_pcnt_edge_t hal_mock_pcnt_get_edge(uint8_t channel) {
    return (channel < MOCK_PCNT_CHANNELS) ? s_edge[channel] : HAL_PCNT_EDGE_RISING;
}

uint8_t hal_mock_pcnt_get_pin(uint8_t channel) {
    return (channel < MOCK_PCNT_CHANNELS) ? s_pin[channel] : 0u;
}

#endif  // HAL_ENABLE_PCNT
#endif  // HAL_TARGET_IS_MOCK
