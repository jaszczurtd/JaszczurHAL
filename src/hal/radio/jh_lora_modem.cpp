#include "jh_lora_modem.h"

#ifdef HAL_ENABLE_LORA

#include <limits.h>

static bool normalize_bandwidth(uint32_t bandwidth_hz,
                                uint32_t *out_bandwidth_hz) {
  if (out_bandwidth_hz == NULL) {
    return false;
  }
  switch (bandwidth_hz) {
  case 7800u:
  case 7812u:
    *out_bandwidth_hz = 7812u;
    return true;
  case 10400u:
  case 10417u:
    *out_bandwidth_hz = 10417u;
    return true;
  case 15600u:
  case 15625u:
    *out_bandwidth_hz = 15625u;
    return true;
  case 20800u:
  case 20833u:
    *out_bandwidth_hz = 20833u;
    return true;
  case 31250u:
    *out_bandwidth_hz = 31250u;
    return true;
  case 41667u:
  case 41700u:
    *out_bandwidth_hz = 41667u;
    return true;
  case 62500u:
    *out_bandwidth_hz = 62500u;
    return true;
  case 125000u:
    *out_bandwidth_hz = 125000u;
    return true;
  case 250000u:
    *out_bandwidth_hz = 250000u;
    return true;
  case 500000u:
    *out_bandwidth_hz = 500000u;
    return true;
  default:
    return false;
  }
}

bool jh_lora_modem_config_valid(const hal_lora_modem_config_t *config,
                                uint32_t min_frequency_hz,
                                uint32_t max_frequency_hz,
                                int8_t min_tx_power_dbm,
                                int8_t max_tx_power_dbm,
                                uint8_t min_spreading_factor) {
  uint32_t normalized_bandwidth_hz = 0u;
  return config != NULL && config->frequency_hz >= min_frequency_hz &&
         config->frequency_hz <= max_frequency_hz &&
         normalize_bandwidth(config->bandwidth_hz, &normalized_bandwidth_hz) &&
         config->spreading_factor >= min_spreading_factor &&
         config->spreading_factor <= 12u && config->coding_rate >= 5u &&
         config->coding_rate <= 8u &&
         config->tx_power_dbm >= min_tx_power_dbm &&
         config->tx_power_dbm <= max_tx_power_dbm &&
         config->preamble_symbols > 0u &&
         (config->explicit_header || config->implicit_payload_length > 0u);
}

hal_status_t jh_lora_modem_time_on_air(const hal_lora_modem_config_t *config,
                                       size_t payload_length,
                                       uint32_t *out_time_ms) {
  if (out_time_ms != NULL) {
    *out_time_ms = 0u;
  }
  uint32_t bandwidth_hz = 0u;
  if (config == NULL || out_time_ms == NULL || payload_length > UINT8_MAX ||
      !normalize_bandwidth(config->bandwidth_hz, &bandwidth_hz) ||
      config->spreading_factor < 5u || config->spreading_factor > 12u ||
      config->coding_rate < 5u || config->coding_rate > 8u ||
      config->preamble_symbols == 0u ||
      (!config->explicit_header &&
       (config->implicit_payload_length == 0u ||
        payload_length != config->implicit_payload_length))) {
    return HAL_EINVAL;
  }

  const uint8_t spreading_factor = config->spreading_factor;
  const uint64_t symbol_numerator = UINT64_C(1) << spreading_factor;
  const bool low_data_rate_optimize = symbol_numerator * UINT64_C(1000000) >=
                                      (uint64_t)bandwidth_hz * UINT64_C(16000);
  int32_t payload_numerator = 8 * (int32_t)payload_length -
                              4 * spreading_factor +
                              (config->crc_enabled ? 16 : 0);
  if (spreading_factor <= 6u) {
    payload_numerator += config->explicit_header ? 20 : 0;
  } else {
    payload_numerator += config->explicit_header ? 28 : 8;
  }
  const int32_t payload_denominator =
      4 * (spreading_factor -
           (spreading_factor > 6u && low_data_rate_optimize ? 2 : 0));
  uint32_t coded_payload_symbols = 0u;
  if (payload_numerator > 0) {
    coded_payload_symbols =
        (uint32_t)((payload_numerator + payload_denominator - 1) /
                   payload_denominator) *
        config->coding_rate;
  }

  const uint64_t quarter_symbols = (uint64_t)config->preamble_symbols * 4u +
                                   (spreading_factor <= 6u ? 57u : 49u) +
                                   (uint64_t)coded_payload_symbols * 4u;
  const uint64_t numerator_ms =
      quarter_symbols * symbol_numerator * UINT64_C(1000);
  const uint64_t denominator = (uint64_t)bandwidth_hz * 4u;
  const uint64_t time_ms = (numerator_ms + denominator - 1u) / denominator;
  if (time_ms == 0u || time_ms > UINT32_MAX) {
    return time_ms > UINT32_MAX ? HAL_EOVERFLOW : HAL_EINVAL;
  }
  *out_time_ms = (uint32_t)time_ms;
  return HAL_OK;
}

#endif /* HAL_ENABLE_LORA */
