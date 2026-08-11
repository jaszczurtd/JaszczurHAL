#pragma once

#include "hal/radio/jh_lora_radio_internal.h"

#ifdef HAL_ENABLE_SX127X

#ifdef __cplusplus
extern "C" {
#endif

const jh_lora_radio_provider_ops_t *jh_sx127x_provider_ops(void);

hal_status_t jh_sx127x_write_register(jh_lora_radio_context_t *context,
                                      uint8_t address, uint8_t value);
hal_status_t jh_sx127x_read_register(jh_lora_radio_context_t *context,
                                     uint8_t address, uint8_t *out_value);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_SX127X */
