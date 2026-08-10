#include "../../hal_config.h"

#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_LORA)

#include "../../hal_system.h"
#include "hal/impl/shared/radio/jh_lora_radio_internal.h"
#include "hal_mock.h"

#include <string.h>

typedef struct {
  jh_lora_radio_context_t *context;
  jh_lora_radio_context_t *peer;
  hal_status_t next_status[HAL_MOCK_LORA_STANDBY + 1u];
  uint8_t pending_rx[HAL_LORA_RADIO_MAX_PAYLOAD];
  size_t pending_rx_length;
  hal_lora_packet_info_t pending_info;
  bool pending_rx_ready;
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
  return allocate_state(context) != NULL ? HAL_OK : HAL_ENOMEM;
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
  return consume(find_state(context), HAL_MOCK_LORA_CONFIGURE);
}

static hal_status_t mock_transmit(jh_lora_radio_context_t *context,
                                  uint32_t timeout_ms) {
  (void)timeout_ms;
  jh_mock_lora_state_t *state = find_state(context);
  const hal_status_t status = consume(state, HAL_MOCK_LORA_TRANSMIT);
  if (status != HAL_OK || state->peer == NULL) {
    return status;
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

static hal_status_t mock_receive_poll(jh_lora_radio_context_t *context) {
  jh_mock_lora_state_t *state = find_state(context);
  hal_status_t status = consume(state, HAL_MOCK_LORA_RECEIVE_POLL);
  if (status != HAL_OK) {
    return status;
  }
  if (state->pending_rx_ready) {
    if (!state->pending_info.crc_valid) {
      state->pending_rx_ready = false;
      return HAL_EPROTO;
    }
    memcpy(context->rx_buffer, state->pending_rx, state->pending_rx_length);
    context->rx_length = state->pending_rx_length;
    context->rx_info = state->pending_info;
    context->rx_ready = true;
    state->pending_rx_ready = false;
    return HAL_OK;
  }
  if (!context->receive_continuous &&
      (uint32_t)(hal_millis() - context->receive_started_ms) >=
          context->receive_timeout_ms) {
    return HAL_ETIMEOUT;
  }
  return HAL_EAGAIN;
}

static hal_status_t mock_sleep(jh_lora_radio_context_t *context) {
  return consume(find_state(context), HAL_MOCK_LORA_SLEEP);
}

static hal_status_t mock_standby(jh_lora_radio_context_t *context) {
  return consume(find_state(context), HAL_MOCK_LORA_STANDBY);
}

static const jh_lora_radio_provider_ops_t s_mock_provider = {
    mock_initialize,    mock_deinitialize, mock_configure, mock_transmit,
    mock_receive_start, mock_receive_poll, mock_sleep,     mock_standby,
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
