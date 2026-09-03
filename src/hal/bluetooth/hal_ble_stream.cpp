#include "hal/bluetooth/hal_ble_stream.h"

#ifdef HAL_ENABLE_BLE_STREAM

#include "hal/bluetooth/jh_ble_backend.h"
#include "hal/bluetooth/jh_ble_stream_runtime.h"
#include "hal/bluetooth/jh_ble_stream_session.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/security/jh_secure_random.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include <string.h>

namespace {

struct stream_payload_t {
  uint8_t data[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t length;
  hal_ble_stream_payload_info_t info;
};

struct stream_runtime_t {
  hal_mutex_t mutex;
  const jh_ble_backend_t *backend;
  hal_ble_stream_state_t state;
  hal_status_t last_status;
  uint16_t capabilities;
  uint16_t negotiated_capabilities;
  uint32_t generation;
  uint32_t idle_timeout_ms;
  uint64_t tx_counter;
  uint64_t rx_counter;
  uint32_t auth_failures;
  uint32_t replay_rejections;
  uint32_t dropped_rx_frames;
  uint32_t dropped_tx_frames;
  uint16_t native_connection;
  jh_ble_stream_session_t session;
  uint32_t auth_attempts;
  uint32_t backoff_until_ms;
  uint32_t last_activity_ms;
  stream_payload_t rx[HAL_BLE_STREAM_RX_QUEUE_DEPTH];
  size_t rx_head;
  size_t rx_count;
  stream_payload_t tx[HAL_BLE_STREAM_TX_QUEUE_DEPTH];
  size_t tx_head;
  size_t tx_count;
  uint8_t control_frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t control_frame_length;
  uint16_t att_mtu;
  bool initialized;
  bool operation_active;
  bool subscribed;
  bool rx_overflow_pending;
  bool notification_pending;
};

stream_runtime_t s_stream{};

hal_mutex_t runtime_mutex(void) {
  return jh_hal_mutex_create_once(&s_stream.mutex);
}

/* Application payloads belong to exactly one authenticated session. */
void clear_payload_queues_locked(void) {
  jh_secure_zeroize(s_stream.rx, sizeof(s_stream.rx));
  jh_secure_zeroize(s_stream.tx, sizeof(s_stream.tx));
  s_stream.rx_head = 0u;
  s_stream.rx_count = 0u;
  s_stream.tx_head = 0u;
  s_stream.tx_count = 0u;
  s_stream.rx_overflow_pending = false;
  s_stream.tx_counter = 0u;
  s_stream.rx_counter = 0u;
}

/* Drop session state and key material without touching the provisioned
   secret. */
void close_session_locked(void) {
  jh_ble_stream_session_reset(&s_stream.session);
  clear_payload_queues_locked();
  jh_secure_zeroize(s_stream.control_frame, sizeof(s_stream.control_frame));
  s_stream.control_frame_length = 0u;
  s_stream.negotiated_capabilities = 0u;
  if (s_stream.state == HAL_BLE_STREAM_STATE_BACKOFF) {
    return;
  }
  s_stream.state = s_stream.subscribed ? HAL_BLE_STREAM_STATE_SUBSCRIBED
                                       : HAL_BLE_STREAM_STATE_IDLE;
}

/* Repeated failures cost the client a bounded backoff window. */
void register_auth_failure_locked(void) {
  ++s_stream.auth_failures;
  ++s_stream.auth_attempts;
  if (s_stream.auth_attempts >= HAL_BLE_STREAM_AUTH_ATTEMPT_LIMIT) {
    s_stream.backoff_until_ms = hal_millis() + HAL_BLE_STREAM_AUTH_BACKOFF_MS;
    s_stream.state = HAL_BLE_STREAM_STATE_BACKOFF;
  }
}

bool backoff_active_locked(void) {
  if (s_stream.state != HAL_BLE_STREAM_STATE_BACKOFF) {
    return false;
  }
  /* Unsigned wrap keeps the comparison valid across the millisecond
     rollover. */
  if ((int32_t)(hal_millis() - s_stream.backoff_until_ms) >= 0) {
    s_stream.auth_attempts = 0u;
    s_stream.state = s_stream.subscribed ? HAL_BLE_STREAM_STATE_SUBSCRIBED
                                         : HAL_BLE_STREAM_STATE_IDLE;
    return false;
  }
  return true;
}

uint64_t active_session_id_locked(void) {
  if (s_stream.state != HAL_BLE_STREAM_STATE_AUTHENTICATED) {
    return 0u;
  }
  uint64_t value = 0u;
  for (size_t index = 0u; index < HAL_BLE_STREAM_SESSION_ID_LEN; ++index) {
    value |= (uint64_t)s_stream.session.session_id[index] << (index * 8u);
  }
  return value;
}

bool active_session_matches_locked(uint32_t expected_generation,
                                   uint64_t expected_session_id) {
  return s_stream.state == HAL_BLE_STREAM_STATE_AUTHENTICATED &&
         s_stream.generation == expected_generation &&
         active_session_id_locked() == expected_session_id;
}

/* Push queued payloads until the controller reports backpressure. */
hal_status_t flush_tx_locked(void) {
  if (s_stream.notification_pending) {
    return HAL_EAGAIN;
  }
  if (s_stream.control_frame_length != 0u) {
    const hal_status_t sent = s_stream.backend->stream_notify(
        s_stream.backend->context, s_stream.native_connection,
        s_stream.control_frame, s_stream.control_frame_length);
    if (sent != HAL_OK) {
      if (sent != HAL_EAGAIN) {
        s_stream.last_status = sent;
        close_session_locked();
      }
      return sent;
    }
    jh_secure_zeroize(s_stream.control_frame, sizeof(s_stream.control_frame));
    s_stream.control_frame_length = 0u;
    s_stream.notification_pending = true;
    return HAL_OK;
  }

  if (s_stream.tx_count != 0u) {
    uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
    size_t frame_length = 0u;
    const stream_payload_t &payload = s_stream.tx[s_stream.tx_head];
    const hal_status_t built = jh_ble_stream_session_build_data(
        &s_stream.session, payload.data, payload.length, frame, sizeof(frame),
        &frame_length);
    if (built != HAL_OK) {
      jh_secure_zeroize(frame, sizeof(frame));
      s_stream.last_status = built;
      if (built == HAL_EOVERFLOW) {
        close_session_locked();
      }
      return built;
    }
    const uint64_t previous_counter = s_stream.tx_counter;
    const hal_status_t sent = s_stream.backend->stream_notify(
        s_stream.backend->context, s_stream.native_connection, frame,
        frame_length);
    jh_secure_zeroize(frame, sizeof(frame));
    if (sent == HAL_EAGAIN) {
      /* The backend did not accept the frame, so its nonce stays reusable. */
      s_stream.session.tx_counter = previous_counter;
      return HAL_EAGAIN;
    }
    if (sent != HAL_OK) {
      s_stream.last_status = sent;
      close_session_locked();
      return sent;
    }
    jh_secure_zeroize(&s_stream.tx[s_stream.tx_head], sizeof(stream_payload_t));
    s_stream.tx_head = (s_stream.tx_head + 1u) % HAL_BLE_STREAM_TX_QUEUE_DEPTH;
    --s_stream.tx_count;
    s_stream.tx_counter = s_stream.session.tx_counter;
    s_stream.notification_pending = true;
  }
  return HAL_OK;
}

hal_status_t stream_send(const void *data, size_t length,
                         bool require_expected_session,
                         uint32_t expected_generation,
                         uint64_t expected_session_id) {
  if (data == nullptr || length == 0u || length > HAL_BLE_STREAM_MAX_PAYLOAD) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_stream.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (!s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (require_expected_session
          ? !active_session_matches_locked(expected_generation,
                                           expected_session_id)
          : s_stream.state != HAL_BLE_STREAM_STATE_AUTHENTICATED) {
    hal_mutex_unlock(mutex);
    return HAL_EAUTH;
  }
  const size_t frame_length = HAL_BLE_STREAM_FRAME_HEADER_LEN +
                              HAL_BLE_STREAM_AEAD_COUNTER_LEN + length +
                              HAL_BLE_STREAM_AEAD_TAG_LEN;
  if (s_stream.att_mtu < HAL_BLE_STREAM_ATT_OVERHEAD ||
      frame_length > (size_t)(s_stream.att_mtu - HAL_BLE_STREAM_ATT_OVERHEAD)) {
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  if (s_stream.tx_count == HAL_BLE_STREAM_TX_QUEUE_DEPTH) {
    ++s_stream.dropped_tx_frames;
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  const size_t tail =
      (s_stream.tx_head + s_stream.tx_count) % HAL_BLE_STREAM_TX_QUEUE_DEPTH;
  memcpy(s_stream.tx[tail].data, data, length);
  s_stream.tx[tail].length = length;
  ++s_stream.tx_count;
  const hal_status_t flushed = flush_tx_locked();
  hal_mutex_unlock(mutex);
  return flushed == HAL_EAGAIN ? HAL_OK : flushed;
}

hal_status_t stream_receive(void *out, size_t capacity, size_t *out_length,
                            hal_ble_stream_payload_info_t *out_payload_info,
                            bool require_expected_session,
                            uint32_t expected_generation,
                            uint64_t expected_session_id) {
  if (out == nullptr || out_length == nullptr || capacity == 0u) {
    return HAL_EINVAL;
  }
  *out_length = 0u;
  if (out_payload_info != nullptr) {
    memset(out_payload_info, 0, sizeof(*out_payload_info));
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_stream.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (!s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (require_expected_session &&
      !active_session_matches_locked(expected_generation,
                                     expected_session_id)) {
    hal_mutex_unlock(mutex);
    return HAL_EAUTH;
  }
  if (s_stream.rx_overflow_pending) {
    s_stream.rx_overflow_pending = false;
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  if (s_stream.rx_count == 0u) {
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  const stream_payload_t &payload = s_stream.rx[s_stream.rx_head];
  if (require_expected_session &&
      (payload.info.generation != expected_generation ||
       payload.info.session_id != expected_session_id)) {
    hal_mutex_unlock(mutex);
    return HAL_EPROTO;
  }
  if (capacity < payload.length) {
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  memcpy(out, payload.data, payload.length);
  *out_length = payload.length;
  if (out_payload_info != nullptr) {
    *out_payload_info = payload.info;
  }
  jh_secure_zeroize(&s_stream.rx[s_stream.rx_head], sizeof(stream_payload_t));
  s_stream.rx_head = (s_stream.rx_head + 1u) % HAL_BLE_STREAM_RX_QUEUE_DEPTH;
  --s_stream.rx_count;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

} // namespace

hal_status_t hal_ble_stream_initialize(const hal_ble_stream_config_t *config) {
  if (config == nullptr) {
    return HAL_EINVAL;
  }
  hal_ble_info_t ble{};
  hal_status_t status = hal_ble_get_info(&ble);
  if (status != HAL_OK) {
    return status;
  }
  if (ble.state == HAL_BLE_STATE_UNINITIALIZED) {
    return HAL_EUNINIT;
  }
  if (ble.state == HAL_BLE_STATE_FAILED || ble.generation == 0u) {
    return HAL_ESTATE;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  const jh_ble_backend_t *backend = jh_ble_backend_instance();
  if (backend == nullptr || backend->stream_notify == nullptr ||
      backend->stream_discard_pending == nullptr ||
      backend->stream_publish == nullptr ||
      backend->stream_unpublish == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  hal_mutex_lock(mutex);
  if (s_stream.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (s_stream.initialized) {
    if (s_stream.generation != ble.generation) {
      s_stream.generation = ble.generation;
      s_stream.subscribed = false;
      s_stream.native_connection = 0u;
      s_stream.att_mtu = 0u;
      s_stream.notification_pending = false;
      close_session_locked();
    }
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }
  s_stream.operation_active = true;
  hal_mutex_unlock(mutex);

  hal_status_t published = backend->stream_publish(
      backend->context, HAL_BLE_STREAM_PROTOCOL_VERSION, config->capabilities);
  hal_mutex_lock(mutex);
  if (published == HAL_OK) {
    /* BLE forwards Stream callbacks only after releasing its own mutex. This
       nested snapshot therefore closes the publish-to-commit race safely. */
    hal_ble_info_t current_ble{};
    published = hal_ble_get_info(&current_ble);
    if (published == HAL_OK &&
        current_ble.state == HAL_BLE_STATE_UNINITIALIZED) {
      published = HAL_EUNINIT;
    } else if (published == HAL_OK &&
               (current_ble.state == HAL_BLE_STATE_FAILED ||
                current_ble.generation == 0u ||
                current_ble.generation != ble.generation)) {
      published = HAL_ESTATE;
    }
  }
  if (published == HAL_OK) {
    s_stream.backend = backend;
    s_stream.capabilities = config->capabilities;
    s_stream.idle_timeout_ms = config->idle_timeout_ms != 0u
                                   ? config->idle_timeout_ms
                                   : HAL_BLE_STREAM_SESSION_IDLE_TIMEOUT_MS;
    s_stream.state = HAL_BLE_STREAM_STATE_IDLE;
    s_stream.last_status = HAL_OK;
    s_stream.subscribed = false;
    s_stream.native_connection = 0u;
    s_stream.att_mtu = 0u;
    s_stream.generation = ble.generation;
    s_stream.initialized = true;
    s_stream.auth_attempts = 0u;
    s_stream.backoff_until_ms = 0u;
    s_stream.last_activity_ms = 0u;
    /* Diagnostics belong to one initialized lifetime. */
    s_stream.auth_failures = 0u;
    s_stream.replay_rejections = 0u;
    s_stream.dropped_rx_frames = 0u;
    s_stream.dropped_tx_frames = 0u;
    s_stream.notification_pending = false;
    jh_ble_stream_session_clear(&s_stream.session);
    s_stream.session.local_capabilities = config->capabilities;
    close_session_locked();
    s_stream.operation_active = false;
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }

  hal_mutex_unlock(mutex);
  (void)backend->stream_unpublish(backend->context);

  hal_mutex_lock(mutex);
  jh_ble_stream_session_clear(&s_stream.session);
  clear_payload_queues_locked();
  jh_secure_zeroize(s_stream.control_frame, sizeof(s_stream.control_frame));
  s_stream.control_frame_length = 0u;
  s_stream.notification_pending = false;
  s_stream.initialized = false;
  s_stream.subscribed = false;
  s_stream.native_connection = 0u;
  s_stream.att_mtu = 0u;
  s_stream.state = HAL_BLE_STREAM_STATE_UNINITIALIZED;
  s_stream.backend = nullptr;
  s_stream.last_status = published;
  s_stream.operation_active = false;
  hal_mutex_unlock(mutex);
  return published;
}

hal_status_t hal_ble_stream_deinitialize(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_stream.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (!s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }
  const jh_ble_backend_t *backend = s_stream.backend;
  s_stream.operation_active = true;
  hal_mutex_unlock(mutex);

  const hal_status_t status = backend->stream_unpublish(backend->context);
  hal_mutex_lock(mutex);
  s_stream.state = HAL_BLE_STREAM_STATE_IDLE;
  close_session_locked();
  jh_ble_stream_session_clear(&s_stream.session);
  s_stream.initialized = false;
  s_stream.subscribed = false;
  s_stream.native_connection = 0u;
  s_stream.att_mtu = 0u;
  s_stream.notification_pending = false;
  s_stream.state = HAL_BLE_STREAM_STATE_UNINITIALIZED;
  s_stream.backend = nullptr;
  s_stream.last_status = status;
  s_stream.operation_active = false;
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_ble_stream_set_secret(const uint8_t *secret, size_t length) {
  if (secret == nullptr || length < HAL_BLE_STREAM_SECRET_MIN_LEN ||
      length > HAL_BLE_STREAM_SECRET_MAX_LEN) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_stream.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (!s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  /* Rotating the secret invalidates any session built on the previous one. */
  close_session_locked();
  const hal_status_t status =
      jh_ble_stream_session_set_secret(&s_stream.session, secret, length);
  hal_mutex_unlock(mutex);
  return status;
}

hal_status_t hal_ble_stream_clear_secret(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_stream.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (!s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  jh_ble_stream_session_clear(&s_stream.session);
  s_stream.session.local_capabilities = s_stream.capabilities;
  close_session_locked();
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_ble_stream_send(const void *data, size_t length) {
  return stream_send(data, length, false, 0u, 0u);
}

hal_status_t
hal_ble_stream_receive_ex(void *out, size_t capacity, size_t *out_length,
                          hal_ble_stream_payload_info_t *out_payload_info) {
  return stream_receive(out, capacity, out_length, out_payload_info, false, 0u,
                        0u);
}

hal_status_t hal_ble_stream_receive(void *out, size_t capacity,
                                    size_t *out_length) {
  return hal_ble_stream_receive_ex(out, capacity, out_length, nullptr);
}

extern "C" hal_status_t
jh_ble_stream_send_for_session(const void *data, size_t length,
                               uint32_t expected_generation,
                               uint64_t expected_session_id) {
  return stream_send(data, length, true, expected_generation,
                     expected_session_id);
}

extern "C" hal_status_t jh_ble_stream_receive_for_session(
    void *out, size_t capacity, size_t *out_length,
    hal_ble_stream_payload_info_t *out_payload_info,
    uint32_t expected_generation, uint64_t expected_session_id) {
  return stream_receive(out, capacity, out_length, out_payload_info, true,
                        expected_generation, expected_session_id);
}

hal_status_t hal_ble_stream_get_info(hal_ble_stream_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  hal_ble_stream_info_t info{};
  info.state = s_stream.state;
  info.last_status = s_stream.last_status;
  info.capabilities = s_stream.capabilities;
  info.negotiated_capabilities = s_stream.negotiated_capabilities;
  info.generation = s_stream.generation;
  info.session_id = active_session_id_locked();
  info.tx_counter = s_stream.tx_counter;
  info.rx_counter = s_stream.rx_counter;
  info.auth_failures = s_stream.auth_failures;
  info.replay_rejections = s_stream.replay_rejections;
  info.dropped_rx_frames = s_stream.dropped_rx_frames;
  info.dropped_tx_frames = s_stream.dropped_tx_frames;
  info.pending_rx = s_stream.rx_count;
  info.pending_tx =
      s_stream.tx_count + (s_stream.notification_pending ? 1u : 0u);
  info.secret_provisioned = s_stream.session.secret_length != 0u;
  info.subscribed = s_stream.subscribed;
  hal_mutex_unlock(mutex);
  *out_info = info;
  return HAL_OK;
}

hal_status_t
hal_ble_stream_close_session(hal_ble_stream_close_reason_t reason) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_stream.operation_active) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  if (!s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (reason == HAL_BLE_STREAM_CLOSE_AUTH_FAILED) {
    ++s_stream.auth_failures;
  } else if (reason == HAL_BLE_STREAM_CLOSE_REPLAY_DETECTED) {
    ++s_stream.replay_rejections;
  }
  close_session_locked();
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

extern "C" void
jh_ble_stream_on_backend_event(const jh_ble_backend_event_t *event) {
  if (event == nullptr) {
    return;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (!s_stream.initialized || s_stream.operation_active) {
    hal_mutex_unlock(mutex);
    return;
  }
  switch (event->type) {
  case JH_BLE_BACKEND_EVENT_STREAM_SUBSCRIPTION:
    s_stream.subscribed = event->stream_subscribed;
    s_stream.native_connection = event->native_connection;
    s_stream.att_mtu = event->mtu;
    if (!s_stream.subscribed) {
      s_stream.notification_pending = false;
      close_session_locked();
    } else if (s_stream.state == HAL_BLE_STREAM_STATE_IDLE) {
      s_stream.state = HAL_BLE_STREAM_STATE_SUBSCRIBED;
    }
    break;
  case JH_BLE_BACKEND_EVENT_STREAM_WRITE: {
    const bool hello =
        event->stream_frame_length >= HAL_BLE_STREAM_FRAME_HEADER_LEN &&
        event->stream_frame[0] == HAL_BLE_STREAM_PROTOCOL_VERSION &&
        event->stream_frame[1] == JH_BLE_STREAM_FRAME_HELLO;
    if (backoff_active_locked()) {
      ++s_stream.dropped_rx_frames;
      s_stream.last_status = HAL_EAUTH;
      break;
    }
    if (s_stream.control_frame_length != 0u) {
      ++s_stream.dropped_rx_frames;
      s_stream.last_status = HAL_EBUSY;
      break;
    }
    if (hello && s_stream.notification_pending) {
      const hal_status_t discarded = s_stream.backend->stream_discard_pending(
          s_stream.backend->context, s_stream.native_connection);
      if (discarded != HAL_OK) {
        ++s_stream.dropped_rx_frames;
        s_stream.last_status = discarded;
        if (discarded != HAL_EBUSY) {
          close_session_locked();
        }
        break;
      }
      s_stream.notification_pending = false;
    }
    if (hello && event->mtu < HAL_BLE_STREAM_MIN_ATT_MTU) {
      s_stream.last_status = HAL_EOVERFLOW;
      break;
    }
    jh_ble_stream_session_result_t result{};
    const hal_status_t status = jh_ble_stream_session_handle_frame(
        &s_stream.session, event->stream_frame, event->stream_frame_length,
        &result);
    s_stream.last_status = status;
    s_stream.last_activity_ms = hal_millis();

    /* A successful HELLO starts a fresh security boundary. Payloads queued
       by the previous authenticated session must never cross it. */
    if (status == HAL_OK && hello &&
        s_stream.session.state == JH_BLE_STREAM_SESSION_HANDSHAKING) {
      clear_payload_queues_locked();
      s_stream.negotiated_capabilities = 0u;
    }

    if (status == HAL_EAUTH) {
      register_auth_failure_locked();
    } else if (result.close_reason == HAL_BLE_STREAM_CLOSE_REPLAY_DETECTED) {
      ++s_stream.replay_rejections;
    }

    if (result.response_length != 0u) {
      memcpy(s_stream.control_frame, result.response, result.response_length);
      s_stream.control_frame_length = result.response_length;
      (void)flush_tx_locked();
    }

    if (result.payload_length != 0u) {
      if (s_stream.rx_count == HAL_BLE_STREAM_RX_QUEUE_DEPTH) {
        ++s_stream.dropped_rx_frames;
        s_stream.rx_overflow_pending = true;
      } else {
        const size_t tail = (s_stream.rx_head + s_stream.rx_count) %
                            HAL_BLE_STREAM_RX_QUEUE_DEPTH;
        memcpy(s_stream.rx[tail].data, result.payload, result.payload_length);
        s_stream.rx[tail].length = result.payload_length;
        s_stream.rx[tail].info.generation = s_stream.generation;
        s_stream.rx[tail].info.session_id = active_session_id_locked();
        s_stream.rx[tail].info.counter = s_stream.session.rx_counter;
        ++s_stream.rx_count;
      }
      s_stream.rx_counter = s_stream.session.rx_counter;
    }

    if (result.close_session) {
      close_session_locked();
    } else if (s_stream.session.state == JH_BLE_STREAM_SESSION_AUTHENTICATED) {
      s_stream.state = HAL_BLE_STREAM_STATE_AUTHENTICATED;
      s_stream.negotiated_capabilities =
          (uint16_t)(s_stream.capabilities &
                     s_stream.session.peer_capabilities);
      s_stream.auth_attempts = 0u;
    } else if (s_stream.session.state == JH_BLE_STREAM_SESSION_HANDSHAKING) {
      s_stream.state = HAL_BLE_STREAM_STATE_HANDSHAKING;
    }
    jh_secure_zeroize(&result, sizeof(result));
    break;
  }
  case JH_BLE_BACKEND_EVENT_STREAM_CAN_SEND:
    s_stream.notification_pending = false;
    if (event->status == HAL_OK) {
      (void)flush_tx_locked();
    } else {
      s_stream.last_status = event->status;
      close_session_locked();
    }
    break;
  case JH_BLE_BACKEND_EVENT_MTU_UPDATED:
    if (s_stream.native_connection == event->native_connection) {
      s_stream.att_mtu = event->mtu;
    }
    break;
  default:
    break;
  }
  hal_mutex_unlock(mutex);
}

extern "C" void jh_ble_stream_on_poll(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_stream.initialized && !s_stream.operation_active) {
    (void)backoff_active_locked();
    if (s_stream.state == HAL_BLE_STREAM_STATE_AUTHENTICATED &&
        hal_millis_deadline_expired(s_stream.last_activity_ms,
                                    s_stream.idle_timeout_ms)) {
      s_stream.last_status = HAL_ETIMEOUT;
      close_session_locked();
    }
    if (s_stream.control_frame_length != 0u || s_stream.tx_count != 0u) {
      (void)flush_tx_locked();
    }
  }
  hal_mutex_unlock(mutex);
}

extern "C" void jh_ble_stream_on_link_lost(uint32_t generation) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return;
  }
  hal_mutex_lock(mutex);
  if (s_stream.initialized) {
    s_stream.generation = generation;
    s_stream.subscribed = false;
    s_stream.native_connection = 0u;
    s_stream.att_mtu = 0u;
    s_stream.notification_pending = false;
    close_session_locked();
  }
  hal_mutex_unlock(mutex);
}

#if HAL_TARGET_IS_MOCK
/* Test-only: force the runtime mutex through a real destroy so
 * Helgrind/DRD can observe the teardown path, then clear it so the next
 * operation recreates it from scratch. Firmware never calls this - call
 * hal_ble_stream_deinitialize() first so the mutex is not destroyed while
 * held or while a session is still active. */
void hal_mock_ble_stream_runtime_full_reset(void) {
  if (s_stream.mutex != nullptr) {
    hal_mutex_destroy(s_stream.mutex);
    s_stream.mutex = nullptr;
  }
}
#endif /* HAL_TARGET_IS_MOCK */

#endif /* HAL_ENABLE_BLE_STREAM */
