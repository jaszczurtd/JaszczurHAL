#include "hal/core/hal_config.h"

#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_LORA)

#include "hal/radio/jh_lora_radio_internal.h"
#include "hal/system/hal_system.h"
#include "hal_mock.h"

#include <string.h>

typedef struct {
  jh_lora_radio_context_t *context;
  jh_lora_radio_context_t *peer;
  hal_status_t next_status[HAL_MOCK_LORA_STANDBY + 1u];
  uint8_t pending_rx[HAL_LORA_RADIO_MAX_PAYLOAD];
  size_t pending_rx_length;
  hal_lora_packet_info_t pending_info;
  int16_t instant_rssi_dbm;
  bool pending_rx_ready;
  bool pending_cad_ready;
  bool pending_cad_detected;
} jh_mock_lora_state_t;

static jh_mock_lora_state_t s_states[HAL_LORA_RADIO_MAX_INSTANCES] = {};
static hal_status_t s_next_initialize_status = HAL_OK;

static jh_mock_lora_state_t *find_state(jh_lora_radio_context_t *context) {
  for (size_t index = 0u; index < HAL_LORA_RADIO_MAX_INSTANCES; ++index) {
    if (s_states[index].context == context) {
      return &s_states[index];
    }
  }
  return NULL;
}

static jh_mock_lora_state_t *allocate_state(jh_lora_radio_context_t *context) {
  for (size_t index = 0u; index < HAL_LORA_RADIO_MAX_INSTANCES; ++index) {
    if (s_states[index].context == NULL) {
      memset(&s_states[index], 0, sizeof(s_states[index]));
      s_states[index].context = context;
      s_states[index].instant_rssi_dbm = -100;
      return &s_states[index];
    }
  }
  return NULL;
}

static hal_status_t consume(jh_mock_lora_state_t *state,
                            hal_mock_lora_operation_t operation) {
  if (state == NULL || operation > HAL_MOCK_LORA_STANDBY) {
    return HAL_EINVAL;
  }
  const hal_status_t status = state->next_status[operation];
  state->next_status[operation] = HAL_NONE;
  return status == HAL_NONE ? HAL_OK : status;
}

static hal_status_t mock_initialize(jh_lora_radio_context_t *context) {
  const hal_status_t status = s_next_initialize_status;
  s_next_initialize_status = HAL_OK;
  if (status != HAL_OK) {
    return status;
  }
  if (allocate_state(context) == NULL) {
    return HAL_ENOMEM;
  }
  ++context->diagnostics.full_calibrations;
  return HAL_OK;
}

static hal_status_t mock_deinitialize(jh_lora_radio_context_t *context) {
  jh_mock_lora_state_t *state = find_state(context);
  if (state == NULL) {
    return HAL_EUNINIT;
  }
  for (size_t index = 0u; index < HAL_LORA_RADIO_MAX_INSTANCES; ++index) {
    if (s_states[index].peer == context) {
      s_states[index].peer = NULL;
    }
  }
  memset(state, 0, sizeof(*state));
  return HAL_OK;
}

static hal_status_t mock_configure(jh_lora_radio_context_t *context) {
  const hal_status_t status =
      consume(find_state(context), HAL_MOCK_LORA_CONFIGURE);
  if (status == HAL_OK &&
      (context->calibrated_frequency_min_hz == 0u ||
       context->modem.frequency_hz < context->calibrated_frequency_min_hz ||
       context->modem.frequency_hz > context->calibrated_frequency_max_hz)) {
    context->calibrated_frequency_min_hz = context->modem.frequency_hz;
    context->calibrated_frequency_max_hz = context->modem.frequency_hz;
    context->diagnostics.calibrated_frequency_min_hz =
        context->modem.frequency_hz;
    context->diagnostics.calibrated_frequency_max_hz =
        context->modem.frequency_hz;
    ++context->diagnostics.image_calibrations;
  }
  return status;
}

static hal_status_t
mock_get_capabilities(jh_lora_radio_context_t *context,
                      hal_lora_radio_capabilities_t *out_capabilities) {
  return jh_lora_radio_describe_capabilities(
      context,
      context->config.model == HAL_LORA_RADIO_SX1276 ||
              context->config.model == HAL_LORA_RADIO_SX1278
          ? JH_LORA_PROVIDER_CAP_SX127X
          : JH_LORA_PROVIDER_CAP_SX126X,
      out_capabilities);
}

static hal_status_t mock_get_instant_rssi(jh_lora_radio_context_t *context,
                                          int16_t *out_rssi_dbm) {
  if (out_rssi_dbm == NULL) {
    return HAL_EINVAL;
  }
  jh_mock_lora_state_t *state = find_state(context);
  const hal_status_t status = consume(state, HAL_MOCK_LORA_GET_INSTANT_RSSI);
  if (status == HAL_OK) {
    *out_rssi_dbm = state->instant_rssi_dbm;
  }
  return status;
}

static hal_status_t mock_transmit_start(jh_lora_radio_context_t *context,
                                        uint32_t timeout_ms) {
  (void)timeout_ms;
  return consume(find_state(context), HAL_MOCK_LORA_TRANSMIT);
}

static hal_status_t mock_deliver_transmit(jh_lora_radio_context_t *context) {
  jh_mock_lora_state_t *state = find_state(context);
  if (state == NULL || state->peer == NULL) {
    return state == NULL ? HAL_EUNINIT : HAL_OK;
  }
  jh_mock_lora_state_t *peer = find_state(state->peer);
  if (peer == NULL || context->tx_length > sizeof(peer->pending_rx)) {
    return peer == NULL ? HAL_EUNINIT : HAL_EOVERFLOW;
  }
  if (state->peer->state != HAL_LORA_RADIO_STATE_RX) {
    return HAL_OK;
  }
  memcpy(peer->pending_rx, context->tx_buffer, context->tx_length);
  peer->pending_rx_length = context->tx_length;
  peer->pending_info.rssi_dbm = -70;
  peer->pending_info.snr_db = 8;
  peer->pending_info.signal_rssi_dbm = -72;
  peer->pending_info.timestamp_ms = hal_millis();
  peer->pending_info.crc_valid = true;
  peer->pending_rx_ready = true;
  return HAL_OK;
}

static hal_status_t mock_receive_start(jh_lora_radio_context_t *context,
                                       uint32_t timeout_ms, bool continuous) {
  (void)timeout_ms;
  (void)continuous;
  return consume(find_state(context), HAL_MOCK_LORA_RECEIVE_START);
}

static hal_status_t
mock_channel_activity_detect_start(jh_lora_radio_context_t *context,
                                   uint32_t timeout_ms) {
  (void)timeout_ms;
  return consume(find_state(context), HAL_MOCK_LORA_CAD_START);
}

static hal_status_t mock_process(jh_lora_radio_context_t *context,
                                 jh_lora_provider_events_t *out_events) {
  if (out_events == NULL) {
    return HAL_EINVAL;
  }
  *out_events = JH_LORA_PROVIDER_EVENT_NONE;
  if (context->state == HAL_LORA_RADIO_STATE_TX) {
    const hal_status_t status = mock_deliver_transmit(context);
    if (status == HAL_OK) {
      *out_events = JH_LORA_PROVIDER_EVENT_TX_DONE | JH_LORA_PROVIDER_EVENT_IRQ;
    }
    return status;
  }
  if (context->state == HAL_LORA_RADIO_STATE_CAD) {
    jh_mock_lora_state_t *state = find_state(context);
    const hal_status_t status = consume(state, HAL_MOCK_LORA_CAD_POLL);
    if (status != HAL_OK) {
      return status;
    }
    if (state->pending_cad_ready) {
      *out_events =
          JH_LORA_PROVIDER_EVENT_CAD_DONE | JH_LORA_PROVIDER_EVENT_IRQ;
      if (state->pending_cad_detected) {
        *out_events |= JH_LORA_PROVIDER_EVENT_CAD_DETECTED;
      }
      state->pending_cad_ready = false;
      return HAL_OK;
    }
    if ((uint32_t)(hal_millis() - context->channel_activity_started_ms) >=
        context->channel_activity_timeout_ms) {
      *out_events = JH_LORA_PROVIDER_EVENT_TIMEOUT;
      return HAL_OK;
    }
    return HAL_EAGAIN;
  }
  if (context->state != HAL_LORA_RADIO_STATE_RX) {
    return HAL_EAGAIN;
  }
  jh_mock_lora_state_t *state = find_state(context);
  hal_status_t status = consume(state, HAL_MOCK_LORA_RECEIVE_POLL);
  if (status != HAL_OK) {
    return status;
  }
  if (state->pending_rx_ready) {
    if (!state->pending_info.crc_valid) {
      state->pending_rx_ready = false;
      *out_events =
          JH_LORA_PROVIDER_EVENT_CRC_ERROR | JH_LORA_PROVIDER_EVENT_IRQ;
      return HAL_OK;
    }
    memcpy(context->rx_buffer, state->pending_rx, state->pending_rx_length);
    context->rx_length = state->pending_rx_length;
    context->rx_info = state->pending_info;
    context->rx_ready = true;
    state->pending_rx_ready = false;
    *out_events = JH_LORA_PROVIDER_EVENT_RX_DONE | JH_LORA_PROVIDER_EVENT_IRQ;
    return HAL_OK;
  }
  if (!context->receive_continuous &&
      (uint32_t)(hal_millis() - context->receive_started_ms) >=
          context->receive_timeout_ms) {
    *out_events = JH_LORA_PROVIDER_EVENT_TIMEOUT;
    return HAL_OK;
  }
  return HAL_EAGAIN;
}

static hal_status_t mock_cancel(jh_lora_radio_context_t *context) {
  return consume(find_state(context), HAL_MOCK_LORA_CANCEL);
}

static hal_status_t mock_sleep(jh_lora_radio_context_t *context) {
  return consume(find_state(context), HAL_MOCK_LORA_SLEEP);
}

static hal_status_t mock_standby(jh_lora_radio_context_t *context) {
  return consume(find_state(context), HAL_MOCK_LORA_STANDBY);
}

static hal_status_t mock_calibrate(jh_lora_radio_context_t *context) {
  if (context->config.model == HAL_LORA_RADIO_SX1276 ||
      context->config.model == HAL_LORA_RADIO_SX1278) {
    return HAL_EUNSUPPORTED;
  }
  const hal_status_t status =
      consume(find_state(context), HAL_MOCK_LORA_CALIBRATE);
  if (status == HAL_OK) {
    ++context->diagnostics.full_calibrations;
    ++context->diagnostics.image_calibrations;
    context->calibrated_frequency_min_hz = context->modem.frequency_hz;
    context->calibrated_frequency_max_hz = context->modem.frequency_hz;
    context->diagnostics.calibrated_frequency_min_hz =
        context->modem.frequency_hz;
    context->diagnostics.calibrated_frequency_max_hz =
        context->modem.frequency_hz;
  }
  return status;
}

static const jh_lora_radio_provider_ops_t s_mock_provider = {
    mock_initialize,
    mock_deinitialize,
    mock_configure,
    mock_get_capabilities,
    mock_get_instant_rssi,
    mock_transmit_start,
    mock_receive_start,
    mock_channel_activity_detect_start,
    mock_process,
    mock_cancel,
    mock_sleep,
    mock_standby,
    mock_calibrate,
};

const jh_lora_radio_provider_ops_t *jh_lora_radio_default_provider(void) {
  return &s_mock_provider;
}

void hal_mock_lora_set_next_initialize_status(hal_status_t status) {
  s_next_initialize_status = status;
}

hal_status_t hal_mock_lora_set_next_status(hal_lora_radio_t radio,
                                           hal_mock_lora_operation_t operation,
                                           hal_status_t status) {
  if (operation > HAL_MOCK_LORA_STANDBY || status == HAL_NONE) {
    return HAL_EINVAL;
  }
  jh_lora_radio_context_t *context = NULL;
  hal_status_t result = jh_lora_radio_context_lock(radio, &context);
  if (result != HAL_OK) {
    return result;
  }
  jh_mock_lora_state_t *state = find_state(context);
  if (state == NULL) {
    result = HAL_EUNINIT;
  } else {
    state->next_status[operation] = status;
  }
  jh_lora_radio_context_unlock(context);
  return result;
}

hal_status_t hal_mock_lora_inject_receive(hal_lora_radio_t radio,
                                          const uint8_t *data, size_t length,
                                          const hal_lora_packet_info_t *info) {
  if (data == NULL || length == 0u || length > HAL_LORA_RADIO_MAX_PAYLOAD) {
    return HAL_EINVAL;
  }
  jh_lora_radio_context_t *context = NULL;
  hal_status_t result = jh_lora_radio_context_lock(radio, &context);
  if (result != HAL_OK) {
    return result;
  }
  jh_mock_lora_state_t *state = find_state(context);
  if (state == NULL) {
    result = HAL_EUNINIT;
  } else {
    memcpy(state->pending_rx, data, length);
    state->pending_rx_length = length;
    memset(&state->pending_info, 0, sizeof(state->pending_info));
    if (info != NULL) {
      state->pending_info = *info;
    } else {
      state->pending_info.crc_valid = true;
      state->pending_info.timestamp_ms = hal_millis();
    }
    state->pending_rx_ready = true;
  }
  jh_lora_radio_context_unlock(context);
  return result;
}

hal_status_t hal_mock_lora_set_instant_rssi(hal_lora_radio_t radio,
                                            int16_t rssi_dbm) {
  jh_lora_radio_context_t *context = NULL;
  hal_status_t result = jh_lora_radio_context_lock(radio, &context);
  if (result != HAL_OK) {
    return result;
  }
  jh_mock_lora_state_t *state = find_state(context);
  if (state == NULL) {
    result = HAL_EUNINIT;
  } else {
    state->instant_rssi_dbm = rssi_dbm;
  }
  jh_lora_radio_context_unlock(context);
  return result;
}

hal_status_t hal_mock_lora_inject_channel_activity(hal_lora_radio_t radio,
                                                   bool detected) {
  jh_lora_radio_context_t *context = NULL;
  hal_status_t result = jh_lora_radio_context_lock(radio, &context);
  if (result != HAL_OK) {
    return result;
  }
  jh_mock_lora_state_t *state = find_state(context);
  if (state == NULL) {
    result = HAL_EUNINIT;
  } else {
    state->pending_cad_ready = true;
    state->pending_cad_detected = detected;
  }
  jh_lora_radio_context_unlock(context);
  return result;
}

hal_status_t hal_mock_lora_get_last_transmit(hal_lora_radio_t radio,
                                             uint8_t *buffer,
                                             size_t buffer_size,
                                             size_t *out_length) {
  if (out_length == NULL || (buffer_size > 0u && buffer == NULL)) {
    return HAL_EINVAL;
  }
  jh_lora_radio_context_t *context = NULL;
  const hal_status_t result = jh_lora_radio_context_lock(radio, &context);
  if (result != HAL_OK) {
    return result;
  }
  *out_length = context->tx_length;
  const size_t copy_length =
      context->tx_length < buffer_size ? context->tx_length : buffer_size;
  if (copy_length > 0u) {
    memcpy(buffer, context->tx_buffer, copy_length);
  }
  jh_lora_radio_context_unlock(context);
  return copy_length < *out_length ? HAL_EOVERFLOW : HAL_OK;
}

hal_status_t hal_mock_lora_connect(hal_lora_radio_t first,
                                   hal_lora_radio_t second) {
  if (first == second) {
    return HAL_EINVAL;
  }
  jh_lora_radio_context_t *first_context = NULL;
  hal_status_t status = jh_lora_radio_context_lock(first, &first_context);
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_radio_context_t *second_context = NULL;
  status = jh_lora_radio_context_lock(second, &second_context);
  if (status != HAL_OK) {
    jh_lora_radio_context_unlock(first_context);
    return status;
  }
  jh_mock_lora_state_t *first_state = find_state(first_context);
  jh_mock_lora_state_t *second_state = find_state(second_context);
  if (first_state == NULL || second_state == NULL) {
    status = HAL_EUNINIT;
  } else {
    first_state->peer = second_context;
    second_state->peer = first_context;
  }
  jh_lora_radio_context_unlock(second_context);
  jh_lora_radio_context_unlock(first_context);
  return status;
}

void hal_mock_lora_reset(void) {
  s_next_initialize_status = HAL_OK;
  bool active = false;
  for (size_t index = 0u; index < HAL_LORA_RADIO_MAX_INSTANCES; ++index) {
    active = active || s_states[index].context != NULL;
  }
  if (!active) {
    memset(s_states, 0, sizeof(s_states));
  }
}

#endif
