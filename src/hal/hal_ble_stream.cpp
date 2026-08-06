#include "hal_ble_stream.h"

#ifdef HAL_ENABLE_BLE_STREAM

#include "hal_sync.h"
#include "hal_system.h"
#include "impl/shared/bluetooth/jh_ble_backend.h"
#include "impl/shared/bluetooth/jh_ble_stream_runtime.h"
#include "impl/shared/bluetooth/jh_ble_stream_session.h"
#include "impl/shared/hal_mutex_once.h"

#include <string.h>

namespace {

struct stream_payload_t {
  uint8_t data[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t length;
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
  bool initialized;
  bool subscribed;
  bool rx_overflow_pending;
};

stream_runtime_t s_stream{};

hal_mutex_t runtime_mutex(void) {
  return jh_hal_mutex_create_once(&s_stream.mutex);
}

void zeroize(void *buffer, size_t length) {
  volatile uint8_t *bytes = static_cast<volatile uint8_t *>(buffer);
  for (size_t index = 0u; index < length; ++index) {
    bytes[index] = 0u;
  }
}

/* Drop session state and key material without touching the provisioned
   secret. */
void close_session_locked(void) {
  jh_ble_stream_session_reset(&s_stream.session);
  zeroize(s_stream.rx, sizeof(s_stream.rx));
  zeroize(s_stream.tx, sizeof(s_stream.tx));
  s_stream.rx_head = 0u;
  s_stream.rx_count = 0u;
  s_stream.tx_head = 0u;
  s_stream.tx_count = 0u;
  s_stream.rx_overflow_pending = false;
  s_stream.tx_counter = 0u;
  s_stream.rx_counter = 0u;
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

/* Push queued payloads until the controller reports backpressure. */
void flush_tx_locked(void) {
  while (s_stream.tx_count != 0u) {
    uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
    size_t frame_length = 0u;
    const stream_payload_t &payload = s_stream.tx[s_stream.tx_head];
    const hal_status_t built = jh_ble_stream_session_build_data(
        &s_stream.session, payload.data, payload.length, frame, sizeof(frame),
        &frame_length);
    if (built != HAL_OK) {
      s_stream.last_status = built;
      if (built == HAL_EOVERFLOW) {
        close_session_locked();
      }
      return;
    }
    const hal_status_t sent = s_stream.backend->stream_notify(
        s_stream.backend->context, s_stream.native_connection, frame,
        frame_length);
    zeroize(frame, sizeof(frame));
    if (sent == HAL_EAGAIN) {
      /* The counter already advanced, so the payload stays queued and the
         next attempt rebuilds it under a fresh counter. */
      return;
    }
    if (sent != HAL_OK) {
      s_stream.last_status = sent;
      return;
    }
    s_stream.tx_head = (s_stream.tx_head + 1u) % HAL_BLE_STREAM_TX_QUEUE_DEPTH;
    --s_stream.tx_count;
    s_stream.tx_counter = s_stream.session.tx_counter;
  }
}

} // namespace

hal_status_t hal_ble_stream_initialize(const hal_ble_stream_config_t *config) {
  if (config == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  const jh_ble_backend_t *backend = jh_ble_backend_instance();
  if (backend == nullptr || backend->stream_notify == nullptr ||
      backend->stream_publish == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  hal_mutex_lock(mutex);
  if (s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }
  s_stream.backend = backend;
  s_stream.capabilities = config->capabilities;
  s_stream.idle_timeout_ms = config->idle_timeout_ms != 0u
                                 ? config->idle_timeout_ms
                                 : HAL_BLE_STREAM_SESSION_IDLE_TIMEOUT_MS;
  s_stream.state = HAL_BLE_STREAM_STATE_IDLE;
  s_stream.last_status = HAL_OK;
  s_stream.subscribed = false;
  s_stream.initialized = true;
  s_stream.auth_attempts = 0u;
  s_stream.backoff_until_ms = 0u;
  /* Diagnostics belong to one initialized lifetime. */
  s_stream.auth_failures = 0u;
  s_stream.replay_rejections = 0u;
  s_stream.dropped_rx_frames = 0u;
  s_stream.dropped_tx_frames = 0u;
  s_stream.session.local_capabilities = config->capabilities;
  close_session_locked();
  hal_mutex_unlock(mutex);

  return backend->stream_publish(
      backend->context, HAL_BLE_STREAM_PROTOCOL_VERSION, config->capabilities);
}

hal_status_t hal_ble_stream_deinitialize(void) {
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  s_stream.state = HAL_BLE_STREAM_STATE_IDLE;
  close_session_locked();
  jh_ble_stream_session_clear(&s_stream.session);
  s_stream.initialized = false;
  s_stream.subscribed = false;
  s_stream.state = HAL_BLE_STREAM_STATE_UNINITIALIZED;
  s_stream.backend = nullptr;
  hal_mutex_unlock(mutex);
  return HAL_OK;
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
  if (data == nullptr || length == 0u || length > HAL_BLE_STREAM_MAX_PAYLOAD) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  if (s_stream.state != HAL_BLE_STREAM_STATE_AUTHENTICATED) {
    hal_mutex_unlock(mutex);
    return HAL_EAUTH;
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
  flush_tx_locked();
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

hal_status_t hal_ble_stream_receive(void *out, size_t capacity,
                                    size_t *out_length) {
  if (out == nullptr || out_length == nullptr || capacity == 0u) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = runtime_mutex();
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
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
  if (capacity < payload.length) {
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  memcpy(out, payload.data, payload.length);
  *out_length = payload.length;
  s_stream.rx_head = (s_stream.rx_head + 1u) % HAL_BLE_STREAM_RX_QUEUE_DEPTH;
  --s_stream.rx_count;
  hal_mutex_unlock(mutex);
  return HAL_OK;
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
  info.tx_counter = s_stream.tx_counter;
  info.rx_counter = s_stream.rx_counter;
  info.auth_failures = s_stream.auth_failures;
  info.replay_rejections = s_stream.replay_rejections;
  info.dropped_rx_frames = s_stream.dropped_rx_frames;
  info.dropped_tx_frames = s_stream.dropped_tx_frames;
  info.pending_rx = s_stream.rx_count;
  info.pending_tx = s_stream.tx_count;
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
  if (!s_stream.initialized) {
    hal_mutex_unlock(mutex);
    return;
  }
  switch (event->type) {
  case JH_BLE_BACKEND_EVENT_STREAM_SUBSCRIPTION:
    s_stream.subscribed = event->stream_subscribed;
    s_stream.native_connection = event->native_connection;
    if (!s_stream.subscribed) {
      close_session_locked();
    } else if (s_stream.state == HAL_BLE_STREAM_STATE_IDLE) {
      s_stream.state = HAL_BLE_STREAM_STATE_SUBSCRIBED;
    }
    break;
  case JH_BLE_BACKEND_EVENT_STREAM_WRITE: {
    if (backoff_active_locked()) {
      ++s_stream.dropped_rx_frames;
      s_stream.last_status = HAL_EAUTH;
      break;
    }
    jh_ble_stream_session_result_t result;
    const hal_status_t status = jh_ble_stream_session_handle_frame(
        &s_stream.session, event->stream_frame, event->stream_frame_length,
        &result);
    s_stream.last_status = status;
    s_stream.last_activity_ms = hal_millis();

    if (status == HAL_EAUTH) {
      register_auth_failure_locked();
    } else if (result.close_reason == HAL_BLE_STREAM_CLOSE_REPLAY_DETECTED) {
      ++s_stream.replay_rejections;
    }

    if (result.response_length != 0u) {
      (void)s_stream.backend->stream_notify(
          s_stream.backend->context, s_stream.native_connection,
          result.response, result.response_length);
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
    zeroize(&result, sizeof(result));
    break;
  }
  case JH_BLE_BACKEND_EVENT_STREAM_CAN_SEND:
    flush_tx_locked();
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
  if (s_stream.initialized) {
    (void)backoff_active_locked();
    if (s_stream.state == HAL_BLE_STREAM_STATE_AUTHENTICATED &&
        (uint32_t)(hal_millis() - s_stream.last_activity_ms) >=
            s_stream.idle_timeout_ms) {
      s_stream.last_status = HAL_ETIMEOUT;
      close_session_locked();
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
    close_session_locked();
  }
  hal_mutex_unlock(mutex);
}

#endif /* HAL_ENABLE_BLE_STREAM */
