#pragma once

#include "hal/radio/hal_lora_radio.h"

#ifdef HAL_ENABLE_LORA

bool jh_lora_modem_config_valid(const hal_lora_modem_config_t *config,
                                uint32_t min_frequency_hz,
                                uint32_t max_frequency_hz,
                                int8_t min_tx_power_dbm,
                                int8_t max_tx_power_dbm,
                                uint8_t min_spreading_factor);

hal_status_t jh_lora_modem_time_on_air(const hal_lora_modem_config_t *config,
                                       size_t payload_length,
                                       uint32_t *out_time_ms);

#endif /* HAL_ENABLE_LORA */
