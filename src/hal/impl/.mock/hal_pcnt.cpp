#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#ifdef HAL_ENABLE_PCNT

#include "../../hal_pcnt.h"
#include "hal_mock.h"

#define MOCK_PCNT_CHANNELS 4

static uint32_t s_count[MOCK_PCNT_CHANNELS] = {};
static bool s_init[MOCK_PCNT_CHANNELS] = {};
static hal_pcnt_edge_t s_edge[MOCK_PCNT_CHANNELS] = {};
static uint8_t s_pin[MOCK_PCNT_CHANNELS] = {};

static bool edge_valid(hal_pcnt_edge_t edge) {
  return edge == HAL_PCNT_EDGE_RISING || edge == HAL_PCNT_EDGE_FALLING ||
         edge == HAL_PCNT_EDGE_BOTH;
}

bool hal_pcnt_is_supported(void) { return true; }

uint8_t hal_pcnt_channel_count(void) { return MOCK_PCNT_CHANNELS; }

hal_status_t hal_pcnt_init_ex(uint8_t channel, uint8_t pin,
                              hal_pcnt_edge_t edge) {
  if (channel >= MOCK_PCNT_CHANNELS || !edge_valid(edge)) {
    return HAL_EINVAL;
  }
  s_init[channel] = true;
  s_edge[channel] = edge;
  s_pin[channel] = pin;
  s_count[channel] = 0u;
  return HAL_OK;
}

bool hal_pcnt_init(uint8_t channel, uint8_t pin, hal_pcnt_edge_t edge) {
  return hal_status_to_bool(hal_pcnt_init_ex(channel, pin, edge));
}

hal_status_t hal_pcnt_read_ex(uint8_t channel, uint32_t *out_count) {
  if (out_count == nullptr || channel >= MOCK_PCNT_CHANNELS) {
    return HAL_EINVAL;
  }
  *out_count = 0u;
  if (!s_init[channel]) {
    return HAL_EUNINIT;
  }
  *out_count = s_count[channel];
  return HAL_OK;
}

uint32_t hal_pcnt_read(uint8_t channel) {
  uint32_t count = 0u;
  (void)hal_pcnt_read_ex(channel, &count);
  return count;
}

hal_status_t hal_pcnt_reset_ex(uint8_t channel) {
  if (channel >= MOCK_PCNT_CHANNELS) {
    return HAL_EINVAL;
  }
  if (!s_init[channel]) {
    return HAL_EUNINIT;
  }
  s_count[channel] = 0u;
  return HAL_OK;
}

void hal_pcnt_reset(uint8_t channel) { (void)hal_pcnt_reset_ex(channel); }

hal_status_t hal_pcnt_read_and_reset_ex(uint8_t channel, uint32_t *out_count) {
  hal_status_t status = hal_pcnt_read_ex(channel, out_count);
  if (status != HAL_OK) {
    return status;
  }
  return hal_pcnt_reset_ex(channel);
}

uint32_t hal_pcnt_read_and_reset(uint8_t channel) {
  uint32_t count = 0u;
  (void)hal_pcnt_read_and_reset_ex(channel, &count);
  return count;
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

/** Simulate @p pulses edges on a channel. */
void hal_mock_pcnt_inject(uint8_t channel, uint32_t pulses) {
  if (channel < MOCK_PCNT_CHANNELS && s_init[channel]) {
    s_count[channel] += pulses;
  }
}

hal_pcnt_edge_t hal_mock_pcnt_get_edge(uint8_t channel) {
  return (channel < MOCK_PCNT_CHANNELS) ? s_edge[channel]
                                        : HAL_PCNT_EDGE_RISING;
}

uint8_t hal_mock_pcnt_get_pin(uint8_t channel) {
  return (channel < MOCK_PCNT_CHANNELS) ? s_pin[channel] : 0u;
}

void hal_mock_pcnt_reset(void) {
  for (uint8_t i = 0; i < MOCK_PCNT_CHANNELS; ++i) {
    s_count[i] = 0u;
    s_init[i] = false;
    s_edge[i] = HAL_PCNT_EDGE_RISING;
    s_pin[i] = 0u;
  }
}

#endif // HAL_ENABLE_PCNT
#endif // HAL_TARGET_IS_MOCK
