#pragma once

#include "hal/radio/jh_lora_radio_internal.h"

#ifdef HAL_ENABLE_SX126X

#include "sx126x_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

const jh_lora_radio_provider_ops_t *jh_sx126x_provider_ops(void);

hal_status_t jh_sx126x_wait_while_busy(jh_lora_radio_context_t *context,
                                       uint32_t timeout_ms);
void jh_sx126x_set_rf_idle(jh_lora_radio_context_t *context);
void jh_sx126x_set_rf_rx(jh_lora_radio_context_t *context);
void jh_sx126x_set_rf_tx(jh_lora_radio_context_t *context);

sx126x_hal_status_t sx126x_hal_write(const void *context,
                                     const uint8_t *command,
                                     uint16_t command_length,
                                     const uint8_t *data, uint16_t data_length);
sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *command,
                                    uint16_t command_length, uint8_t *data,
                                    uint16_t data_length);
sx126x_hal_status_t sx126x_hal_reset(const void *context);
sx126x_hal_status_t sx126x_hal_wakeup(const void *context);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_SX126X */
