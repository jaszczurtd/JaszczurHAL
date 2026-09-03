#include "hal/radio/hal_lora_link.h"

#ifdef HAL_ENABLE_LORA_LINK

#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/radio/jh_lora_link_frame.h"
#include "hal/security/hal_crc.h"
#include "hal/system/hal_system.h"

#include <string.h>

#define JH_LORA_LINK_HANDLE_KIND 9u
#define JH_LORA_LINK_MAX_FRAGMENTS 32u

typedef struct {
  uint16_t source;
  uint32_t session_id;
  uint32_t newest_sequence;
  uint32_t received_window;
  uint32_t last_seen_ms;
  bool valid;
} jh_lora_link_peer_t;

typedef struct {
  uint16_t source;
  uint16_t destination;
  uint32_t session_id;
  uint32_t sequence;
  uint32_t integrity;
  uint32_t received_fragments;
  uint32_t started_ms;
  uint16_t message_length;
  uint8_t port;
  uint8_t fragment_count;
  bool acknowledged;
  bool encrypted;
  bool active;
} jh_lora_link_reassembly_t;

typedef struct {
  hal_lora_link_config_t config;
  hal_mutex_t mutex;
  hal_lora_link_t handle;
  hal_lora_link_state_t state;
  hal_lora_link_send_status_t send_status;
  hal_lora_link_diagnostics_t diagnostics;
  uint8_t key[HAL_LORA_LINK_CRYPTO_KEY_BYTES];

  uint8_t tx_message[HAL_LORA_LINK_MAX_MESSAGE_SIZE];
  size_t tx_length;
  uint16_t tx_destination;
  uint32_t tx_sequence;
  uint32_t tx_integrity;
  uint32_t acknowledgement_started_ms;
  uint32_t retry_started_ms;
  uint8_t tx_port;
  uint8_t tx_fragment_index;
  uint8_t tx_fragment_count;
  uint8_t retries_completed;
  bool tx_acknowledged;
  bool tx_active;
  bool sequence_exhausted;

  uint8_t rx_message[HAL_LORA_LINK_MAX_MESSAGE_SIZE];
  size_t rx_length;
  hal_lora_link_message_info_t rx_info;
  bool rx_ready;

  jh_lora_link_reassembly_t reassembly;
  jh_lora_link_peer_t peers[HAL_LORA_LINK_MAX_PEERS];
  uint8_t radio_frame[HAL_LORA_RADIO_MAX_PAYLOAD];
  uint8_t frame_payload[JH_LORA_LINK_FRAME_MAX_PLAINTEXT];
  bool allocated;
} jh_lora_link_context_t;

static jh_lora_link_context_t s_contexts[HAL_LORA_LINK_MAX_INSTANCES] = {};
static jh_handle_slot_t s_handle_slots[HAL_LORA_LINK_MAX_INSTANCES] = {};
static jh_handle_pool_t s_handle_pool = {};
static hal_mutex_t s_pool_mutex = NULL;
static bool s_pool_initialized = false;

static hal_status_t pool_lock(void) {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_pool_mutex);
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_pool_initialized) {
    const hal_status_t status = jh_handle_pool_init(
        &s_handle_pool, s_handle_slots, HAL_LORA_LINK_MAX_INSTANCES,
        JH_LORA_LINK_HANDLE_KIND);
    if (status != HAL_OK) {
      hal_mutex_unlock(mutex);
      return status;
    }
    s_pool_initialized = true;
  }
  return HAL_OK;
}

static void pool_unlock(void) { hal_mutex_unlock(s_pool_mutex); }

static void secure_clear(uint8_t *data, size_t length) {
  volatile uint8_t *cursor = data;
  while (length-- > 0u) {
    *cursor++ = 0u;
  }
}

static void clear_context(jh_lora_link_context_t *context) {
  hal_mutex_t mutex = context->mutex;
  secure_clear(context->key, sizeof(context->key));
  memset(context, 0, sizeof(*context));
  context->mutex = mutex;
  context->state = HAL_LORA_LINK_STATE_ERROR;
  context->send_status.state = HAL_LORA_OPERATION_IDLE;
  context->send_status.result = HAL_NONE;
  context->diagnostics.last_error = HAL_NONE;
}

static hal_status_t context_lock(hal_lora_link_t link,
                                 jh_lora_link_context_t **out_context) {
  if (out_context == NULL) {
    return HAL_EINVAL;
  }
  *out_context = NULL;
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *token = NULL;
  status = jh_handle_resolve(&s_handle_pool, link, &token, NULL);
  pool_unlock();
  if (status != HAL_OK || token == NULL) {
    return HAL_EUNINIT;
  }
  jh_lora_link_context_t *context =
      static_cast<jh_lora_link_context_t *>(token);
  hal_mutex_lock(context->mutex);
  status = pool_lock();
  if (status != HAL_OK) {
    hal_mutex_unlock(context->mutex);
    return status;
  }
  void *verified = NULL;
  status = jh_handle_resolve(&s_handle_pool, link, &verified, NULL);
  pool_unlock();
  if (status != HAL_OK || verified != context || !context->allocated) {
    hal_mutex_unlock(context->mutex);
    return HAL_EUNINIT;
  }
  *out_context = context;
  return HAL_OK;
}

static void context_unlock(jh_lora_link_context_t *context) {
  hal_mutex_unlock(context->mutex);
}

static void record_error(jh_lora_link_context_t *context, hal_status_t status) {
  if (status == HAL_OK || status == HAL_EAGAIN || status == HAL_EOVERFLOW ||
      status == HAL_IGNORED) {
    return;
  }
  context->diagnostics.last_error = status;
  if (status != HAL_ETIMEOUT && status != HAL_ECANCELED &&
      status != HAL_EPROTO && status != HAL_EAUTH) {
    ++context->diagnostics.operation_errors;
  }
}

static const uint8_t *context_key(const jh_lora_link_context_t *context) {
  return context->config.security == HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305
             ? context->key
             : NULL;
}

static bool context_uses_crypto(const jh_lora_link_context_t *context) {
  return context->config.security == HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305;
}

static hal_status_t validate_config(const hal_lora_link_config_t *config) {
  if (config == NULL || config->radio == NULL ||
      config->local_address == HAL_LORA_LINK_ADDRESS_NONE ||
      config->local_address == HAL_LORA_LINK_ADDRESS_BROADCAST ||
      config->session_id == 0u || config->acknowledgement_timeout_ms == 0u ||
      config->reassembly_timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  if (config->security == HAL_LORA_LINK_SECURITY_NONE) {
    return config->key == NULL && config->key_length == 0u ? HAL_OK
                                                           : HAL_EINVAL;
  }
  if (config->security != HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305 ||
      config->key == NULL ||
      config->key_length != HAL_LORA_LINK_CRYPTO_KEY_BYTES) {
    return HAL_EINVAL;
  }
#ifdef HAL_ENABLE_CRYPTO
  return HAL_OK;
#else
  return HAL_EUNSUPPORTED;
#endif
}

hal_lora_link_config_t hal_lora_link_config_defaults(hal_lora_radio_t radio,
                                                     uint16_t local_address,
                                                     uint32_t session_id) {
  hal_lora_link_config_t config = {};
  config.radio = radio;
  config.local_address = local_address;
  config.session_id = session_id;
  config.initial_sequence = 1u;
  config.acknowledgement_timeout_ms = 1500u;
  config.retry_backoff_ms = 200u;
  config.reassembly_timeout_ms = 5000u;
  config.max_retries = 3u;
  config.security = HAL_LORA_LINK_SECURITY_NONE;
  return config;
}

static hal_status_t allocate_context(const hal_lora_link_config_t *config,
                                     hal_lora_link_t *out_link,
                                     jh_lora_link_context_t **out_context) {
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_link_context_t *context = NULL;
  for (size_t index = 0u; index < HAL_LORA_LINK_MAX_INSTANCES; ++index) {
    if (!s_contexts[index].allocated) {
      context = &s_contexts[index];
      break;
    }
  }
  if (context == NULL) {
    pool_unlock();
    return HAL_ENOMEM;
  }
  if (jh_hal_mutex_create_once(&context->mutex) == NULL) {
    pool_unlock();
    return HAL_ENOMEM;
  }
  clear_context(context);
  context->allocated = true;
  context->config = *config;
  context->config.key = NULL;
  if (config->security == HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305) {
    memcpy(context->key, config->key, sizeof(context->key));
  }
  void *handle = NULL;
  status = jh_handle_allocate(&s_handle_pool, context, &handle);
  if (status != HAL_OK) {
    clear_context(context);
    pool_unlock();
    return status;
  }
  pool_unlock();
  hal_mutex_lock(context->mutex);
  *out_link = reinterpret_cast<hal_lora_link_t>(handle);
  context->handle = *out_link;
  *out_context = context;
  return HAL_OK;
}

static void abandon_context(hal_lora_link_t link,
                            jh_lora_link_context_t *context) {
  void *released = NULL;
  if (pool_lock() == HAL_OK) {
    (void)jh_handle_release(&s_handle_pool, link, &released);
    clear_context(context);
    pool_unlock();
  }
  context_unlock(context);
}

static hal_status_t start_receive(jh_lora_link_context_t *context) {
  hal_lora_radio_state_t radio_state = HAL_LORA_RADIO_STATE_ERROR;
  hal_status_t status =
      hal_lora_radio_get_state(context->config.radio, &radio_state);
  if (status == HAL_OK && radio_state == HAL_LORA_RADIO_STATE_ERROR) {
    status = hal_lora_radio_standby(context->config.radio);
  }
  if (status == HAL_OK) {
    status = hal_lora_radio_receive_start_continuous(context->config.radio);
  }
  if (status == HAL_OK) {
    context->state = HAL_LORA_LINK_STATE_RECEIVING;
  } else {
    context->state = HAL_LORA_LINK_STATE_ERROR;
  }
  return status;
}

static hal_status_t stop_radio_operation(jh_lora_link_context_t *context) {
  hal_lora_radio_state_t radio_state = HAL_LORA_RADIO_STATE_ERROR;
  hal_status_t status =
      hal_lora_radio_get_state(context->config.radio, &radio_state);
  if (status != HAL_OK) {
    return status;
  }
  if (radio_state == HAL_LORA_RADIO_STATE_RX ||
      radio_state == HAL_LORA_RADIO_STATE_TX ||
      radio_state == HAL_LORA_RADIO_STATE_CAD) {
    status = hal_lora_radio_cancel(context->config.radio);
  } else if (radio_state != HAL_LORA_RADIO_STATE_STANDBY) {
    status = hal_lora_radio_standby(context->config.radio);
  }
  return status;
}

hal_status_t hal_lora_link_create(const hal_lora_link_config_t *config,
                                  hal_lora_link_t *out_link) {
  if (out_link != NULL) {
    *out_link = NULL;
  }
  if (out_link == NULL) {
    return HAL_EINVAL;
  }
  hal_status_t status = validate_config(config);
  if (status != HAL_OK) {
    return status;
  }
  hal_lora_radio_state_t radio_state = HAL_LORA_RADIO_STATE_ERROR;
  status = hal_lora_radio_get_state(config->radio, &radio_state);
  if (status != HAL_OK) {
    return status;
  }
  if (radio_state != HAL_LORA_RADIO_STATE_STANDBY) {
    return HAL_EBUSY;
  }
  hal_lora_radio_capabilities_t capabilities = {};
  status = hal_lora_radio_get_capabilities(config->radio, &capabilities);
  const size_t required_payload =
      JH_LORA_LINK_FRAME_HEADER_SIZE + 1u +
      (config->security == HAL_LORA_LINK_SECURITY_NONE
           ? 0u
           : JH_LORA_LINK_FRAME_TAG_SIZE);
  if (status != HAL_OK || !capabilities.supports_continuous_receive ||
      capabilities.max_payload_length < required_payload) {
    return status != HAL_OK ? status : HAL_EUNSUPPORTED;
  }

  jh_lora_link_context_t *context = NULL;
  status = allocate_context(config, out_link, &context);
  if (status != HAL_OK) {
    return status;
  }
  status = hal_lora_radio_set_event_callback(config->radio, NULL, NULL);
  if (status == HAL_OK) {
    status = start_receive(context);
  }
  if (status != HAL_OK) {
    hal_lora_link_t failed = *out_link;
    *out_link = NULL;
    abandon_context(failed, context);
    return status;
  }
  context_unlock(context);
  return HAL_OK;
}

static hal_status_t release_context(hal_lora_link_t link,
                                    jh_lora_link_context_t *context) {
  void *released = NULL;
  hal_status_t status = pool_lock();
  if (status == HAL_OK) {
    status = jh_handle_release(&s_handle_pool, link, &released);
    pool_unlock();
  }
  if (status != HAL_OK || released != context) {
    return status == HAL_OK ? HAL_EINTERNAL : status;
  }
  status = pool_lock();
  if (status == HAL_OK) {
    clear_context(context);
    pool_unlock();
  }
  return status;
}

hal_status_t hal_lora_link_destroy(hal_lora_link_t link) {
  jh_lora_link_context_t *context = NULL;
  hal_status_t status = context_lock(link, &context);
  if (status != HAL_OK) {
    return status;
  }
  const hal_status_t radio_status = stop_radio_operation(context);
  if (radio_status != HAL_OK) {
    context_unlock(context);
    return radio_status;
  }
  status = hal_lora_radio_set_event_callback(context->config.radio, NULL, NULL);
  if (status == HAL_OK) {
    status = release_context(link, context);
  }
  context_unlock(context);
  return status;
}

static uint8_t fragment_count_for(const jh_lora_link_context_t *context,
                                  size_t length) {
  const size_t capacity =
      jh_lora_link_frame_payload_capacity(context_uses_crypto(context));
  return (uint8_t)((length + capacity - 1u) / capacity);
}

static hal_status_t start_data_fragment(jh_lora_link_context_t *context) {
  const bool encrypted = context_uses_crypto(context);
  const size_t payload_capacity =
      jh_lora_link_frame_payload_capacity(encrypted);
  const size_t offset = (size_t)context->tx_fragment_index * payload_capacity;
  const size_t remaining = context->tx_length - offset;
  const size_t payload_length =
      remaining < payload_capacity ? remaining : payload_capacity;
  jh_lora_link_frame_header_t header = {};
  header.flags = encrypted ? JH_LORA_LINK_FRAME_FLAG_ENCRYPTED : 0u;
  if (context->tx_acknowledged) {
    header.flags |= JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST;
  }
  header.port = context->tx_port;
  header.source = context->config.local_address;
  header.destination = context->tx_destination;
  header.session_id = context->config.session_id;
  header.sequence = context->tx_sequence;
  header.fragment_index = context->tx_fragment_index;
  header.fragment_count = context->tx_fragment_count;
  header.message_length = (uint16_t)context->tx_length;
  header.integrity = context->tx_integrity;

  size_t frame_length = 0u;
  hal_status_t status = jh_lora_link_frame_encode(
      &header, &context->tx_message[offset], payload_length,
      context_key(context), context->radio_frame, sizeof(context->radio_frame),
      &frame_length);
  if (status == HAL_OK) {
    status = hal_lora_radio_transmit_start(context->config.radio,
                                           context->radio_frame, frame_length);
  }
  if (status == HAL_OK) {
    context->state = HAL_LORA_LINK_STATE_TRANSMITTING;
  }
  return status;
}

static hal_status_t finish_send(jh_lora_link_context_t *context,
                                hal_status_t result) {
  context->tx_active = false;
  context->send_status.result = result;
  if (result == HAL_OK) {
    context->send_status.state = HAL_LORA_OPERATION_SUCCEEDED;
    ++context->diagnostics.transmitted_messages;
    context->diagnostics.last_transmitted_sequence = context->tx_sequence;
    context->diagnostics.last_destination = context->tx_destination;
  } else if (result == HAL_ETIMEOUT) {
    context->send_status.state = HAL_LORA_OPERATION_TIMED_OUT;
    ++context->diagnostics.send_timeouts;
  } else if (result == HAL_ECANCELED) {
    context->send_status.state = HAL_LORA_OPERATION_CANCELLED;
    ++context->diagnostics.cancelled_sends;
  } else {
    context->send_status.state = HAL_LORA_OPERATION_FAILED;
  }
  const hal_status_t receive_status = start_receive(context);
  return receive_status == HAL_OK ? result : receive_status;
}

hal_status_t hal_lora_link_send_start(hal_lora_link_t link,
                                      uint16_t destination, uint8_t port,
                                      const uint8_t *data, size_t length,
                                      bool acknowledged) {
  if (destination == HAL_LORA_LINK_ADDRESS_NONE ||
      (destination == HAL_LORA_LINK_ADDRESS_BROADCAST && acknowledged) ||
      data == NULL || length == 0u || length > HAL_LORA_LINK_MAX_MESSAGE_SIZE ||
      length > UINT16_MAX) {
    return HAL_EINVAL;
  }
  jh_lora_link_context_t *context = NULL;
  hal_status_t status = context_lock(link, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (context->state != HAL_LORA_LINK_STATE_RECEIVING || context->tx_active) {
    context_unlock(context);
    return HAL_EBUSY;
  }
  if (context->sequence_exhausted) {
    context_unlock(context);
    return HAL_EOVERFLOW;
  }
  const uint8_t fragment_count = fragment_count_for(context, length);
  if (fragment_count == 0u || fragment_count > JH_LORA_LINK_MAX_FRAGMENTS) {
    context_unlock(context);
    return HAL_EOVERFLOW;
  }
  status = stop_radio_operation(context);
  if (status != HAL_OK) {
    record_error(context, status);
    context_unlock(context);
    return status;
  }

  memcpy(context->tx_message, data, length);
  context->tx_length = length;
  context->tx_destination = destination;
  context->tx_port = port;
  context->tx_sequence = context->config.initial_sequence;
  context->config.initial_sequence++;
  if (context_uses_crypto(context) && context->tx_sequence == UINT32_MAX) {
    context->sequence_exhausted = true;
  }
  context->tx_integrity =
      context_uses_crypto(context) ? 0u : hal_crc32(data, length);
  context->tx_fragment_index = 0u;
  context->tx_fragment_count = fragment_count;
  context->retries_completed = 0u;
  context->tx_acknowledged = acknowledged;
  context->tx_active = true;
  context->send_status.state = HAL_LORA_OPERATION_IN_PROGRESS;
  context->send_status.result = HAL_EAGAIN;
  context->send_status.sequence = context->tx_sequence;
  context->send_status.attempts = 1u;
  context->send_status.fragment_count = fragment_count;
  status = start_data_fragment(context);
  if (status != HAL_OK) {
    status = finish_send(context, status);
    record_error(context, status);
  }
  context_unlock(context);
  return status;
}

static hal_status_t start_ack_wait(jh_lora_link_context_t *context) {
  const hal_status_t status = hal_lora_radio_receive_start(
      context->config.radio, context->config.acknowledgement_timeout_ms);
  if (status == HAL_OK) {
    context->acknowledgement_started_ms = hal_millis();
    context->state = HAL_LORA_LINK_STATE_WAITING_ACKNOWLEDGEMENT;
  }
  return status;
}

static hal_status_t restart_ack_wait(jh_lora_link_context_t *context) {
  const uint32_t now = hal_millis();
  if (hal_elapsed_u32(now, context->acknowledgement_started_ms,
                      context->config.acknowledgement_timeout_ms)) {
    return HAL_ETIMEOUT;
  }
  const uint32_t elapsed = now - context->acknowledgement_started_ms;
  return hal_lora_radio_receive_start(
      context->config.radio,
      context->config.acknowledgement_timeout_ms - elapsed);
}

static bool peer_sequence_seen(const jh_lora_link_peer_t *peer,
                               uint32_t sequence) {
  if (!peer->valid) {
    return false;
  }
  const uint32_t newer_delta = sequence - peer->newest_sequence;
  if (newer_delta != 0u && newer_delta < UINT32_C(0x80000000)) {
    return false;
  }
  const uint32_t older_delta = peer->newest_sequence - sequence;
  return older_delta >= 32u ||
         (peer->received_window & (UINT32_C(1) << older_delta)) != 0u;
}

static jh_lora_link_peer_t *find_peer(jh_lora_link_context_t *context,
                                      uint16_t source, uint32_t session_id,
                                      bool allocate) {
  jh_lora_link_peer_t *oldest = &context->peers[0];
  for (size_t index = 0u; index < HAL_LORA_LINK_MAX_PEERS; ++index) {
    jh_lora_link_peer_t *peer = &context->peers[index];
    if (peer->valid && peer->source == source &&
        peer->session_id == session_id) {
      return peer;
    }
    if (!peer->valid) {
      oldest = peer;
      if (allocate) {
        break;
      }
    } else if ((uint32_t)(hal_millis() - peer->last_seen_ms) >
               (uint32_t)(hal_millis() - oldest->last_seen_ms)) {
      oldest = peer;
    }
  }
  if (!allocate) {
    return NULL;
  }
  memset(oldest, 0, sizeof(*oldest));
  oldest->source = source;
  oldest->session_id = session_id;
  oldest->valid = true;
  return oldest;
}

static void mark_peer_sequence(jh_lora_link_peer_t *peer, uint32_t sequence) {
  if (peer->received_window == 0u) {
    peer->newest_sequence = sequence;
    peer->received_window = 1u;
  } else {
    const uint32_t newer_delta = sequence - peer->newest_sequence;
    if (newer_delta != 0u && newer_delta < UINT32_C(0x80000000)) {
      peer->received_window =
          newer_delta >= 32u ? 1u : (peer->received_window << newer_delta) | 1u;
      peer->newest_sequence = sequence;
    } else {
      const uint32_t older_delta = peer->newest_sequence - sequence;
      if (older_delta < 32u) {
        peer->received_window |= UINT32_C(1) << older_delta;
      }
    }
  }
  peer->last_seen_ms = hal_millis();
}

static bool same_reassembly(const jh_lora_link_reassembly_t *reassembly,
                            const jh_lora_link_frame_header_t *header,
                            bool encrypted) {
  return reassembly->active && reassembly->source == header->source &&
         reassembly->destination == header->destination &&
         reassembly->session_id == header->session_id &&
         reassembly->sequence == header->sequence &&
         reassembly->integrity == header->integrity &&
         reassembly->message_length == header->message_length &&
         reassembly->port == header->port &&
         reassembly->fragment_count == header->fragment_count &&
         reassembly->acknowledged ==
             ((header->flags & JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST) != 0u) &&
         reassembly->encrypted == encrypted;
}

static void begin_reassembly(jh_lora_link_context_t *context,
                             const jh_lora_link_frame_header_t *header,
                             bool encrypted) {
  context->reassembly = {};
  context->reassembly.source = header->source;
  context->reassembly.destination = header->destination;
  context->reassembly.session_id = header->session_id;
  context->reassembly.sequence = header->sequence;
  context->reassembly.integrity = header->integrity;
  context->reassembly.message_length = header->message_length;
  context->reassembly.port = header->port;
  context->reassembly.fragment_count = header->fragment_count;
  context->reassembly.acknowledged =
      (header->flags & JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST) != 0u;
  context->reassembly.encrypted = encrypted;
  context->reassembly.started_ms = hal_millis();
  context->reassembly.active = true;
}

static uint32_t fragment_mask(uint8_t fragment_count) {
  return fragment_count == 32u ? UINT32_MAX
                               : (UINT32_C(1) << fragment_count) - UINT32_C(1);
}

static hal_status_t
start_acknowledgement(jh_lora_link_context_t *context,
                      const jh_lora_link_frame_header_t *received) {
  hal_status_t status = stop_radio_operation(context);
  if (status != HAL_OK) {
    return status;
  }
  jh_lora_link_frame_header_t acknowledgement = {};
  acknowledgement.flags = JH_LORA_LINK_FRAME_FLAG_ACK;
  if (context_uses_crypto(context)) {
    acknowledgement.flags |= JH_LORA_LINK_FRAME_FLAG_ENCRYPTED;
  }
  acknowledgement.source = context->config.local_address;
  acknowledgement.destination = received->source;
  acknowledgement.session_id = context->config.session_id;
  acknowledgement.sequence = received->sequence;
  acknowledgement.integrity = received->session_id;
  size_t frame_length = 0u;
  status = jh_lora_link_frame_encode(
      &acknowledgement, NULL, 0u, context_key(context), context->radio_frame,
      sizeof(context->radio_frame), &frame_length);
  if (status == HAL_OK) {
    status = hal_lora_radio_transmit_start(context->config.radio,
                                           context->radio_frame, frame_length);
  }
  if (status == HAL_OK) {
    context->state = HAL_LORA_LINK_STATE_SENDING_ACKNOWLEDGEMENT;
  } else {
    const hal_status_t receive_status = start_receive(context);
    if (receive_status != HAL_OK) {
      status = receive_status;
    }
  }
  return status;
}

static hal_status_t
accept_data_frame(jh_lora_link_context_t *context,
                  const jh_lora_link_frame_header_t *header,
                  const uint8_t *payload, size_t payload_length,
                  const hal_lora_packet_info_t *packet_info) {
  const bool encrypted =
      (header->flags & JH_LORA_LINK_FRAME_FLAG_ENCRYPTED) != 0u;
  jh_lora_link_peer_t *peer =
      find_peer(context, header->source, header->session_id, false);
  if (peer != NULL && peer_sequence_seen(peer, header->sequence)) {
    peer->last_seen_ms = hal_millis();
    if (header->fragment_index == 0u) {
      ++context->diagnostics.duplicate_messages;
    } else {
      ++context->diagnostics.duplicate_fragments;
    }
    if (header->fragment_index + 1u == header->fragment_count &&
        (header->flags & JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST) != 0u) {
      return start_acknowledgement(context, header);
    }
    return HAL_OK;
  }
  if (context->rx_ready) {
    ++context->diagnostics.dropped_messages;
    return HAL_EBUSY;
  }
  if (header->message_length > HAL_LORA_LINK_MAX_MESSAGE_SIZE) {
    ++context->diagnostics.malformed_frames;
    return HAL_EOVERFLOW;
  }
  if (!same_reassembly(&context->reassembly, header, encrypted)) {
    if (context->reassembly.active && header->fragment_index != 0u) {
      ++context->diagnostics.reassembly_drops;
      return HAL_EAGAIN;
    }
    if (context->reassembly.active) {
      ++context->diagnostics.reassembly_drops;
    }
    begin_reassembly(context, header, encrypted);
  }
  const uint32_t bit = UINT32_C(1) << header->fragment_index;
  if ((context->reassembly.received_fragments & bit) != 0u) {
    ++context->diagnostics.duplicate_fragments;
    return HAL_OK;
  }
  const size_t fragment_capacity =
      jh_lora_link_frame_payload_capacity(encrypted);
  const size_t offset = (size_t)header->fragment_index * fragment_capacity;
  memcpy(&context->rx_message[offset], payload, payload_length);
  context->reassembly.received_fragments |= bit;
  if (context->reassembly.received_fragments !=
      fragment_mask(header->fragment_count)) {
    return HAL_OK;
  }
  if (!encrypted && hal_crc32(context->rx_message, header->message_length) !=
                        header->integrity) {
    ++context->diagnostics.integrity_failures;
    context->reassembly.active = false;
    return HAL_EPROTO;
  }
  peer = find_peer(context, header->source, header->session_id, true);
  mark_peer_sequence(peer, header->sequence);
  context->rx_length = header->message_length;
  context->rx_info.source = header->source;
  context->rx_info.destination = header->destination;
  context->rx_info.session_id = header->session_id;
  context->rx_info.sequence = header->sequence;
  context->rx_info.port = header->port;
  context->rx_info.fragment_count = header->fragment_count;
  context->rx_info.encrypted = encrypted;
  context->rx_info.packet = *packet_info;
  context->rx_ready = true;
  context->reassembly.active = false;
  ++context->diagnostics.received_messages;
  context->diagnostics.last_received_sequence = header->sequence;
  context->diagnostics.last_source = header->source;
  context->diagnostics.last_rssi_dbm = packet_info->rssi_dbm;
  context->diagnostics.last_snr_db = packet_info->snr_db;
  if ((header->flags & JH_LORA_LINK_FRAME_FLAG_ACK_REQUEST) != 0u &&
      header->fragment_index + 1u == header->fragment_count) {
    return start_acknowledgement(context, header);
  }
  return HAL_OK;
}

static hal_status_t
decode_received_frame(jh_lora_link_context_t *context, size_t frame_length,
                      jh_lora_link_frame_header_t *out_header,
                      size_t *out_payload_length) {
  hal_status_t status = jh_lora_link_frame_decode(
      context->radio_frame, frame_length, context_key(context), out_header,
      context->frame_payload, sizeof(context->frame_payload),
      out_payload_length);
  if (status == HAL_EAUTH) {
    ++context->diagnostics.authentication_failures;
  } else if (status != HAL_OK) {
    ++context->diagnostics.malformed_frames;
  }
  if (status != HAL_OK) {
    return status;
  }
  const bool encrypted =
      (out_header->flags & JH_LORA_LINK_FRAME_FLAG_ENCRYPTED) != 0u;
  if (encrypted != context_uses_crypto(context) ||
      (encrypted && (out_header->flags & JH_LORA_LINK_FRAME_FLAG_ACK) == 0u &&
       out_header->integrity != 0u)) {
    ++context->diagnostics.authentication_failures;
    return HAL_EAUTH;
  }
  ++context->diagnostics.received_frames;
  return HAL_OK;
}

static bool acknowledgement_matches(const jh_lora_link_context_t *context,
                                    const jh_lora_link_frame_header_t *header) {
  return (header->flags & JH_LORA_LINK_FRAME_FLAG_ACK) != 0u &&
         header->source == context->tx_destination &&
         header->destination == context->config.local_address &&
         header->sequence == context->tx_sequence &&
         header->integrity == context->config.session_id;
}

static hal_status_t schedule_retry(jh_lora_link_context_t *context) {
  if (context->retries_completed >= context->config.max_retries) {
    return finish_send(context, HAL_ETIMEOUT);
  }
  ++context->retries_completed;
  ++context->send_status.attempts;
  ++context->diagnostics.retransmissions;
  context->tx_fragment_index = 0u;
  context->retry_started_ms = hal_millis();
  context->state = HAL_LORA_LINK_STATE_RETRY_WAIT;
  if (context->config.retry_backoff_ms == 0u) {
    const hal_status_t status = start_data_fragment(context);
    return status == HAL_OK ? HAL_OK : finish_send(context, status);
  }
  return HAL_OK;
}

static hal_status_t finish_acknowledgement(jh_lora_link_context_t *context,
                                           hal_status_t result) {
  if (result == HAL_OK) {
    ++context->diagnostics.acknowledgements_sent;
  }
  const hal_status_t receive_status = start_receive(context);
  return receive_status == HAL_OK ? result : receive_status;
}

static hal_status_t process_transmit(jh_lora_link_context_t *context) {
  const bool acknowledgement =
      context->state == HAL_LORA_LINK_STATE_SENDING_ACKNOWLEDGEMENT;
  (void)hal_lora_radio_process(context->config.radio);
  hal_lora_operation_status_t radio_status = {};
  hal_status_t status =
      hal_lora_radio_get_tx_status(context->config.radio, &radio_status);
  if (status != HAL_OK) {
    return acknowledgement ? finish_acknowledgement(context, status)
                           : finish_send(context, status);
  }
  if (radio_status.state == HAL_LORA_OPERATION_IN_PROGRESS) {
    return HAL_EAGAIN;
  }
  if (radio_status.result != HAL_OK) {
    return acknowledgement
               ? finish_acknowledgement(context, radio_status.result)
               : finish_send(context, radio_status.result);
  }
  ++context->diagnostics.transmitted_frames;
  if (acknowledgement) {
    return finish_acknowledgement(context, HAL_OK);
  }
  if (context->tx_fragment_index + 1u < context->tx_fragment_count) {
    ++context->tx_fragment_index;
    status = start_data_fragment(context);
    return status == HAL_OK ? HAL_OK : finish_send(context, status);
  }
  if (!context->tx_acknowledged) {
    return finish_send(context, HAL_OK);
  }
  status = start_ack_wait(context);
  return status == HAL_OK ? HAL_OK : finish_send(context, status);
}

static hal_status_t process_ack_wait(jh_lora_link_context_t *context) {
  const hal_status_t process_status =
      hal_lora_radio_process(context->config.radio);
  size_t frame_length = 0u;
  hal_lora_packet_info_t packet_info = {};
  hal_status_t status = hal_lora_radio_receive(
      context->config.radio, context->radio_frame, sizeof(context->radio_frame),
      &frame_length, &packet_info);
  if (status == HAL_EAGAIN && process_status == HAL_ETIMEOUT) {
    status = HAL_ETIMEOUT;
  }
  if (status == HAL_EAGAIN) {
    return HAL_EAGAIN;
  }
  if (status == HAL_ETIMEOUT) {
    return schedule_retry(context);
  }
  if (status == HAL_EPROTO) {
    ++context->diagnostics.malformed_frames;
    status = restart_ack_wait(context);
    if (status == HAL_ETIMEOUT) {
      return schedule_retry(context);
    }
    return status == HAL_OK ? HAL_EAGAIN : finish_send(context, status);
  }
  if (status != HAL_OK) {
    return finish_send(context, status);
  }
  jh_lora_link_frame_header_t header = {};
  size_t payload_length = 0u;
  status =
      decode_received_frame(context, frame_length, &header, &payload_length);
  if (status == HAL_OK && payload_length == 0u &&
      acknowledgement_matches(context, &header)) {
    ++context->diagnostics.acknowledgements_received;
    return finish_send(context, HAL_OK);
  }
  status = restart_ack_wait(context);
  if (status == HAL_ETIMEOUT) {
    return schedule_retry(context);
  }
  return status == HAL_OK ? HAL_EAGAIN : finish_send(context, status);
}

static hal_status_t process_receive(jh_lora_link_context_t *context) {
  const hal_status_t process_status =
      hal_lora_radio_process(context->config.radio);
  size_t frame_length = 0u;
  hal_lora_packet_info_t packet_info = {};
  hal_status_t status = hal_lora_radio_receive(
      context->config.radio, context->radio_frame, sizeof(context->radio_frame),
      &frame_length, &packet_info);
  if (status == HAL_EAGAIN) {
    if (process_status == HAL_EPROTO) {
      ++context->diagnostics.malformed_frames;
      return HAL_EPROTO;
    }
    return process_status == HAL_OK ? HAL_EAGAIN : process_status;
  }
  if (status != HAL_OK) {
    if (status == HAL_EPROTO) {
      ++context->diagnostics.malformed_frames;
    }
    return status;
  }
  jh_lora_link_frame_header_t header = {};
  size_t payload_length = 0u;
  status =
      decode_received_frame(context, frame_length, &header, &payload_length);
  if (status != HAL_OK) {
    return status;
  }
  if (header.destination != context->config.local_address &&
      header.destination != HAL_LORA_LINK_ADDRESS_BROADCAST) {
    return HAL_IGNORED;
  }
  if (header.source == context->config.local_address ||
      (header.flags & JH_LORA_LINK_FRAME_FLAG_ACK) != 0u) {
    return HAL_IGNORED;
  }
  return accept_data_frame(context, &header, context->frame_payload,
                           payload_length, &packet_info);
}

static void expire_reassembly(jh_lora_link_context_t *context) {
  if (context->reassembly.active &&
      hal_millis_deadline_expired(context->reassembly.started_ms,
                                  context->config.reassembly_timeout_ms)) {
    context->reassembly.active = false;
    ++context->diagnostics.reassembly_timeouts;
  }
}

hal_status_t hal_lora_link_process(hal_lora_link_t link) {
  jh_lora_link_context_t *context = NULL;
  hal_status_t status = context_lock(link, &context);
  if (status != HAL_OK) {
    return status;
  }
  expire_reassembly(context);
  switch (context->state) {
  case HAL_LORA_LINK_STATE_RECEIVING:
    status = process_receive(context);
    break;
  case HAL_LORA_LINK_STATE_TRANSMITTING:
  case HAL_LORA_LINK_STATE_SENDING_ACKNOWLEDGEMENT:
    status = process_transmit(context);
    break;
  case HAL_LORA_LINK_STATE_WAITING_ACKNOWLEDGEMENT:
    status = process_ack_wait(context);
    break;
  case HAL_LORA_LINK_STATE_RETRY_WAIT:
    if (hal_millis_deadline_expired(context->retry_started_ms,
                                    context->config.retry_backoff_ms)) {
      status = start_data_fragment(context);
      if (status != HAL_OK) {
        status = finish_send(context, status);
      }
    } else {
      status = HAL_EAGAIN;
    }
    break;
  case HAL_LORA_LINK_STATE_ERROR:
  default:
    status = HAL_ESTATE;
    break;
  }
  record_error(context, status);
  context_unlock(context);
  return status;
}

hal_status_t
hal_lora_link_get_send_status(hal_lora_link_t link,
                              hal_lora_link_send_status_t *out_status) {
  if (out_status == NULL) {
    return HAL_EINVAL;
  }
  jh_lora_link_context_t *context = NULL;
  const hal_status_t status = context_lock(link, &context);
  if (status != HAL_OK) {
    return status;
  }
  *out_status = context->send_status;
  context_unlock(context);
  return HAL_OK;
}

hal_status_t hal_lora_link_receive(hal_lora_link_t link, uint8_t *buffer,
                                   size_t buffer_size, size_t *out_length,
                                   hal_lora_link_message_info_t *out_info) {
  if (out_length == NULL || (buffer_size > 0u && buffer == NULL)) {
    return HAL_EINVAL;
  }
  *out_length = 0u;
  if (out_info != NULL) {
    memset(out_info, 0, sizeof(*out_info));
  }
  jh_lora_link_context_t *context = NULL;
  hal_status_t status = context_lock(link, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (!context->rx_ready) {
    context_unlock(context);
    return HAL_EAGAIN;
  }
  *out_length = context->rx_length;
  const size_t copy_length =
      context->rx_length < buffer_size ? context->rx_length : buffer_size;
  if (copy_length > 0u) {
    memcpy(buffer, context->rx_message, copy_length);
  }
  if (out_info != NULL) {
    *out_info = context->rx_info;
  }
  context->rx_ready = false;
  status = copy_length < context->rx_length ? HAL_EOVERFLOW : HAL_OK;
  context_unlock(context);
  return status;
}

hal_status_t hal_lora_link_cancel(hal_lora_link_t link) {
  jh_lora_link_context_t *context = NULL;
  hal_status_t status = context_lock(link, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (!context->tx_active) {
    context_unlock(context);
    return HAL_ESTATE;
  }
  status = stop_radio_operation(context);
  if (status == HAL_OK) {
    status = finish_send(context, HAL_ECANCELED);
    if (status == HAL_OK) {
      status = HAL_ECANCELED;
    }
  }
  record_error(context, status);
  context_unlock(context);
  return status;
}

hal_status_t hal_lora_link_get_state(hal_lora_link_t link,
                                     hal_lora_link_state_t *out_state) {
  if (out_state == NULL) {
    return HAL_EINVAL;
  }
  jh_lora_link_context_t *context = NULL;
  const hal_status_t status = context_lock(link, &context);
  if (status != HAL_OK) {
    return status;
  }
  *out_state = context->state;
  context_unlock(context);
  return HAL_OK;
}

hal_status_t
hal_lora_link_get_diagnostics(hal_lora_link_t link,
                              hal_lora_link_diagnostics_t *out_diagnostics) {
  if (out_diagnostics == NULL) {
    return HAL_EINVAL;
  }
  jh_lora_link_context_t *context = NULL;
  const hal_status_t status = context_lock(link, &context);
  if (status != HAL_OK) {
    return status;
  }
  *out_diagnostics = context->diagnostics;
  context_unlock(context);
  return HAL_OK;
}

#if HAL_TARGET_IS_MOCK
/* Test-only: force the pool mutex and every context mutex through a real
 * destroy so Helgrind/DRD can observe the teardown path, then mark the
 * pool uninitialized so the next link operation recreates them from
 * scratch. Firmware never calls this. Call only when no other thread is
 * using the link pool. */
void hal_mock_lora_link_full_reset(void) {
  for (size_t index = 0u; index < HAL_LORA_LINK_MAX_INSTANCES; ++index) {
    if (s_contexts[index].mutex != NULL) {
      hal_mutex_destroy(s_contexts[index].mutex);
    }
  }
  memset(s_contexts, 0, sizeof(s_contexts));
  if (s_pool_mutex != NULL) {
    hal_mutex_destroy(s_pool_mutex);
    s_pool_mutex = NULL;
  }
  s_pool_initialized = false;
}
#endif /* HAL_TARGET_IS_MOCK */

#endif /* HAL_ENABLE_LORA_LINK */
