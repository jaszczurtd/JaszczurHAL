#pragma once

#include "hal/hal_lora_radio.h"

#ifdef HAL_ENABLE_LORA

#include "hal/hal_sync.h"

#include <stdbool.h>

typedef struct jh_lora_radio_context_s jh_lora_radio_context_t;

typedef struct {
  hal_status_t (*initialize)(jh_lora_radio_context_t *context);
  hal_status_t (*deinitialize)(jh_lora_radio_context_t *context);
  hal_status_t (*configure)(jh_lora_radio_context_t *context);
  hal_status_t (*transmit)(jh_lora_radio_context_t *context,
                           uint32_t timeout_ms);
  hal_status_t (*receive_start)(jh_lora_radio_context_t *context,
                                uint32_t timeout_ms, bool continuous);
  hal_status_t (*receive_poll)(jh_lora_radio_context_t *context);
  hal_status_t (*sleep)(jh_lora_radio_context_t *context);
  hal_status_t (*standby)(jh_lora_radio_context_t *context);
} jh_lora_radio_provider_ops_t;

struct jh_lora_radio_context_s {
  hal_lora_radio_config_t config;
  hal_lora_modem_config_t modem;
  hal_lora_radio_diagnostics_t diagnostics;
  hal_lora_radio_state_t state;
  const jh_lora_radio_provider_ops_t *provider;
  hal_mutex_t mutex;
  uint8_t tx_buffer[HAL_LORA_RADIO_MAX_PAYLOAD];
  uint8_t rx_buffer[HAL_LORA_RADIO_MAX_PAYLOAD];
  hal_lora_packet_info_t rx_info;
  size_t tx_length;
  size_t rx_length;
  uint32_t receive_started_ms;
  uint32_t receive_timeout_ms;
  hal_status_t provider_last_status;
  bool configured;
  bool receive_continuous;
  bool rx_ready;
  bool operation_busy;
  bool provider_sleeping;
  bool board_device;
  bool allocated;
};

/** Return the target's normal provider (mock or the shared SX126x adapter). */
const jh_lora_radio_provider_ops_t *jh_lora_radio_default_provider(void);

/** Internal locked handle access used by the mock control surface. */
hal_status_t jh_lora_radio_context_lock(hal_lora_radio_t radio,
                                        jh_lora_radio_context_t **out_context);
void jh_lora_radio_context_unlock(jh_lora_radio_context_t *context);

#if HAL_TARGET_IS_MOCK
/** Install a provider for host tests; NULL restores the mock provider. */
hal_status_t jh_lora_radio_set_provider_for_test(
    const jh_lora_radio_provider_ops_t *provider);
#endif

#endif /* HAL_ENABLE_LORA */
