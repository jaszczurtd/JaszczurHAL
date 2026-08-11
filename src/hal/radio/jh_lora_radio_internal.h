#pragma once

#include "hal/radio/hal_lora_radio.h"

#ifdef HAL_ENABLE_LORA

#include "hal/system/hal_sync.h"

#include <stdbool.h>

typedef struct jh_lora_radio_context_s jh_lora_radio_context_t;

typedef uint32_t jh_lora_provider_events_t;

#define JH_LORA_PROVIDER_EVENT_NONE UINT32_C(0)
#define JH_LORA_PROVIDER_EVENT_TX_DONE (UINT32_C(1) << 0u)
#define JH_LORA_PROVIDER_EVENT_RX_DONE (UINT32_C(1) << 1u)
#define JH_LORA_PROVIDER_EVENT_TIMEOUT (UINT32_C(1) << 2u)
#define JH_LORA_PROVIDER_EVENT_CRC_ERROR (UINT32_C(1) << 3u)
#define JH_LORA_PROVIDER_EVENT_HEADER_ERROR (UINT32_C(1) << 4u)
#define JH_LORA_PROVIDER_EVENT_IRQ (UINT32_C(1) << 5u)
#define JH_LORA_PROVIDER_EVENT_CAD_DONE (UINT32_C(1) << 6u)
#define JH_LORA_PROVIDER_EVENT_CAD_DETECTED (UINT32_C(1) << 7u)

typedef uint32_t jh_lora_provider_capabilities_t;

#define JH_LORA_PROVIDER_CAP_CONTINUOUS_RX (UINT32_C(1) << 0u)
#define JH_LORA_PROVIDER_CAP_CAD (UINT32_C(1) << 1u)
#define JH_LORA_PROVIDER_CAP_INSTANT_RSSI (UINT32_C(1) << 2u)
#define JH_LORA_PROVIDER_CAP_EXPLICIT_CALIBRATION (UINT32_C(1) << 3u)
#define JH_LORA_PROVIDER_CAP_ALL                                               \
  (JH_LORA_PROVIDER_CAP_CONTINUOUS_RX | JH_LORA_PROVIDER_CAP_CAD |             \
   JH_LORA_PROVIDER_CAP_INSTANT_RSSI |                                         \
   JH_LORA_PROVIDER_CAP_EXPLICIT_CALIBRATION)
#define JH_LORA_PROVIDER_CAP_SX126X JH_LORA_PROVIDER_CAP_ALL
#define JH_LORA_PROVIDER_CAP_SX127X                                            \
  (JH_LORA_PROVIDER_CAP_CONTINUOUS_RX | JH_LORA_PROVIDER_CAP_CAD |             \
   JH_LORA_PROVIDER_CAP_INSTANT_RSSI)

typedef struct {
  hal_status_t (*initialize)(jh_lora_radio_context_t *context);
  hal_status_t (*deinitialize)(jh_lora_radio_context_t *context);
  hal_status_t (*configure)(jh_lora_radio_context_t *context);
  hal_status_t (*get_capabilities)(
      jh_lora_radio_context_t *context,
      hal_lora_radio_capabilities_t *out_capabilities);
  hal_status_t (*get_instant_rssi)(jh_lora_radio_context_t *context,
                                   int16_t *out_rssi_dbm);
  hal_status_t (*transmit_start)(jh_lora_radio_context_t *context,
                                 uint32_t timeout_ms);
  hal_status_t (*receive_start)(jh_lora_radio_context_t *context,
                                uint32_t timeout_ms, bool continuous);
  hal_status_t (*channel_activity_detect_start)(
      jh_lora_radio_context_t *context, uint32_t timeout_ms);
  hal_status_t (*process)(jh_lora_radio_context_t *context,
                          jh_lora_provider_events_t *out_events);
  hal_status_t (*cancel)(jh_lora_radio_context_t *context);
  hal_status_t (*sleep)(jh_lora_radio_context_t *context);
  hal_status_t (*standby)(jh_lora_radio_context_t *context);
  hal_status_t (*calibrate)(jh_lora_radio_context_t *context);
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
  uint32_t transmit_started_ms;
  uint32_t transmit_timeout_ms;
  uint32_t receive_started_ms;
  uint32_t receive_timeout_ms;
  uint32_t channel_activity_started_ms;
  uint32_t channel_activity_timeout_ms;
  hal_lora_operation_status_t tx_status;
  hal_lora_channel_activity_status_t channel_activity_status;
  hal_status_t rx_result;
  hal_lora_radio_event_callback_t event_callback;
  void *event_user_data;
  hal_lora_radio_event_t pending_event;
  hal_lora_radio_t handle;
  hal_status_t provider_last_status;
  uint32_t calibrated_frequency_min_hz;
  uint32_t calibrated_frequency_max_hz;
  bool configured;
  bool receive_continuous;
  bool rx_ready;
  bool event_pending;
  bool operation_busy;
  bool provider_sleeping;
  bool provider_irq_attached;
  bool board_device;
  bool allocated;
};

/** Return the target's normal provider selected by the feature registry. */
const jh_lora_radio_provider_ops_t *jh_lora_radio_default_provider(void);

/** Internal locked handle access used by the mock control surface. */
hal_status_t jh_lora_radio_context_lock(hal_lora_radio_t radio,
                                        jh_lora_radio_context_t **out_context);
void jh_lora_radio_context_unlock(jh_lora_radio_context_t *context);

/** Build the common capability snapshot from provider-owned support flags. */
hal_status_t jh_lora_radio_describe_capabilities(
    const jh_lora_radio_context_t *context,
    jh_lora_provider_capabilities_t supported,
    hal_lora_radio_capabilities_t *out_capabilities);

#if HAL_TARGET_IS_MOCK || defined(JH_LORA_PROVIDER_TESTING)
/** Install a provider for host tests; NULL restores the default provider. */
hal_status_t jh_lora_radio_set_provider_for_test(
    const jh_lora_radio_provider_ops_t *provider);
#endif

#endif /* HAL_ENABLE_LORA */
